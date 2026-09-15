/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <officelabs/InlineCompletionController.hxx>
#include <officelabs/AgentHttp.hxx>
#include <officelabs/AgentIdentity.hxx>
#include <officelabs/InlineCompletionEligibility.hxx>

#include <com/sun/star/awt/XVclWindowPeer.hpp>
#include <com/sun/star/beans/PropertyValue.hpp>
#include <com/sun/star/frame/XController.hpp>
#include <com/sun/star/lang/XComponent.hpp>
#include <com/sun/star/text/XTextDocument.hpp>

#include <config_folders.h>
#include <osl/file.hxx>
#include <rtl/bootstrap.hxx>
#include <rtl/strbuf.hxx>
#include <rtl/string.hxx>
#include <rtl/ustrbuf.hxx>
#include <sal/log.hxx>
#include <tools/time.hxx>
#include <tools/link.hxx>
#include <vcl/commandevent.hxx>
#include <vcl/officelabs/extinput.hxx>
#include <vcl/svapp.hxx>

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <chrono>
#include <sstream>
#include <string_view>
#include <thread>

namespace officelabs {

namespace {

const sal_Int32 COMPLETION_TIMEOUT_SECONDS = 3;

void runOnVclThread(void* pData, void*)
{
    std::unique_ptr<std::function<void()>> pFn(static_cast<std::function<void()>*>(pData));
    (*pFn)();
}

} // anonymous namespace

InlineCompletionController::Fetcher InlineCompletionController::agentFetcher()
{
    return [](const OString& rJsonBody) -> FetchResult {
        const AgentResponse aResp = httpRequest(
            "POST", "/completions/", rJsonBody,
            COMPLETION_TIMEOUT_SECONDS, readSessionToken(), OString(),
            { { "X-OfficeLabs-Feature"_ostr, "inline"_ostr } });
        return { aResp.nStatus, aResp.aBody };
    };
}

InlineCompletionController::InlineCompletionController(
    const css::uno::Reference<css::frame::XController>& xController,
    const css::uno::Reference<css::frame::XModel>& xModel,
    vcl::Window* pEditWin,
    Fetcher aFetcher,
    CaretProvider aCaretProvider,
    EnabledProvider aEnabledProvider)
    : m_xController(xController)
    , m_xModel(xModel)
    , m_pEditWin(pEditWin)
    , m_aFetcher(std::move(aFetcher))
    , m_aCaretProvider(std::move(aCaretProvider))
    , m_aEnabledProvider(std::move(aEnabledProvider))
    , m_aTimer("officelabs InlineCompletion")
    , m_aTrackTimer("officelabs InlineCompletion track")
    , m_nGeneration(0)
    , m_bInFlight(false)
    , m_bDisposed(false)
    , m_nFailures(0)
    , m_nBackoffUntilMs(0)
{
    m_aDoc.setModel(xModel);
    m_aDoc.setController(xController);
    css::uno::Reference<css::text::XTextDocument> xTextDoc(xModel, css::uno::UNO_QUERY);
    if (xTextDoc.is())
        m_aDoc.setDocument(xTextDoc);

    if (!m_aCaretProvider)
        m_aCaretProvider = [this]() { return GhostTextWindow::caretRectPixel(m_pEditWin.get()); };

    if (!m_aEnabledProvider)
        m_aEnabledProvider = [this]() { return isEnabled(); };

    m_aTimer.SetTimeout(400); // ms
    m_aTimer.SetInvokeHandler(LINK(this, InlineCompletionController, TimerHdl));

    m_aTrackTimer.SetTimeout(100); // ms
    m_aTrackTimer.SetInvokeHandler(LINK(this, InlineCompletionController, TrackTimerHdl));

    m_pShared = std::make_shared<Shared>();
    m_pShared->pOwner = this;
}

InlineCompletionController::~InlineCompletionController()
{
    SAL_WARN_IF(!m_bDisposed, "officelabs",
                "InlineCompletionController destroyed without dispose()");
}

void InlineCompletionController::start()
{
    if (m_bDisposed)
        return;

    try
    {
        m_xInputInterception.set(m_xController, css::uno::UNO_QUERY);
        if (m_xInputInterception.is())
            m_xInputInterception->addKeyHandler(this);

        css::uno::Reference<css::lang::XComponent> xComp(m_xController, css::uno::UNO_QUERY);
        if (xComp.is())
            xComp->addEventListener(this);
    }
    catch (const css::uno::Exception& e)
    {
        SAL_WARN("officelabs", "InlineCompletionController::start failed: " << e.Message);
    }

    if (m_pEditWin)
        m_pEditWin->AddEventListener(LINK(this, InlineCompletionController, WindowEventHdl));
}

void InlineCompletionController::dispose()
{
    if (m_bDisposed)
        return;
    m_bDisposed = true;

    if (m_pShared)
        m_pShared->pOwner = nullptr;

    m_aTimer.Stop();
    m_aTrackTimer.Stop();

    try
    {
        if (m_xInputInterception.is())
            m_xInputInterception->removeKeyHandler(this);

        css::uno::Reference<css::lang::XComponent> xComp(m_xController, css::uno::UNO_QUERY);
        if (xComp.is())
            xComp->removeEventListener(this);
    }
    catch (const css::uno::Exception& e)
    {
        SAL_WARN("officelabs", "InlineCompletionController::dispose cleanup failed: " << e.Message);
    }

    if (m_pEditWin)
        m_pEditWin->RemoveEventListener(LINK(this, InlineCompletionController, WindowEventHdl));

    hideGhost();
    m_pGhost.disposeAndClear();

    m_xInputInterception.clear();
    m_xController.clear();
    m_xModel.clear();
    m_pEditWin.reset();
}

bool InlineCompletionController::isFromEditWindow(
    const css::uno::Reference<css::uno::XInterface>& xSource) const
{
    if (!m_pEditWin)
        return false;

    return xSource == m_pEditWin->GetComponentInterface(false);
}

sal_Bool SAL_CALL InlineCompletionController::keyPressed(const css::awt::KeyEvent& e)
{
    if (m_bDisposed || !isFromEditWindow(e.Source))
        return false;

    if (isComposing())
    {
        hideGhost();
        ++m_nGeneration;
        return false;
    }

    const sal_Int16 nCode = e.KeyCode;
    const sal_Int16 nMods = e.Modifiers;

    if (m_pGhost && m_pGhost->isShowing())
    {
        if (nCode == css::awt::Key::TAB && nMods == 0)
        {
            OUString sText = m_sSuggestion;
            hideGhost();
            m_aDoc.insertAtCursor(sText);
            return true;
        }

        if (nCode == css::awt::Key::ESCAPE)
        {
            hideGhost();
            // keyReleased restarts the debounce for every key, Escape included,
            // which used to fetch and show the same suggestion again.
            m_oDismissed = m_aDoc.getCursorContext();
            return true;
        }
    }

    hideGhost();
    ++m_nGeneration;
    m_aTimer.Stop();
    return false;
}

sal_Bool SAL_CALL InlineCompletionController::keyReleased(const css::awt::KeyEvent& e)
{
    if (m_bDisposed || !isFromEditWindow(e.Source) || isComposing())
        return false;

    m_aTimer.Start();
    return false;
}

void SAL_CALL InlineCompletionController::disposing(const css::lang::EventObject& /*rSource*/)
{
    dispose();
}

void InlineCompletionController::requestNow()
{
    if (m_bDisposed || m_bInFlight || isComposing() || !m_aEnabledProvider())
    {
        hideGhost();
        return;
    }

    const sal_uInt64 nNow = tools::Time::GetSystemTicks();
    if (nNow < m_nBackoffUntilMs)
        return;

    CursorContext c = m_aDoc.getCursorContext();
    if (!isEligible(c))
        return;

    if (m_oDismissed)
    {
        if (m_oDismissed->textBefore == c.textBefore && m_oDismissed->textAfter == c.textAfter)
            return;
        m_oDismissed.reset();
    }

    m_aRequested = c;
    m_bInFlight = true;
    const sal_uInt64 nGen = ++m_nGeneration;
    const OString sBody = buildCompletionRequest(c);

    std::thread([pShared = m_pShared, aFetcher = m_aFetcher, sBody, nGen]() {
        // Only value data and the shared owner slot cross threads; the owner is
        // looked up again on the VCL thread, where dispose() nulls it.
        FetchResult aResult = aFetcher(sBody);
        auto* pFn = new std::function<void()>([pShared, nGen, aResult = std::move(aResult)]() {
            if (pShared->pOwner)
                pShared->pOwner->onResult(nGen, aResult);
        });
        Application::PostUserEvent(LINK_NONMEMBER(pFn, runOnVclThread));
    }).detach();
}

void InlineCompletionController::onResult(sal_uInt64 nGeneration, const FetchResult& rResult)
{
    m_bInFlight = false;

    if (rResult.nStatus != 200)
    {
        ++m_nFailures;
        if (m_nFailures >= 3)
        {
            m_nBackoffUntilMs = tools::Time::GetSystemTicks() + 30000;
            m_nFailures = 0;
        }
        return;
    }

    m_nFailures = 0;

    if (nGeneration != m_nGeneration)
        return;

    if (!stillValid(m_aRequested, m_aDoc.getCursorContext()))
        return;

    OUString sSuggestion = sanitizeSuggestion(parseFirstSuggestion(rResult.aBody));
    if (sSuggestion.isEmpty())
        return;

    auto aRect = m_aCaretProvider();
    if (!aRect)
    {
        hideGhost();
        return;
    }

    if (!m_pGhost)
        m_pGhost = VclPtr<GhostTextWindow>::Create(m_pEditWin.get());

    if (!m_pGhost->showAt(*aRect, sSuggestion))
    {
        hideGhost();
        return;
    }

    m_sSuggestion = sSuggestion;
    m_aShownRect = aRect;
    m_aTrackTimer.Start();
}

void InlineCompletionController::hideGhost()
{
    if (m_pGhost)
        m_pGhost->hide();

    m_aTrackTimer.Stop();
    m_aShownRect.reset();
    m_sSuggestion.clear();
}

bool InlineCompletionController::isComposing() const
{
    return OfficeLabsIsExtTextInputActive(m_pEditWin.get());
}

bool InlineCompletionController::isEnabled() const
{
    OUString aURL
        = u"${$BRAND_BASE_DIR/" LIBO_ETC_FOLDER "/" SAL_CONFIGFILE("bootstrap")
         ":UserInstallation}/user/officelabs/inline-completion.txt"_ustr;
    rtl::Bootstrap::expandMacros(aURL);

    osl::File aFile(aURL);
    const bool bExists = aFile.open(osl_File_OpenFlag_Read) == osl::FileBase::E_None;
    if (!bExists)
        return isInlineCompletionEnabledValue({}, false);

    char aBuffer[16]{};
    sal_uInt64 nRead = 0;
    aFile.read(aBuffer, sizeof(aBuffer), nRead);
    return isInlineCompletionEnabledValue(std::string_view(aBuffer, nRead), true);
}

IMPL_LINK(InlineCompletionController, WindowEventHdl, VclWindowEvent&, rEvent, void)
{
    const VclEventId nId = rEvent.GetId();

    switch (nId)
    {
        case VclEventId::WindowCommand:
        {
            // Dismiss on context menu only. Wheel/scroll is handled by the
            // geometry tracking timer; IME composition is read from WindowImpl.
            const auto* pCommand = static_cast<const CommandEvent*>(rEvent.GetData());
            if (!pCommand)
                break;
            if (pCommand->GetCommand() != CommandEventId::ContextMenu)
                break;
            hideGhost();
            ++m_nGeneration;
            break;
        }

        case VclEventId::WindowMouseButtonDown:
        case VclEventId::WindowLoseFocus:
        case VclEventId::WindowResize:
            hideGhost();
            ++m_nGeneration;
            break;

        case VclEventId::ObjectDying:
            dispose();
            break;

        default:
            break;
    }
}

IMPL_LINK_NOARG(InlineCompletionController, TimerHdl, Timer*, void)
{
    requestNow();
}

IMPL_LINK_NOARG(InlineCompletionController, TrackTimerHdl, Timer*, void)
{
    if (m_pGhost && m_pGhost->isShowing())
    {
        auto aRect = m_aCaretProvider();
        if (!aRect || aRect != m_aShownRect)
        {
            hideGhost();
            ++m_nGeneration;
        }
        else
        {
            m_aTrackTimer.Start();
        }
    }
}

} // namespace officelabs

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
