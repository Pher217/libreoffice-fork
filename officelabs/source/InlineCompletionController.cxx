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

#include <com/sun/star/awt/KeyModifier.hpp>
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
#include <vcl/unohelp.hxx>

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <chrono>
#include <cmath>
#include <sstream>
#include <string_view>
#include <thread>

namespace officelabs {

namespace {

const sal_Int32 COMPLETION_TIMEOUT_SECONDS = 3;

// Hung-fetcher watchdog, deliberately far above COMPLETION_TIMEOUT_SECONDS so a
// merely slow reply is never abandoned -- this fires only when the fetcher is
// wedged *outside* its own HTTP timeout (a socket that never returns, a DNS
// stall, a blocked bridge thread), which is the case that used to kill inline
// completion for the whole session (project#228).
const sal_uInt64 INFLIGHT_WATCHDOG_MS = 10000;

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
    EnabledProvider aEnabledProvider,
    FontProvider aFontProvider)
    : m_xController(xController)
    , m_xModel(xModel)
    , m_pEditWin(pEditWin)
    , m_aFetcher(std::move(aFetcher))
    , m_aCaretProvider(std::move(aCaretProvider))
    , m_aEnabledProvider(std::move(aEnabledProvider))
    , m_aFontProvider(std::move(aFontProvider))
    , m_aTimer("officelabs InlineCompletion")
    , m_aTrackTimer("officelabs InlineCompletion track")
    , m_nGeneration(0)
    , m_bInFlight(false)
    , m_bRequestPending(false)
    , m_bDisposed(false)
    , m_bTypeThroughPending(false)
    , m_nFailures(0)
    , m_nBackoffUntilMs(0)
    , m_nInFlightTimeoutMs(INFLIGHT_WATCHDOG_MS)
    , m_nInFlightDeadlineMs(0)
    , m_nInFlightGeneration(0)
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

    if (!m_aFontProvider)
        m_aFontProvider = [this]() { return cursorDocFont(); };

    // 150 ms, not 400. Measured end-to-end inference is ~0.5 s median, so a
    // 400 ms debounce was over 40% of the delay before anything appeared.
    // A short debounce is affordable now that a matching keystroke types
    // through the ghost instead of dismissing it: the common case while
    // typing is no longer "dismiss and re-fetch", so the extra fires this
    // allows are mostly suppressed by the in-flight guard rather than
    // becoming requests.
    m_aTimer.SetTimeout(150); // ms
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
            // The document can change under a shown ghost with no key event at
            // all -- a sidebar applyEdit, an agent UNO edit, a dialog's
            // replace-all -- and the caret pixel rect the tracking timer
            // watches need not move. Inserting then splices a suggestion
            // written for different text into the document.
            //
            // A type-through re-anchor posted but not yet run is the same
            // class of problem: m_aRequested still describes the
            // pre-insertion document and the typed character may not even be
            // in the document yet, so stillValid() could wrongly agree.
            //
            // Consume the key rather than returning false: letting Writer
            // insert a literal tab at the caret would be an unintended edit
            // made at the exact moment the user tried to accept a suggestion,
            // which is the class of surprise this check exists to prevent.
            if (m_bTypeThroughPending || !stillValid(m_aRequested, m_aDoc.getCursorContext()))
            {
                hideGhost();
                ++m_nGeneration;
                return true;
            }

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

        // Type-through. A printable character that matches the head of the
        // suggestion keeps the ghost and consumes one character from it,
        // instead of dismissing and re-fetching. This is what makes the
        // suggestion feel like it is being typed into rather than flickering:
        // the user types along it and the remainder shrinks from the left.
        //
        // The key is NOT consumed -- Writer must insert it as normal. The
        // ghost is re-anchored afterwards from a posted user event, because
        // at this point the document and the caret have not moved yet.
        // KeyModifier::SHIFT is set for every uppercase letter and shifted
        // symbol (see toolkit's createKeyEvent), so it must stay allowed here
        // or capitals never type through; MOD1/MOD2/MOD3 (Ctrl/Alt/Meta) are
        // still excluded since those are shortcuts, not typed text.
        const sal_Unicode cTyped = static_cast<sal_Unicode>(e.KeyChar);
        const bool bOnlyShiftOrNoMods
            = (nMods & ~css::awt::KeyModifier::SHIFT) == 0;
        if (bOnlyShiftOrNoMods && cTyped >= 0x20 && !m_sSuggestion.isEmpty()
            && m_sSuggestion[0] == cTyped)
        {
            const OUString sRemainder = m_sSuggestion.copy(1);
            SAL_INFO("officelabs.inline",
                     "type-through: consumed '" << OUString(cTyped)
                     << "', " << sRemainder.getLength() << " left");

            // The document has not been touched by this key yet -- Writer may
            // buffer it behind a pending-input timer, or run autocorrect on
            // it synchronously before the callback below runs. Predict the
            // context the insertion should produce and let
            // reanchorAfterTypeThrough refuse to re-anchor if the document
            // does not actually match it once the callback runs.
            CursorContext aExpected = m_aDoc.getCursorContext();
            aExpected.textBefore += OUString(cTyped);

            m_bTypeThroughPending = true;
            auto* pFn = new std::function<void()>(
                [pShared = m_pShared, sRemainder, aExpected]() {
                    if (pShared->pOwner)
                        pShared->pOwner->reanchorAfterTypeThrough(sRemainder, aExpected);
                });
            Application::PostUserEvent(LINK_NONMEMBER(pFn, runOnVclThread));
            return false;
        }
    }

    // Escape before a pending suggestion arrived dismisses it just the same.
    // A bare Escape with nothing pending records nothing.
    if (nCode == css::awt::Key::ESCAPE && m_bInFlight)
        m_oDismissed = m_aDoc.getCursorContext();

    hideGhost();
    ++m_nGeneration;
    m_aTimer.Stop();
    return false;
}

sal_Bool SAL_CALL InlineCompletionController::keyReleased(const css::awt::KeyEvent& e)
{
    if (m_bDisposed || !isFromEditWindow(e.Source) || isComposing())
        return false;

    // A ghost still on screen here means the key was typed through it and the
    // remainder still applies. Re-arming the debounce would fetch a competing
    // suggestion and swap the one the user is typing along, which is the
    // flicker this behaviour exists to remove. The next miss dismisses the
    // ghost and restarts the debounce as usual.
    if (m_pGhost && m_pGhost->isShowing())
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
    if (m_bDisposed || isComposing() || !m_aEnabledProvider())
    {
        hideGhost();
        return;
    }

    const sal_uInt64 nNow = tools::Time::GetSystemTicks();

    // Watchdog, before the in-flight guard below: a fetcher that never returns
    // would otherwise hold the slot forever and turn every later fire into the
    // early return -- inline completion silently dead for the rest of the
    // session, with both flags stuck set, since onResult() is the only place
    // either is cleared (project#228). The thread is detached and cannot be
    // cancelled, so what is abandoned is the slot, not the request; the late
    // reply is then ignored by the identity check in onResult().
    if (m_bInFlight && nNow >= m_nInFlightDeadlineMs)
    {
        SAL_WARN("officelabs",
                 "inline completion: fetcher did not return within "
                     << m_nInFlightTimeoutMs << " ms; abandoning the request");
        m_bInFlight = false;
        m_bRequestPending = false;
        // Invalidate the slot's identity too, or a reply for the request just
        // abandoned still matches m_nInFlightGeneration and is treated below as
        // though someone were waiting for it. 0 never collides: m_nGeneration
        // is pre-incremented, so a real request's generation starts at 1.
        m_nInFlightGeneration = 0;

        // The watchdog giving up IS the failure (project#234): it is
        // unambiguous evidence attributable to the right request at the right
        // time, unlike whatever the request's reply says if it ever lands.
        // onResult() ignores that reply entirely once it is no longer the
        // slot owner, so this is the only place an abandoned request is
        // counted -- never twice, never masked by a late 200.
        noteFailure();
    }

    // One request at a time. Remember that this fire was suppressed: the timer
    // is one-shot and only keyReleased re-arms it, so a user who stops typing
    // while a request is in flight would otherwise never get a request for the
    // text they just finished -- and the in-flight reply is discarded as stale
    // because every keyPressed bumps the generation.
    if (m_bInFlight)
    {
        m_bRequestPending = true;
        return;
    }

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
    m_nInFlightGeneration = nGen;
    m_nInFlightDeadlineMs = nNow + m_nInFlightTimeoutMs;
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

void InlineCompletionController::noteFailure()
{
    ++m_nFailures;
    if (m_nFailures >= 3)
    {
        m_nBackoffUntilMs = tools::Time::GetSystemTicks() + 30000;
        m_nFailures = 0;
    }
}

void InlineCompletionController::onResult(sal_uInt64 nGeneration, const FetchResult& rResult)
{
    // Only the reply for the request that actually holds the slot may release
    // it. m_nGeneration is bumped by every keystroke, so it identifies staleness
    // (checked further down) and not identity: without this, a request the
    // watchdog already abandoned could return late and free the slot belonging
    // to the newer request that replaced it, putting two fetches in flight at
    // once (project#228).
    const bool bOwnsSlot = (nGeneration == m_nInFlightGeneration);
    if (bOwnsSlot)
    {
        m_bInFlight = false;

        // Re-arm for text typed while this request was in flight. Done first, so
        // it happens on every path below -- most of which drop the result.
        if (m_bRequestPending)
        {
            m_bRequestPending = false;
            m_aTimer.Start();
        }
    }

    // A reply that does not own the slot belongs to a request the watchdog
    // already abandoned (requestNow() is the only place that clears
    // m_nInFlightGeneration, and only for that reason). Its failure was
    // already counted there, and its outcome is stale either way -- counting
    // a late non-200 again would double-count, and a late 200 resetting
    // m_nFailures would mask real consecutive failures of the request that
    // has since taken the slot (project#234). Discard it outright.
    if (!bOwnsSlot)
        return;

    if (rResult.nStatus != 200)
    {
        noteFailure();
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

    if (!m_pGhost->showAt(*aRect, sSuggestion, m_aFontProvider()))
    {
        hideGhost();
        return;
    }

    m_sSuggestion = sSuggestion;
    m_aShownRect = aRect;
    m_aTrackTimer.Start();
}

void InlineCompletionController::reanchorAfterTypeThrough(const OUString& rRemainder,
                                                           const CursorContext& rExpected)
{
    m_bTypeThroughPending = false;

    if (m_bDisposed || !m_pGhost || !m_pGhost->isShowing())
        return;

    // Guard: confirm the typed character actually landed where keyPressed
    // predicted before touching anything else. Writer buffers typed input
    // behind a pending-input flush timer, and autocorrect can rewrite text
    // before the caret synchronously -- either way the remainder computed in
    // keyPressed no longer corresponds to the document, and re-anchoring
    // against it would show or accept the wrong text.
    //
    // Trade-off: during fast typing this guard usually fails, so the ghost is
    // dismissed and re-fetched after the debounce instead of following the
    // typing. That is deliberate -- it is strictly better than re-anchoring
    // against a stale document, which the tracking timer would dismiss within
    // 100 ms anyway (a flicker). Type-through still works at normal typing
    // speed, where Writer flushes synchronously.
    const CursorContext aCurrent = m_aDoc.getCursorContext();
    if (rExpected.textBefore != aCurrent.textBefore || rExpected.textAfter != aCurrent.textAfter)
    {
        hideGhost();
        m_aTimer.Start();
        return;
    }

    // The whole suggestion has now been typed out by hand: nothing is left to
    // offer, so drop it and let the normal debounce ask for the next one.
    if (rRemainder.isEmpty())
    {
        hideGhost();
        m_aTimer.Start();
        return;
    }

    auto aRect = m_aCaretProvider();
    if (!aRect)
    {
        hideGhost();
        m_aTimer.Start();
        return;
    }

    // Re-anchor against the document as it now is, so the suggestion the user
    // is typing along stays the one Tab would accept and stillValid keeps
    // agreeing with it.
    m_aRequested = aCurrent;
    m_sSuggestion = rRemainder;

    if (!m_pGhost->showAt(*aRect, rRemainder, m_aFontProvider()))
    {
        hideGhost();
        m_aTimer.Start();
        return;
    }

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

std::optional<vcl::Font> InlineCompletionController::cursorDocFont()
{
    if (!m_pEditWin)
        return std::nullopt;

    std::optional<CursorCharFont> aCharFont = m_aDoc.getCursorCharFont();
    if (!aCharFont)
        return std::nullopt;

    const sal_Int32 nTwips = static_cast<sal_Int32>(std::lround(aCharFont->heightPt * 20));
    const tools::Long nHeightPixel
        = m_pEditWin->LogicToPixel(Size(0, nTwips)).Height();
    if (nHeightPixel < 1)
        return std::nullopt;

    vcl::Font aFont;
    aFont.SetFamilyName(aCharFont->familyName);
    aFont.SetFontHeight(nHeightPixel);
    aFont.SetWeight(vcl::unohelper::ConvertFontWeight(aCharFont->weight));
    aFont.SetItalic(vcl::unohelper::ConvertFontSlant(aCharFont->slant));

    return aFont;
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
            m_aTimer.Start();
        }
        else
        {
            m_aTrackTimer.Start();
        }
    }
}

} // namespace officelabs

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
