/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * See officelabs/inc/officelabs/StudioWindow.hxx for what this is and why it
 * carries its own message router.
 */

#include <officelabs/StudioWindow.hxx>

#if defined(HAVE_FEATURE_CEF)

#include <officelabs/TrustedUrl.hxx>
#include <officelabs/WebViewMessageHandler.hxx>
#include <officelabs/WebViewPanel.hxx>

#include <include/cef_app.h>
#include <include/cef_client.h>
#include <include/cef_task.h>
#include <include/views/cef_browser_view.h>
#include <include/views/cef_browser_view_delegate.h>
#include <include/views/cef_window.h>
#include <include/views/cef_window_delegate.h>
#include <include/wrapper/cef_message_router.h>

#include <sal/log.hxx>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

namespace officelabs
{

#if defined(MACOSX)
/// Defined in StudioWindowMac.mm. On macOS the CEF UI thread IS the AppKit main
/// thread, so waiting for the close to complete means pumping that run loop --
/// a plain sleep would deadlock against the work we are waiting for.
void studioWindowPumpNativeEvents(double fSeconds);
#endif

namespace
{

CefRefPtr<CefWindow> g_studioWindow;
std::atomic<bool> g_bBrowserClosed{ false };
std::atomic<bool> g_bWindowDestroyed{ false };

/// The Studio's URL: the same bundle the sidebar loads, with ?view=studio.
///
/// Deliberately reuses WebViewPanel::getUIUrl() rather than recomputing the
/// path. That expansion has been wrong twice already (a trailing slash, issue
/// #155; and an uncollapsed ".." that made the trust gate reject our own UI), so
/// a third copy is a third place for it to drift.
OUString studioUrl()
{
    const OUString sUrl = WebViewPanel::getUIUrl();
    if (sUrl.isEmpty())
        return sUrl;
    return sUrl + (sUrl.indexOf('?') < 0 ? u"?view=studio"_ustr : u"&view=studio"_ustr);
}

/* ---------------------------------------------------------------------------
 * The client.
 *
 * Its own CefMessageRouterBrowserSide and its own WebViewMessageHandler, built
 * with a null panel: the Studio has no SfxBindings and owns no document, so the
 * document operations (getDocument, getSelection, applyEdit, getAppType,
 * getDocumentUrl) answer Failure(500, "Panel not available") by design, while
 * the panel-independent handlers -- getSessionToken above all, and
 * requestConsent -- work exactly as they do in the sidebar.
 * ------------------------------------------------------------------------- */
class StudioClient final : public CefClient,
                           public CefLifeSpanHandler,
                           public CefRequestHandler,
                           public CefLoadHandler
{
public:
    StudioClient()
    {
        CefMessageRouterConfig config;
        config.js_query_function = "cefQuery";
        config.js_cancel_function = "cefQueryCancel";
        m_router = CefMessageRouterBrowserSide::Create(config);

        m_handler = std::make_unique<WebViewMessageHandler>(nullptr);
        m_router->AddHandler(m_handler.get(), true);
    }

    ~StudioClient() override
    {
        if (m_router && m_handler)
            m_router->RemoveHandler(m_handler.get());
    }

    // CefClient
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
    CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }

    bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<CefFrame> frame,
                                  CefProcessId source_process,
                                  CefRefPtr<CefProcessMessage> message) override
    {
        // The line whose absence made the spike window inert.
        return m_router->OnProcessMessageReceived(browser, frame, source_process, message);
    }

    // CefLifeSpanHandler
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override
    {
        SAL_INFO("officelabs.cef", "Studio browser created id=" << browser->GetIdentifier());
    }

    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override
    {
        m_router->OnBeforeClose(browser);
        g_bBrowserClosed.store(true, std::memory_order_release);
    }

    /// No popups, ever -- same reasoning as the sidebar client: a popup is a new
    /// browser with our client, and it would inherit cefQuery while being
    /// trivially opened by page script.
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
        SAL_WARN("officelabs.cef", "Blocked Studio popup to "
                 << target_url.ToString().substr(0, 80));
        return true; // cancel
    }

    // CefRequestHandler
    ///
    /// The confinement half of the token boundary, repeated here on purpose.
    /// The Studio is a SECOND WebView that answers getSessionToken, so it needs
    /// the same two independent checks the sidebar got in fork#26: navigation is
    /// confined here, and WebViewMessageHandler::OnQuery refuses an untrusted
    /// frame even if one is somehow reached. A new window that only inherited
    /// the second check would be a new way to reach the first one's failure.
    bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefRequest> request,
                        bool /*user_gesture*/,
                        bool /*is_redirect*/) override
    {
        m_router->OnBeforeBrowse(browser, frame);

        const OUString sUrl = OUString::fromUtf8(request->GetURL().ToString().c_str());
        if (!officelabs::isTrustedUiUrl(sUrl))
        {
            SAL_WARN("officelabs.cef", "Blocked Studio navigation to an untrusted URL");
            return true; // cancel
        }
        return false; // allow
    }

    void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                   TerminationStatus status,
                                   int error_code,
                                   const CefString& /*error_string*/) override
    {
        m_router->OnRenderProcessTerminated(browser);
        SAL_WARN("officelabs.cef", "Studio render process terminated, status="
                 << static_cast<int>(status) << " code=" << error_code);
    }

    // CefLoadHandler
    void OnLoadEnd(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, int httpStatusCode) override
    {
        if (frame && frame->IsMain())
            SAL_INFO("officelabs.cef", "Studio main frame loaded, status=" << httpStatusCode);
    }

    void OnLoadError(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, ErrorCode errorCode,
                     const CefString& errorText, const CefString& failedUrl) override
    {
        if (!frame || !frame->IsMain())
            return;
        SAL_WARN("officelabs.cef", "Studio load failed " << errorCode << " "
                 << errorText.ToString() << " url=" << failedUrl.ToString());
    }

private:
    CefRefPtr<CefMessageRouterBrowserSide> m_router;
    std::unique_ptr<WebViewMessageHandler> m_handler;

    IMPLEMENT_REFCOUNTING(StudioClient);
    DISALLOW_COPY_AND_ASSIGN(StudioClient);
};

/// Suppress the Chrome-style toolbar; the Studio draws its own chrome.
class StudioBrowserViewDelegate final : public CefBrowserViewDelegate
{
public:
    StudioBrowserViewDelegate() = default;

    ChromeToolbarType GetChromeToolbarType(CefRefPtr<CefBrowserView>) override
    {
        return CEF_CTT_NONE;
    }

private:
    IMPLEMENT_REFCOUNTING(StudioBrowserViewDelegate);
    DISALLOW_COPY_AND_ASSIGN(StudioBrowserViewDelegate);
};

/// The native handle (HWND / NSView*, see officelabs::NativeWindowHandle) of
/// the LO document frame that opened the current g_studioWindow, or nullptr
/// if none is open. Read/written only on the CEF UI thread (same thread that
/// owns g_studioWindow itself), so this needs no synchronization of its own.
/// Lets closeStudioWindowForFrame() close the Studio when THAT frame closes,
/// without giving the Studio any broader notion of "the current document".
void* g_pStudioOwnerFrame = nullptr;

/// Command id for the window-scoped Cmd+W (mac) / Ctrl+W (Windows/Linux)
/// accelerator, below. The Studio is a bare CefWindow with no VCL frame of
/// its own -- it is not part of LibreOffice's window/menu-bar model at all,
/// while sharing the SAME NSApplication (and so the same main menu bar, and
/// the same key-equivalent dispatch) as every LO document window. Without a
/// window-scoped claim on Cmd+W, that key combination falls through to
/// LibreOffice's own application-wide Close accelerator (.uno:CloseDoc on the
/// shared menu), which closes whatever LO document frame LO itself currently
/// considers active -- not this window, which the user actually has focused.
constexpr int kCmdCloseStudioWindow = 1;

class StudioWindowDelegate final : public CefWindowDelegate
{
public:
    explicit StudioWindowDelegate(CefRefPtr<CefBrowserView> browserView)
        : m_browserView(browserView)
    {
    }

    void OnWindowCreated(CefRefPtr<CefWindow> window) override
    {
        window->AddChildView(m_browserView);
        window->SetTitle("OfficeLabs Macro Studio");
        window->CenterWindow(CefSize(1400, 900));

        // Claim Cmd+W/Ctrl+W as THIS window's own accelerator, at the highest
        // priority CEF Views offers (high_priority=true -- see
        // CefWindow::SetAccelerator's doc comment: the key event is then
        // handled here before it can reach the web content or CefKeyboardHandler
        // first). That is what stops it from ever becoming an unclaimed key
        // event that the shared application menu bar picks up instead.
        window->SetAccelerator(kCmdCloseStudioWindow, 'W', /*shift_pressed=*/false,
                               /*ctrl_pressed=*/true, /*alt_pressed=*/false,
                               /*high_priority=*/true);

        window->Show();
    }

    void OnWindowDestroyed(CefRefPtr<CefWindow>) override
    {
        g_bWindowDestroyed.store(true, std::memory_order_release);
        g_studioWindow = nullptr;
        m_browserView = nullptr;
    }

    bool CanResize(CefRefPtr<CefWindow>) override { return true; }
    bool CanClose(CefRefPtr<CefWindow>) override { return true; }

    bool OnAccelerator(CefRefPtr<CefWindow> window, int command_id) override
    {
        if (command_id == kCmdCloseStudioWindow)
        {
            // Close THIS window only. Never touches the document -- there is
            // no UNO dispatch, no .uno:CloseDoc, nothing document-shaped here.
            window->Close();
            return true;
        }
        return false;
    }

private:
    CefRefPtr<CefBrowserView> m_browserView;
    IMPLEMENT_REFCOUNTING(StudioWindowDelegate);
    DISALLOW_COPY_AND_ASSIGN(StudioWindowDelegate);
};

void createStudioWindowOnUiThread(void* hOwnerFrame)
{
    // Single instance: focus the existing window rather than opening a second.
    // Deliberately NOT reassigning g_pStudioOwnerFrame here: the Studio stays
    // bound to whichever document frame first opened it, so a second frame
    // merely refocusing it does not adopt it -- see closeStudioWindowForFrame().
    if (g_studioWindow)
    {
        g_studioWindow->Show();
        g_studioWindow->RequestFocus();
        return;
    }

    g_pStudioOwnerFrame = hOwnerFrame;

    const OUString sUrl = studioUrl();
    if (sUrl.isEmpty())
    {
        SAL_WARN("officelabs.cef", "Cannot resolve the Studio URL; not opening");
        return;
    }
    const OString aUtf8 = OUStringToOString(sUrl, RTL_TEXTENCODING_UTF8);

    g_bBrowserClosed.store(false, std::memory_order_release);
    g_bWindowDestroyed.store(false, std::memory_order_release);

    CefBrowserSettings browserSettings;
    CefRefPtr<CefBrowserView> browserView = CefBrowserView::CreateBrowserView(
        new StudioClient(), CefString(aUtf8.getStr()), browserSettings,
        nullptr, nullptr, new StudioBrowserViewDelegate());

    if (!browserView)
    {
        SAL_WARN("officelabs.cef", "CreateBrowserView returned null; Views unavailable");
        return;
    }

    g_studioWindow = CefWindow::CreateTopLevelWindow(new StudioWindowDelegate(browserView));
    if (!g_studioWindow)
        SAL_WARN("officelabs.cef", "CreateTopLevelWindow returned null");
}

class CreateStudioWindowTask final : public CefTask
{
public:
    explicit CreateStudioWindowTask(void* hOwnerFrame) : m_hOwnerFrame(hOwnerFrame) {}

    void Execute() override { createStudioWindowOnUiThread(m_hOwnerFrame); }

private:
    void* m_hOwnerFrame;
    IMPLEMENT_REFCOUNTING(CreateStudioWindowTask);
    DISALLOW_COPY_AND_ASSIGN(CreateStudioWindowTask);
};

class CloseStudioWindowTask final : public CefTask
{
public:
    CloseStudioWindowTask() = default;

    void Execute() override
    {
        if (g_studioWindow)
            g_studioWindow->Close();
    }

private:
    IMPLEMENT_REFCOUNTING(CloseStudioWindowTask);
    DISALLOW_COPY_AND_ASSIGN(CloseStudioWindowTask);
};

/// Closes the Studio only if it is currently owned by hFrame -- see
/// g_pStudioOwnerFrame. Fire-and-forget: unlike closeStudioWindowAndWait(),
/// nothing here needs to block until the close finishes, so this does not
/// touch g_bBrowserClosed/g_bWindowDestroyed at all.
class CloseStudioWindowForFrameTask final : public CefTask
{
public:
    explicit CloseStudioWindowForFrameTask(void* hFrame) : m_hFrame(hFrame) {}

    void Execute() override
    {
        if (g_studioWindow && g_pStudioOwnerFrame == m_hFrame)
            g_studioWindow->Close();
    }

private:
    void* m_hFrame;
    IMPLEMENT_REFCOUNTING(CloseStudioWindowForFrameTask);
    DISALLOW_COPY_AND_ASSIGN(CloseStudioWindowForFrameTask);
};

} // namespace

void openStudioWindow(void* hOwnerFrame)
{
    if (CefCurrentlyOn(TID_UI))
        createStudioWindowOnUiThread(hOwnerFrame);
    else
        CefPostTask(TID_UI, new CreateStudioWindowTask(hOwnerFrame));
}

void closeStudioWindowForFrame(void* hFrame)
{
    if (!hFrame)
        return;

    if (CefCurrentlyOn(TID_UI))
    {
        if (g_studioWindow && g_pStudioOwnerFrame == hFrame)
            g_studioWindow->Close();
    }
    else
        CefPostTask(TID_UI, new CloseStudioWindowForFrameTask(hFrame));
}

void closeStudioWindowAndWait()
{
    if (!g_studioWindow)
        return;

    if (CefCurrentlyOn(TID_UI))
        g_studioWindow->Close();
    else
        CefPostTask(TID_UI, new CloseStudioWindowTask());

    // Bounded: 300 x 10ms = 3s, then give up and log rather than hang the quit.
    for (int i = 0; i < 300; ++i)
    {
        if (g_bBrowserClosed.load(std::memory_order_acquire)
            && g_bWindowDestroyed.load(std::memory_order_acquire))
            break;
#if defined(MACOSX)
        // The CEF UI thread IS this thread here, so the close can only progress
        // if we keep pumping.
        studioWindowPumpNativeEvents(0.01);
#else
        // Windows: multi_threaded_message_loop puts the CEF UI thread elsewhere,
        // so the close progresses on its own and we only have to wait.
        // UNVERIFIED -- no Windows run has ever exercised this path.
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
#endif
    }

    if (!g_bWindowDestroyed.load(std::memory_order_acquire))
        SAL_WARN("officelabs.cef", "Studio window did not close within 3s; continuing shutdown");

    g_studioWindow = nullptr;
}

} // namespace officelabs

#else // !HAVE_FEATURE_CEF

namespace officelabs
{
void openStudioWindow(void*) {}
void closeStudioWindowForFrame(void*) {}
void closeStudioWindowAndWait() {}
}

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
