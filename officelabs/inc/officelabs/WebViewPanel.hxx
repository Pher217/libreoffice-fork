/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * OfficeLabs WebView Panel
 *
 * CEF-based sidebar panel that hosts a React chat UI.
 * Replaces AIAssistantPanel when HAVE_FEATURE_CEF is defined.
 *
 * IPC:
 *   C++ -> JS: postMessageToJS() calls window.officelabs.__onMessage(json)
 *   JS -> C++: window.cefQuery() routed to WebViewMessageHandler
 *
 * BROWSER PERSISTENCE:
 *   The CEF browser and its native host window/view live in static storage
 *   (PerFrameCefState, keyed by the LO frame's native handle) and survive
 *   panel destruction/recreation.  This prevents the sidebar from losing
 *   state when OLE in-place activation (charts, equations) temporarily
 *   changes the frame context.
 *   On panel destroy: host is hidden/detached, handler panel-pointer is nulled.
 *   On panel create:  host is shown/repositioned, handler is re-wired.
 *
 * PLATFORM ISOLATION (mac port, Decision 5):
 *   All native window operations (Win32 on Windows, Cocoa on macOS) are
 *   isolated behind INativeCefHost (see officelabs/INativeCefHost.hxx).
 *   This file and WebViewPanel.cxx are platform-neutral; the Windows impl
 *   lives in WebViewPanelHostWin.cxx, the macOS impl in
 *   WebViewPanelHostMac.mm.
 */

#ifndef INCLUDED_OFFICELABS_WEBVIEWPANEL_HXX
#define INCLUDED_OFFICELABS_WEBVIEWPANEL_HXX

#ifdef HAVE_FEATURE_CEF

// prewin/postwin must come before ANY header that might pull in <windows.h>
// (vcl/sysdata.hxx, CEF headers, etc.)
#ifdef _WIN32
#include <prewin.h>
#include <windows.h>
#include <postwin.h>
#endif

#include <officelabs/officelabsdllapi.h>
#include <officelabs/INativeCefHost.hxx>
#include <sfx2/sidebar/PanelLayout.hxx>
#include <vcl/syschild.hxx>
#include <vcl/sysdata.hxx>

#include <include/cef_browser.h>
#include <include/cef_client.h>
#include <include/wrapper/cef_message_router.h>

#include <vcl/timer.hxx>
#include <rtl/ref.hxx>
#include <memory>

class SfxBindings;
class SfxViewShell;

namespace officelabs {

class DocumentController;
class InlineCompletionController;
class WebViewMessageHandler;

class OFFICELABS_DLLPUBLIC WebViewPanel final : public PanelLayout
{
public:
    // Factory method matching AIAssistantPanel::Create() signature
    static std::unique_ptr<PanelLayout> Create(
        weld::Widget* pParent, SfxBindings* pBindings);

    /// Release static CEF resources before CefShutdown(). Called by CefInit.
    static void cleanupPersistentBrowser();

    /// Broadcast syncCefWindowSize() to every currently alive panel. Called
    /// by a native host when its frame is activated, so every sidebar's
    /// host window/view re-evaluates show/hide instantly instead of waiting
    /// for its own timer tick (mirrors the pre-refactor WM_ACTIVATE
    /// broadcast in FrameSubclassProc).
    static void broadcastSyncToAllPanels();

    /// Remove the per-frame CEF state entry for a destroyed native frame.
    /// Called by a native host when it observes the owning LO frame being
    /// torn down (Windows: WM_NCDESTROY on the subclassed frame HWND).
    static void eraseFrameState(NativeWindowHandle hFrame);

    WebViewPanel(weld::Widget* pParent, SfxBindings* pBindings);
    virtual ~WebViewPanel() override;

    // C++ -> JS: push JSON data to the React UI
    void postMessageToJS(const OUString& jsonMessage);

    // Access the browser (for message handler)
    CefRefPtr<CefBrowser> getBrowser() const { return m_browser; }

    // Called by CefLifeSpanHandler when browser is created
    void onBrowserCreated(CefRefPtr<CefBrowser> browser);

    // Reload after render process crash
    void reloadBrowser();

    // Detect/refresh the current Writer document for DocumentController
    void detectDocument();

    // Access document controller (for message handler)
    DocumentController* getDocController() const { return m_pDocController.get(); }

    // Recompute this panel's host geometry/visibility from the current VCL
    // layout and hand off to the native host. Public because the native
    // host's frame-tracking hook calls back into it (see
    // INativeCefHost::onFrameMoved()/onFrameResized()).
    void syncCefWindowSize();

    // Forward an intercepted CEF key event (F5 / Esc slideshow hotkeys) to
    // the native host so it can re-dispatch to the LO frame's accelerator
    // table. Called by WebViewCefClient::OnPreKeyEvent.
    void forwardKeyToFrame(int vkCode, bool bShift);

    /// file:// URL of the bundled UI, or the dev server when
    /// OFFICELABS_UI_DEV_URL is set. Static and public because the Studio
    /// window loads the same bundle and must not recompute the path: that
    /// expansion has already been wrong twice (a trailing slash, issue #155;
    /// an uncollapsed ".." that made the trust gate reject our own UI).
    static OUString getUIUrl();

private:
    void initOrReattachCefBrowser();
    void initCefBrowser();
    void reattachCefBrowser();

    DECL_LINK(ResizeTimerHdl, Timer*, void);

    SfxBindings* m_pBindings;

    // VclBin created via CreateChildFrame() inside the weld container
    VclPtr<vcl::Window> m_pBinWindow;

    // Non-owning: points into s_perFrameState[m_hFrameHandle].host, which
    // owns the actual native CEF host window/view and survives panel
    // destroy/recreate (OLE in-place activation). Null until
    // initOrReattachCefBrowser() resolves the frame.
    INativeCefHost* m_pNativeHost = nullptr;

    CefRefPtr<CefBrowser> m_browser;

    // Opaque native handle for the owning LO document frame: HWND on
    // Windows, NSView* on macOS. Used only as the per-frame-state map key
    // and to hand to INativeCefHost::create() -- never dereferenced here.
    NativeWindowHandle m_hFrameHandle = nullptr;

    // Resize tracking: timer polls for container size changes (fallback
    // when the native frame-tracking hook doesn't fire, e.g. mac for now).
    Timer m_aResizeTimer{ "officelabs::WebViewPanel resize" };

    // Grace period after reattach: number of timer ticks during which
    // syncCefWindowSize() will NOT hide the host.  This prevents the
    // "black sidebar" after OLE in-place activation (chart insert etc.)
    // where the VCL parent isn't laid out yet and IsReallyVisible()
    // returns false, causing an immediate hide.
    int m_nReattachGraceTicks = 0;

    // Backend document bridge (per-panel — rebuilt on each attach)
    std::unique_ptr<DocumentController> m_pDocController;

    // Writer inline-completion key handler, wired to the current frame's edit
    // window and controller. Recreated when the frame/controller changes.
    rtl::Reference<InlineCompletionController> m_xInlineCompletion;

    // NOTE: CefClient, CefMessageRouter, WebViewMessageHandler, and the
    // INativeCefHost are NOT per-instance — they live in static per-frame
    // storage in WebViewPanel.cxx and survive panel destruction/recreation.
};

} // namespace officelabs

#endif // HAVE_FEATURE_CEF
#endif // INCLUDED_OFFICELABS_WEBVIEWPANEL_HXX

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
