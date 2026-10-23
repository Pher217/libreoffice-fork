/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * OfficeLabs WebView Panel Implementation
 *
 * Embeds a CEF browser inside a native host window/view tracked to the
 * sidebar. The browser loads a React chat app (from Vite dev server or
 * bundled files).
 *
 * PER-FRAME BROWSER ARCHITECTURE
 * --------------------------------
 * Each LibreOffice document frame (Writer, Calc, Impress, ...) gets its
 * OWN independent CEF browser + native host window/view.  State is kept in
 * a std::map<NativeWindowHandle, PerFrameCefState> keyed by the LO frame's
 * native handle (HWND on Windows, NSView* on macOS).
 *
 * BROWSER PERSISTENCE (within a frame)
 * -------------------------------------
 * The CEF browser, native host, message router, and client for a frame live
 * in PerFrameCefState and survive panel destruction/recreation within that
 * frame.  This handles OLE in-place activation (charts, equations) which
 * temporarily destroys and recreates the sidebar panel.
 *
 * When the sidebar destroys the panel for a frame, the destructor only
 * detaches the handler/client/host from this panel instance (rebinding
 * their panel pointer to null).  When the sidebar recreates the panel for
 * the same frame, the constructor reattaches to the existing browser — no
 * page reload, no localStorage loss.
 *
 * VISIBILITY MANAGEMENT
 * ----------------------
 * syncCefWindowSize() asks the native host to show itself only when VCL
 * considers the sidebar visible; the host itself applies additional native
 * gating (frame active/minimized, fullscreen guard) before actually
 * showing.  When a frame is activated, the host broadcasts a sync to all
 * live panels immediately so the transition is instant.
 *
 * PLATFORM ISOLATION (mac port, Decision 5)
 * -------------------------------------------
 * This file contains NO raw native window calls (no HWND, no Cocoa). All
 * native window operations go through INativeCefHost — see
 * officelabs/INativeCefHost.hxx, WebViewPanelHostWin.cxx (Windows),
 * WebViewPanelHostMac.mm (macOS).
 */

#ifdef HAVE_FEATURE_CEF

#include <officelabs/WebViewPanel.hxx>
#include <officelabs/TrustedUrl.hxx>
#include <officelabs/INativeCefHost.hxx>
#include <officelabs/CefInit.hxx>
#include <officelabs/WebViewMessageHandler.hxx>
#include <officelabs/DocumentController.hxx>
#include <officelabs/StudioWindow.hxx>

#include <sfx2/bindings.hxx>
#include <sfx2/dispatch.hxx>
#include <sfx2/objsh.hxx>
#include <sfx2/viewfrm.hxx>
#include <vcl/svapp.hxx>
#include <vcl/syschild.hxx>
#include <vcl/sysdata.hxx>
#include <sal/log.hxx>
#include <rtl/bootstrap.hxx>
#include <osl/file.hxx>
#include <tools/link.hxx>
#include <toolkit/helper/vclunohelper.hxx>

#include <com/sun/star/lang/IllegalArgumentException.hpp>
#include <com/sun/star/text/XTextDocument.hpp>
#include <com/sun/star/sheet/XSpreadsheetDocument.hpp>
#include <com/sun/star/drawing/XDrawPagesSupplier.hpp>
#include <sfx2/docfac.hxx>

#include <include/cef_app.h>
#include <include/cef_keyboard_handler.h>
#include <include/cef_load_handler.h>

#include <functional>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>
#include <algorithm>

// ============================================================
// Per-frame CEF state — one entry per LO document frame handle
// ============================================================
namespace {

/// State that belongs to a single document frame (Writer/Calc/Impress window).
/// Created when the AI panel is first opened in that frame.
/// Survives panel destroy/recreate within the same frame (e.g. OLE activation).
/// Destroyed when the frame's native handle is destroyed (native host
/// observes this and calls WebViewPanel::eraseFrameState()), or explicitly
/// via WebViewPanel::cleanupPersistentBrowser() at CEF shutdown.
struct PerFrameCefState
{
    CefRefPtr<CefBrowser>                        browser;
    std::unique_ptr<officelabs::INativeCefHost>  host;
    CefRefPtr<CefMessageRouterBrowserSide>       messageRouter;
    std::unique_ptr<officelabs::WebViewMessageHandler> messageHandler;
    CefRefPtr<CefClient>                         client;
    SfxObjectShell*                              lastDocShell = nullptr;
    bool                                         browserCreated = false;
};

/// All per-frame state, keyed by the LO frame's native handle.
static std::map<officelabs::NativeWindowHandle, PerFrameCefState> s_perFrameState;

/// All currently alive WebViewPanel instances.
/// Used to broadcast syncCefWindowSize() to all panels on frame activation.
static std::vector<officelabs::WebViewPanel*> s_allPanels;

} // anonymous namespace

// ============================================================
// Helpers
// ============================================================
namespace {

// CEF normalizes CefKeyEvent::windows_key_code to the Windows VK_* numbering
// on EVERY platform (a CEF convention, not a Windows-only artifact -- see
// include/internal/cef_types.h). These two are defined locally, as plain
// portable integers, so this cross-platform file does not need <windows.h>
// (which is excluded on mac by WebViewPanel.hxx's #ifdef _WIN32 guard) just
// to name them.
constexpr int kVkF5 = 0x74;      // VK_F5
constexpr int kVkEscape = 0x1B;  // VK_ESCAPE

// Helper: invoke a std::function on VCL thread via PostUserEvent
void VclDispatchCb(void* pData, void*)
{
    auto* pFn = static_cast<std::function<void()>*>(pData);
    (*pFn)();
    delete pFn;
}

void postToVclThread(std::function<void()> fn)
{
    auto* pData = new std::function<void()>(std::move(fn));
    Application::PostUserEvent(LINK_NONMEMBER(pData, VclDispatchCb));
}

} // anonymous namespace

namespace {

// Minimal helpers for the load-error fallback page.

static std::string htmlEscape(const std::string& rInput)
{
    std::string aOutput;
    aOutput.reserve(rInput.size());
    for (char c : rInput)
    {
        switch (c)
        {
            case '&': aOutput += "&amp;"; break;
            case '<': aOutput += "&lt;"; break;
            case '>': aOutput += "&gt;"; break;
            case '"': aOutput += "&quot;"; break;
            case '\'': aOutput += "&#39;"; break;
            default: aOutput += c; break;
        }
    }
    return aOutput;
}

static std::string base64Encode(const std::string& rInput)
{
    static const char aChars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string aOutput;
    const size_t nLen = rInput.size();
    size_t i = 0;
    while (i + 3 <= nLen)
    {
        unsigned int v = (static_cast<unsigned char>(rInput[i]) << 16)
                       | (static_cast<unsigned char>(rInput[i + 1]) << 8)
                       | static_cast<unsigned char>(rInput[i + 2]);
        aOutput += aChars[(v >> 18) & 0x3F];
        aOutput += aChars[(v >> 12) & 0x3F];
        aOutput += aChars[(v >> 6) & 0x3F];
        aOutput += aChars[v & 0x3F];
        i += 3;
    }
    if (i < nLen)
    {
        unsigned int v = static_cast<unsigned char>(rInput[i]) << 16;
        if (i + 1 < nLen)
            v |= static_cast<unsigned char>(rInput[i + 1]) << 8;
        aOutput += aChars[(v >> 18) & 0x3F];
        aOutput += aChars[(v >> 12) & 0x3F];
        aOutput += (i + 1 < nLen) ? aChars[(v >> 6) & 0x3F] : '=';
        aOutput += '=';
    }
    return aOutput;
}

} // anonymous namespace

namespace officelabs {

// ============================================================
// CefClient implementation for this panel
// ============================================================
class WebViewCefClient final : public CefClient,
                                public CefLifeSpanHandler,
                                public CefRequestHandler,
                                public CefKeyboardHandler,
                                public CefLoadHandler
{
public:
    WebViewCefClient(WebViewPanel* pPanel,
                     CefRefPtr<CefMessageRouterBrowserSide> router)
        : m_pPanel(pPanel)
        , m_messageRouter(router)
    {
    }

    /// Update the panel pointer (called on attach/detach within same frame).
    void setPanel(WebViewPanel* p) { m_pPanel.store(p, std::memory_order_release); }

    /// Cache the browser for use in crash recovery (when panel pointer may be null).
    void setBrowser(CefRefPtr<CefBrowser> b) { m_browser = b; }

    // CefClient
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
    CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }
    CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }

    // CefKeyboardHandler — intercept LO-bound hotkeys before CEF handles them.
    // When the AI sidebar has keyboard focus (user clicked into the chat input),
    // keys like F5 would otherwise be consumed by CEF as browser shortcuts
    // (F5 = page reload) instead of being dispatched to LibreOffice.
    // We intercept the specific hotkeys that have LO accelerator bindings and
    // ask the native host to re-dispatch them to the LO frame.
    bool OnPreKeyEvent(CefRefPtr<CefBrowser> /*browser*/,
                       const CefKeyEvent& event,
                       CefEventHandle /*os_event*/,
                       bool* /*is_keyboard_shortcut*/) override
    {
        // Only act on physical key-down events (not char events or key-up).
        if (event.type != KEYEVENT_RAWKEYDOWN && event.type != KEYEVENT_KEYDOWN)
            return false;

        const int vk = event.windows_key_code;

        // F5  → .uno:Presentation (start slideshow from first slide)
        // F5+Shift → .uno:PresentationCurrentSlide (start from current slide)
        // Escape → stop running slideshow / close fullscreen presentation
        if (vk != kVkF5 && vk != kVkEscape)
            return false;

        WebViewPanel* pPanel = m_pPanel.load(std::memory_order_acquire);
        if (!pPanel)
            return false;

        pPanel->forwardKeyToFrame(vk, (event.modifiers & EVENTFLAG_SHIFT_DOWN) != 0);
        return true;  // prevent CEF from processing this key
    }

    // CefLifeSpanHandler
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override
    {
        WebViewPanel* panel = m_pPanel.load(std::memory_order_acquire);
        if (panel)
            panel->onBrowserCreated(browser);
    }

    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override
    {
        m_messageRouter->OnBeforeClose(browser);
    }

    // CefRequestHandler
    bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefRequest> request,
                        bool /*user_gesture*/,
                        bool /*is_redirect*/) override
    {
        m_messageRouter->OnBeforeBrowse(browser, frame);

        // Confine the WebView to our own UI. This used to return false
        // unconditionally, so a link in agent output, a redirect, or an
        // injected iframe could navigate the view to a remote page -- and the
        // renderer injects cefQuery into EVERY page, so that page could then
        // ask for the session token and reach the whole agent surface.
        //
        // Cancelling is the conservative half of the fix. The other half is in
        // WebViewMessageHandler::OnQuery, which refuses to answer an untrusted
        // frame even if one is somehow reached: two independent checks, because
        // a single point of failure guarding a credential is not a boundary.
        const OUString sUrl = OUString::fromUtf8(request->GetURL().ToString().c_str());
        if (!officelabs::isTrustedUiUrl(sUrl))
        {
            SAL_WARN("officelabs.cef", "Blocked WebView navigation to an untrusted URL");
            return true;  // cancel
        }
        return false;  // allow
    }

    /// No popups, ever. A popup is a new browser with our client, and it would
    /// inherit cefQuery while being trivially opened by page script.
    bool OnBeforePopup(CefRefPtr<CefBrowser> /*browser*/,
                       CefRefPtr<CefFrame> /*frame*/,
                       int /*popup_id*/,
                       const CefString& target_url,
                       const CefString& /*target_frame_name*/,
                       CefLifeSpanHandler::WindowOpenDisposition /*target_disposition*/,
                       bool /*user_gesture*/,
                       const CefPopupFeatures& /*popupFeatures*/,
                       CefWindowInfo& /*windowInfo*/,
                       CefRefPtr<CefClient>& /*client*/,
                       CefBrowserSettings& /*settings*/,
                       CefRefPtr<CefDictionaryValue>& /*extra_info*/,
                       bool* /*no_javascript_access*/) override
    {
        SAL_WARN("officelabs.cef", "Blocked WebView popup to "
                 << target_url.ToString().substr(0, 80));
        return true;  // cancel
    }

    void OnRenderProcessTerminated(CefRefPtr<CefBrowser> /*browser*/,
                                    TerminationStatus status,
                                    int error_code,
                                    const CefString& /*error_string*/) override
    {
        SAL_WARN("officelabs.cef", "Render process terminated, status="
                 << static_cast<int>(status) << " code=" << error_code);

        // Use cached browser ref instead of going through the panel pointer.
        // During OLE activation (doVerb), the panel pointer may be null.
        CefRefPtr<CefBrowser> browser = m_browser;
        postToVclThread([browser]() {
            if (browser)
            {
                SAL_INFO("officelabs.cef", "Reloading browser after render crash");
                browser->Reload();
            }
        });
    }

    bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                   CefRefPtr<CefFrame> frame,
                                   CefProcessId source_process,
                                   CefRefPtr<CefProcessMessage> message) override
    {
        return m_messageRouter->OnProcessMessageReceived(
            browser, frame, source_process, message);
    }

    // CefLoadHandler
    void OnLoadStart(CefRefPtr<CefBrowser> /*browser*/,
                     CefRefPtr<CefFrame> frame,
                     TransitionType /*transitionType*/) override
    {
        if (frame && frame->IsMain())
        {
            const std::string url = frame->GetURL().ToString();
            if (url.rfind("data:", 0) != 0)
                m_bShowingLoadError = false;
        }
    }

    void OnLoadEnd(CefRefPtr<CefBrowser> /*browser*/,
                   CefRefPtr<CefFrame> frame,
                   int /*httpStatusCode*/) override
    {
        if (frame && frame->IsMain())
        {
            const std::string url = frame->GetURL().ToString();
            if (url.rfind("data:", 0) != 0)
                m_bShowingLoadError = false;
        }
    }

    void OnLoadError(CefRefPtr<CefBrowser> /*browser*/,
                     CefRefPtr<CefFrame> frame,
                     ErrorCode errorCode,
                     const CefString& errorText,
                     const CefString& failedUrl) override
    {
        if (!frame || !frame->IsMain())
            return;

        const std::string url = failedUrl.ToString();
        if (url.rfind("data:", 0) == 0)
            return;

        if (errorCode == ERR_ABORTED)
            return;

        const std::string text = errorText.ToString();

        SAL_WARN("officelabs.cef",
                 "Main frame load failed: url=" << url
                 << " code=" << static_cast<int>(errorCode)
                 << " text=" << text);

        if (m_bShowingLoadError)
            return;

        const std::string html =
            "<html><body>"
            "<h1>Sidebar failed to load</h1>"
            "<p><strong>URL:</strong> " + htmlEscape(url) + "</p>"
            "<p><strong>Error:</strong> " + htmlEscape(text) + "</p>"
            "</body></html>";

        const std::string dataUrl = "data:text/html;charset=utf-8;base64," + base64Encode(html);
        m_bShowingLoadError = true;
        frame->LoadURL(CefString(dataUrl));
    }

    IMPLEMENT_REFCOUNTING(WebViewCefClient);

private:
    std::atomic<WebViewPanel*> m_pPanel;
    CefRefPtr<CefMessageRouterBrowserSide> m_messageRouter;
    CefRefPtr<CefBrowser> m_browser;  // cached for crash recovery
    bool m_bShowingLoadError = false;   // guards the load-error fallback page
};

// ============================================================
// WebViewPanel
// ============================================================

std::unique_ptr<PanelLayout> WebViewPanel::Create(
    weld::Widget* pParent, SfxBindings* pBindings)
{
    if (!pParent)
        throw css::lang::IllegalArgumentException(
            u"No parent given to WebViewPanel::Create"_ustr, nullptr, 0);

    return std::make_unique<WebViewPanel>(pParent, pBindings);
}

void WebViewPanel::broadcastSyncToAllPanels()
{
    for (auto* p : s_allPanels)
        p->syncCefWindowSize();
}

void WebViewPanel::eraseFrameState(NativeWindowHandle hFrame)
{
    s_perFrameState.erase(hFrame);

    // The frame that just went away may be the one that opened the Studio
    // window (OFFICELABS_STUDIO). No-op if the Studio isn't open, or is owned
    // by a different frame -- see StudioWindow.hxx.
    officelabs::closeStudioWindowForFrame(hFrame);
}

void WebViewPanel::cleanupPersistentBrowser()
{
    SAL_INFO("officelabs.cef", "cleanupPersistentBrowser: releasing all per-frame CEF state");

    for (auto& [handle, state] : s_perFrameState)
    {
        if (state.browser)
        {
            state.browser->GetHost()->CloseBrowser(true);
            state.browser = nullptr;
        }
        state.client = nullptr;
        state.messageRouter = nullptr;
        state.messageHandler.reset();

        if (state.host)
        {
            state.host->destroy();
            state.host.reset();
        }
    }

    s_perFrameState.clear();
}

WebViewPanel::WebViewPanel(weld::Widget* pParent, SfxBindings* pBindings)
    : PanelLayout(pParent, u"WebViewPanel"_ustr, u"officelabs/ui/webviewpanel.ui"_ustr)
    , m_pBindings(pBindings)
{
    SAL_INFO("officelabs.cef", "WebViewPanel created, active frames="
             << s_perFrameState.size());

    // Register this instance for broadcast sync
    s_allPanels.push_back(this);

    // Create per-panel document bridge
    m_pDocController = std::make_unique<DocumentController>();

    // Initialize CEF if needed, then determine whether to init a new browser
    // or reattach to an existing one for this frame. The frame handle is not
    // yet known here — initOrReattachCefBrowser() resolves it after layout.
    bool bCefOk = CefInit::instance().initialize();
    if (!bCefOk)
    {
        SAL_WARN("officelabs.cef", "CEF init failed - panel will be blank");
        return;
    }

    // Defer CEF browser creation to allow sidebar layout to complete.
    // Then force the frame to re-layout so the sidebar deck picks up the
    // correct dimensions.
    postToVclThread([this]() {
        initOrReattachCefBrowser();

        // Option C: force the frame to re-layout once CEF has attached.
        if (m_pNativeHost)
            m_pNativeHost->forceFrameRelayout();

        // Force immediate size sync after re-layout
        m_nReattachGraceTicks = 10;
        syncCefWindowSize();
    });
}

WebViewPanel::~WebViewPanel()
{
    SAL_INFO("officelabs.cef", "WebViewPanel destroyed (detaching, NOT closing browser)");

    m_aResizeTimer.Stop();

    // Unregister from the broadcast list
    auto it = std::find(s_allPanels.begin(), s_allPanels.end(), this);
    if (it != s_allPanels.end())
        s_allPanels.erase(it);

    // DETACH: null out the panel pointer in the handler, client, and host so
    // they don't call back into a deleted panel. Do NOT close the browser
    // or destroy the native host — they persist in per-frame state.
    auto stateIt = s_perFrameState.find(m_hFrameHandle);
    if (stateIt != s_perFrameState.end())
    {
        auto& state = stateIt->second;
        if (state.messageHandler)
            state.messageHandler->setPanel(nullptr);
        if (state.client)
            static_cast<WebViewCefClient*>(state.client.get())->setPanel(nullptr);
        if (state.host)
            state.host->setPanel(nullptr);
    }

    // Keep the native host WHERE IT IS during the destroy/recreate transition.
    // DO NOT hide it or move it off-screen — both cause it to not re-show
    // because syncCefWindowSize() fights with the visibility state before
    // the VCL parent is fully laid out.

    // Clear instance copies (per-frame state keeps the real references alive)
    m_browser = nullptr;
    m_pNativeHost = nullptr;

    m_pBinWindow.disposeAndClear();
}

void WebViewPanel::initOrReattachCefBrowser()
{
    if (!CefInit::instance().isInitialized())
        return;

    // Get the VclBin widget for position/size reference
    css::uno::Reference<css::awt::XWindow> xChildFrame = m_xContainer->CreateChildFrame();
    m_pBinWindow = VCLUnoHelper::GetWindow(xChildFrame);
    if (!m_pBinWindow)
    {
        SAL_WARN("officelabs.cef", "initOrReattach: CreateChildFrame failed");
        return;
    }

    vcl::Window* pSizedParent = m_pBinWindow->GetParent();
    if (!pSizedParent)
    {
        SAL_WARN("officelabs.cef", "initOrReattach: no parent window");
        return;
    }

    const SystemEnvData* pFrameData = pSizedParent->GetSystemData();
    NativeWindowHandle hFrameHandle = nativeHandleFromSystemEnvData(pFrameData);
    if (!hFrameHandle)
    {
        SAL_WARN("officelabs.cef", "initOrReattach: could not get frame native handle");
        return;
    }

    m_hFrameHandle = hFrameHandle;

    // Check if this frame already has an active browser
    auto it = s_perFrameState.find(m_hFrameHandle);
    if (it != s_perFrameState.end() && it->second.browserCreated)
    {
        SAL_INFO("officelabs.cef", "initOrReattach: frame already has browser — reattaching");
        reattachCefBrowser();
    }
    else
    {
        SAL_INFO("officelabs.cef", "initOrReattach: new frame — creating browser");
        initCefBrowser();
    }
}

void WebViewPanel::initCefBrowser()
{
    // m_hFrameHandle and m_pBinWindow are set by initOrReattachCefBrowser()
    if (!m_hFrameHandle || !m_pBinWindow)
        return;

    // Create per-frame state entry
    auto& state = s_perFrameState[m_hFrameHandle];

    // --- Set up CefMessageRouter for JS <-> C++ IPC ---
    CefMessageRouterConfig config;
    config.js_query_function = "cefQuery";
    config.js_cancel_function = "cefQueryCancel";
    state.messageRouter = CefMessageRouterBrowserSide::Create(config);

    state.messageHandler = std::make_unique<WebViewMessageHandler>(this);
    state.messageRouter->AddHandler(state.messageHandler.get(), true);

    state.client = new WebViewCefClient(this, state.messageRouter);

    // Get sidebar size and position
    vcl::Window* pSizedParent = m_pBinWindow->GetParent();
    Size aParentSize = pSizedParent->GetSizePixel();
    int w = aParentSize.Width();
    int h = aParentSize.Height();
    if (w <= 0 || h <= 0) { w = 300; h = 800; }

    auto aScrPos = pSizedParent->OutputToAbsoluteScreenPixel(Point(0, 0));
    int scrX = static_cast<int>(aScrPos.X());
    int scrY = static_cast<int>(aScrPos.Y());

    // Create the native host (owned popup on Windows, child NSView on mac)
    // anchored to this frame.
    state.host = createNativeCefHost(this);
    NativeWindowHandle hCefParent = state.host->create(NativeParent{ m_hFrameHandle });

    if (hCefParent)
    {
        state.host->setBounds(scrX, scrY, w, h);
        state.host->show();
        state.host->raiseAbove();
    }

    m_pNativeHost = state.host.get();

    if (!hCefParent)
        return;

    // --- Create CEF browser ---
    CefWindowInfo windowInfo;
    CefBrowserSettings browserSettings;
    windowInfo.SetAsChild(static_cast<CefWindowHandle>(hCefParent), CefRect(0, 0, w, h));

    OUString url = getUIUrl();
    OString utf8Url = OUStringToOString(url, RTL_TEXTENCODING_UTF8);
    SAL_INFO("officelabs.cef", "Creating CEF browser for frame "
             << reinterpret_cast<sal_uIntPtr>(m_hFrameHandle)
             << " URL: " << utf8Url);

    CefBrowserHost::CreateBrowser(
        windowInfo,
        state.client,
        CefString(utf8Url.getStr()),
        browserSettings,
        nullptr,
        nullptr
    );

    state.browserCreated = true;

    // INTERIM TRIGGER. D22's real entry point is Tools > Macros > Edit Macros,
    // which means intercepting SID_BASICIDE_APPEAR -- and that interception must
    // not merge before its two regression tests exist (a Basic runtime error
    // still reaching the stock IDE with its shell, and the Basic Macros dialog's
    // Edit button still landing on the named module). Until then the window is
    // reachable only behind an env var, so nothing about the shipped product
    // changes. Opened after the sidebar browser deliberately: the same run then
    // exercises Views/SetAsChild coexistence.
    if (const char* pEnv = std::getenv("OFFICELABS_STUDIO"))
    {
        if (pEnv[0] == '1')
            officelabs::openStudioWindow(m_hFrameHandle);
    }

    // Start resize tracking timer
    m_aResizeTimer.SetInvokeHandler(LINK(this, WebViewPanel, ResizeTimerHdl));
    m_aResizeTimer.SetTimeout(500);
    m_aResizeTimer.Start();
}

void WebViewPanel::reattachCefBrowser()
{
    // m_hFrameHandle and m_pBinWindow are set by initOrReattachCefBrowser()
    if (!m_hFrameHandle || !m_pBinWindow)
        return;

    auto it = s_perFrameState.find(m_hFrameHandle);
    if (it == s_perFrameState.end())
    {
        SAL_WARN("officelabs.cef", "reattach: no per-frame state for this handle — init instead");
        initCefBrowser();
        return;
    }

    auto& state = it->second;
    SAL_INFO("officelabs.cef", "reattachCefBrowser: wiring persistent browser to new panel");

    // Copy per-frame refs into instance members
    m_browser = state.browser;
    m_pNativeHost = state.host.get();

    // Re-wire handler, client, and host to point to this (new) panel
    if (state.messageHandler)
        state.messageHandler->setPanel(this);
    if (state.client)
        static_cast<WebViewCefClient*>(state.client.get())->setPanel(this);
    if (state.host)
        state.host->setPanel(this);

    // Show the host and bring it to the top of Z-order.
    if (m_pNativeHost)
    {
        m_pNativeHost->show();
        m_pNativeHost->raiseAbove();

        if (m_browser)
        {
            m_browser->GetHost()->NotifyMoveOrResizeStarted();
            m_browser->GetHost()->WasResized();
        }
    }

    // GRACE PERIOD: prevent syncCefWindowSize() from hiding the host
    // during the first few seconds while VCL lays out the sidebar.
    // 10 ticks × 500ms timer = 5 seconds max.
    m_nReattachGraceTicks = 10;
    SAL_INFO("officelabs.cef", "reattach: grace period started (10 ticks)");

    // Start resize tracking timer
    m_aResizeTimer.SetInvokeHandler(LINK(this, WebViewPanel, ResizeTimerHdl));
    m_aResizeTimer.SetTimeout(500);
    m_aResizeTimer.Start();

    // Re-detect the document and check for document switch.
    SfxObjectShell* pPrevShell = state.lastDocShell;
    detectDocument();

    if (pPrevShell != nullptr && state.lastDocShell != pPrevShell)
    {
        SAL_INFO("officelabs.cef",
                 "reattach: document changed — clearing chat in React");
        postMessageToJS(u"{\"type\":\"session:newDocument\"}"_ustr);
    }

    SAL_INFO("officelabs.cef", "reattachCefBrowser: done");
}

void WebViewPanel::syncCefWindowSize()
{
    if (!m_pNativeHost || !m_pBinWindow)
        return;

    // Walk up the VCL hierarchy to find the largest enclosing sidebar panel.
    // m_pBinWindow->GetParent() is the panel content area, but it may be
    // smaller than the visible sidebar. Walk up until we find the panel
    // that fills the sidebar deck area.
    vcl::Window* pParent = m_pBinWindow->GetParent();
    if (!pParent)
        return;

    // Try grandparent — the deck body area which extends to the full sidebar height
    vcl::Window* pGrandParent = pParent->GetParent();
    if (pGrandParent && pGrandParent->GetSizePixel().Height() > pParent->GetSizePixel().Height())
        pParent = pGrandParent;

    bool bSidebarVisible = pParent->IsReallyVisible();

    // GRACE PERIOD after reattach (handles OLE activation): don't hide just
    // because VCL hasn't finished laying out the reattached sidebar yet.
    if (m_nReattachGraceTicks > 0)
    {
        if (bSidebarVisible)
        {
            m_nReattachGraceTicks = 0;
            SAL_INFO("officelabs.cef", "syncCef: grace period ended (VCL ready)");
        }
        else
        {
            --m_nReattachGraceTicks;
            if (m_nReattachGraceTicks == 0)
                SAL_INFO("officelabs.cef", "syncCef: grace period expired");
            return;
        }
    }

    if (!bSidebarVisible)
    {
        m_pNativeHost->hide();
        return;
    }

    // --- Position and size ---
    Size aSize = pParent->GetSizePixel();
    auto aScrPos = pParent->OutputToAbsoluteScreenPixel(Point(0, 0));

    // setBounds() performs the full native sync (fullscreen guard, minimized
    // check, z-order, actual apply) — see the platform host implementations.
    // Always called; a native no-op when nothing actually changed.
    m_pNativeHost->setBounds(static_cast<int>(aScrPos.X()), static_cast<int>(aScrPos.Y()),
                             aSize.Width(), aSize.Height());

    // NOTE: m_browser may be null during first few seconds (async creation),
    // so this is a best-effort notification, matching the pre-refactor code.
    if (m_browser)
    {
        m_browser->GetHost()->NotifyMoveOrResizeStarted();
        m_browser->GetHost()->WasResized();
    }
}

IMPL_LINK_NOARG(WebViewPanel, ResizeTimerHdl, Timer*, void)
{
    syncCefWindowSize();
    m_aResizeTimer.Start();  // Restart for next check
}

void WebViewPanel::forwardKeyToFrame(int vkCode, bool bShift)
{
    if (m_pNativeHost)
        m_pNativeHost->forwardKeyEvent(vkCode, bShift);
}

OUString WebViewPanel::getUIUrl()
{
    // Check for dev mode: OFFICELABS_UI_DEV_URL=http://localhost:5173
    const char* devUrl = std::getenv("OFFICELABS_UI_DEV_URL");
    if (devUrl && *devUrl)
    {
        SAL_INFO("officelabs.cef", "Dev mode: loading UI from " << devUrl);
        return OUString::fromUtf8(devUrl);
    }

    // Production: load from bundled files in instdir/program/officelabs-ui/
    OUString sInstDir(u"$BRAND_BASE_DIR/$BRAND_SHARE_SUBDIR/.."_ustr);
    rtl::Bootstrap::expandMacros(sInstDir);

    OUString sFileUrl;
    osl::FileBase::getAbsoluteFileURL(OUString(), sInstDir, sFileUrl);

    // Issue #155: normalise the join so exactly one '/' separates the base
    // from the relative path. getAbsoluteFileURL leaves a trailing '/' on
    // macOS bundles (e.g. ".../OfficeLabs.app/Contents/"), so a naive
    // sFileUrl + "/program/..." produces "//" in the file URL.
    if (sFileUrl.endsWith("/"))
        sFileUrl = sFileUrl.copy(0, sFileUrl.getLength() - 1);

    return sFileUrl + "/program/officelabs-ui/index.html";
}

void WebViewPanel::postMessageToJS(const OUString& jsonMessage)
{
    if (!m_browser || !m_browser->GetMainFrame())
        return;

    OString utf8 = OUStringToOString(jsonMessage, RTL_TEXTENCODING_UTF8);
    std::string js = "window.officelabs && window.officelabs.__onMessage("
                   + std::string(utf8.getStr()) + ");";

    m_browser->GetMainFrame()->ExecuteJavaScript(
        CefString(js), m_browser->GetMainFrame()->GetURL(), 0);
}

// Helper: determine app type string from factory name
static OUString appTypeFromFactory(const OUString& rFactoryName)
{
    if (rFactoryName == "swriter")
        return u"writer"_ustr;
    if (rFactoryName == "scalc")
        return u"calc"_ustr;
    if (rFactoryName == "simpress")
        return u"impress"_ustr;
    if (rFactoryName == "sdraw")
        return u"draw"_ustr;
    return u"writer"_ustr;  // safe fallback
}

void WebViewPanel::detectDocument()
{
    SfxObjectShell* pShell = nullptr;

    // Strategy 1: Use SfxBindings -> Dispatcher -> ViewFrame -> ObjectShell
    if (m_pBindings)
    {
        SfxDispatcher* pDisp = m_pBindings->GetDispatcher();
        if (pDisp)
        {
            SfxViewFrame* pViewFrame = pDisp->GetFrame();
            if (pViewFrame)
                pShell = pViewFrame->GetObjectShell();
        }
    }

    // Strategy 2: Fallback to SfxObjectShell::Current()
    if (!pShell)
        pShell = SfxObjectShell::Current();

    if (!pShell)
    {
        SAL_WARN("officelabs.cef", "detectDocument: no document shell found");
        return;
    }

    // Determine application type from factory name
    const OUString sFactory = pShell->GetFactory().GetFactoryName();
    const OUString sAppType = appTypeFromFactory(sFactory);
    m_pDocController->setAppType(sAppType);

    // Update per-frame lastDocShell
    auto stateIt = s_perFrameState.find(m_hFrameHandle);
    if (stateIt != s_perFrameState.end())
        stateIt->second.lastDocShell = pShell;

    SAL_INFO("officelabs.cef",
             "detectDocument: factory=" << sFactory << " appType=" << sAppType);

    // Bind the appropriate document interface based on app type
    if (sAppType == "writer")
    {
        css::uno::Reference<css::text::XTextDocument> xTextDoc(
            pShell->GetModel(), css::uno::UNO_QUERY);
        if (xTextDoc.is())
            m_pDocController->setDocument(xTextDoc);
    }
    else if (sAppType == "calc")
    {
        css::uno::Reference<css::sheet::XSpreadsheetDocument> xCalcDoc(
            pShell->GetModel(), css::uno::UNO_QUERY);
        if (xCalcDoc.is())
            m_pDocController->setCalcDocument(xCalcDoc);
    }
    else if (sAppType == "impress" || sAppType == "draw")
    {
        css::uno::Reference<css::drawing::XDrawPagesSupplier> xDrawDoc(
            pShell->GetModel(), css::uno::UNO_QUERY);
        if (xDrawDoc.is())
            m_pDocController->setImpressDocument(xDrawDoc);
    }
}

void WebViewPanel::onBrowserCreated(CefRefPtr<CefBrowser> browser)
{
    m_browser = browser;

    // Store in per-frame state
    auto stateIt = s_perFrameState.find(m_hFrameHandle);
    if (stateIt != s_perFrameState.end())
    {
        stateIt->second.browser = browser;
        // Give the client a direct reference for crash recovery
        if (stateIt->second.client)
            static_cast<WebViewCefClient*>(stateIt->second.client.get())->setBrowser(browser);
    }

    SAL_INFO("officelabs.cef", "CEF browser created for frame "
             << reinterpret_cast<sal_uIntPtr>(m_hFrameHandle));

    // Browser is ready — force immediate resize sync now that m_browser is set.
    postToVclThread([this]() {
        syncCefWindowSize();
        detectDocument();
    });
}

void WebViewPanel::reloadBrowser()
{
    if (m_browser)
    {
        SAL_INFO("officelabs.cef", "Reloading CEF browser after crash");
        m_browser->Reload();
    }
}

} // namespace officelabs

#endif // HAVE_FEATURE_CEF

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
