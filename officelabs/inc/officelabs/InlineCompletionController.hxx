/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef INCLUDED_OFFICELABS_INLINECOMPLETIONCONTROLLER_HXX
#define INCLUDED_OFFICELABS_INLINECOMPLETIONCONTROLLER_HXX

#include <officelabs/officelabsdllapi.h>
#include <officelabs/DocumentController.hxx>
#include <officelabs/GhostTextWindow.hxx>

#include <com/sun/star/awt/Key.hpp>
#include <com/sun/star/awt/KeyEvent.hpp>
#include <com/sun/star/awt/XKeyHandler.hpp>
#include <com/sun/star/awt/XUserInputInterception.hpp>
#include <com/sun/star/frame/XController.hpp>
#include <com/sun/star/frame/XModel.hpp>
#include <com/sun/star/lang/EventObject.hpp>
#include <com/sun/star/lang/XComponent.hpp>
#include <com/sun/star/lang/XEventListener.hpp>

#include <cppuhelper/compbase.hxx>
#include <cppuhelper/implbase.hxx>
#include <rtl/ref.hxx>
#include <tools/time.hxx>
#include <vcl/svapp.hxx>
#include <vcl/timer.hxx>
#include <vcl/vclptr.hxx>
#include <vcl/vclevent.hxx>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>

namespace officelabs {

/// UNO key handler + timer that drives inline ghost-text completions for
/// Writer. Runs entirely inside CEF builds because it depends on AgentHttp.
class OFFICELABS_DLLPUBLIC InlineCompletionController final
    : public cppu::WeakImplHelper<css::awt::XKeyHandler>
{
public:
    struct FetchResult
    {
        long nStatus = 0;
        std::string aBody;
    };

    /// Performs the agent request. Injectable so tests need no agent.
    using Fetcher = std::function<FetchResult(const OString& rJsonBody)>;

    static Fetcher agentFetcher();

    InlineCompletionController(
        const css::uno::Reference<css::frame::XController>& xController,
        const css::uno::Reference<css::frame::XModel>& xModel,
        vcl::Window* pEditWin,
        Fetcher aFetcher = agentFetcher());

    void start();
    void dispose();

    const css::uno::Reference<css::frame::XController>& controller() const
    {
        return m_xController;
    }

    // XKeyHandler
    virtual sal_Bool SAL_CALL keyPressed(const css::awt::KeyEvent& e) override;
    virtual sal_Bool SAL_CALL keyReleased(const css::awt::KeyEvent& e) override;
    virtual void SAL_CALL disposing(const css::lang::EventObject& Source) override;

    /// Synchronously start a completion request (used by timer and tests).
    void requestNow();

    // Test accessors.
    bool isGhostVisible() const { return m_bSuggestionPending || (m_pGhost && m_pGhost->isShowing()); }
    const OUString& ghostText() const { return m_sSuggestion; }
    OUString pendingSuggestion() const { return m_bSuggestionPending ? m_sSuggestion : OUString(); }
    bool isInFlight() const { return m_bInFlight; }

private:
    ~InlineCompletionController();

    bool isFromEditWindow(const css::uno::Reference<css::uno::XInterface>& xSource) const;
    void onResult(sal_uInt64 nGeneration, const FetchResult& rResult);
    void hideGhost();

    DECL_LINK(WindowEventHdl, VclWindowEvent&, void);
    DECL_LINK(TimerHdl, Timer*, void);

    css::uno::Reference<css::frame::XController> m_xController;
    css::uno::Reference<css::frame::XModel> m_xModel;
    css::uno::Reference<css::awt::XUserInputInterception> m_xInputInterception;

    DocumentController m_aDoc;
    VclPtr<vcl::Window> m_pEditWin;
    VclPtr<GhostTextWindow> m_pGhost;

    Fetcher m_aFetcher;
    Timer m_aTimer;

    std::atomic<sal_uInt64> m_nGeneration;
    std::atomic<bool> m_bInFlight;
    bool m_bComposing;
    bool m_bDisposed;

    int m_nFailures;
    sal_uInt64 m_nBackoffUntilMs;

    CursorContext m_aRequested;

    OUString m_sSuggestion;
    bool m_bSuggestionPending;

    struct Shared
    {
        InlineCompletionController* pOwner;
    };
    std::shared_ptr<Shared> m_pShared;

};

} // namespace officelabs

#endif // INCLUDED_OFFICELABS_INLINECOMPLETIONCONTROLLER_HXX

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
