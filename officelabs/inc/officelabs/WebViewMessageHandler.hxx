/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * OfficeLabs WebView Message Handler
 *
 * Handles JS -> C++ messages via cefQuery().
 * Only handles document operations - chat is direct HTTP/SSE to agent.
 *
 * Request types:
 *   getDocument  - Read document text via DocumentController
 *   getSelection - Get current selection
 *   applyEdit    - Apply/reject a pending edit
 *   getAppType   - Get current application type ("writer", "calc", "impress")
 *   getSessionToken - Hand the WebView the agent's session token (P0b)
 *   requestConsent  - Run the native consent dialog for a challenge (P0b, D9)
 *   requestOfficeRestart - Ask LibreOffice to restart itself (theme applies
 *                          after a restart)
 *   getActiveTheme  - The theme this process actually resolved at startup
 *                      (include/vcl/officelabstheme.hxx), as opposed to the
 *                      agent's saved-but-possibly-pending theme.txt.
 *
 * THREADING: m_pPanel is std::atomic because it's read on the CEF IO
 *            thread (OnQuery) and written on the VCL thread (setPanel).
 */

#ifndef INCLUDED_OFFICELABS_WEBVIEWMESSAGEHANDLER_HXX
#define INCLUDED_OFFICELABS_WEBVIEWMESSAGEHANDLER_HXX

#ifdef HAVE_FEATURE_CEF

#include <officelabs/officelabsdllapi.h>

#ifdef _WIN32
#include <prewin.h>
#include <windows.h>
#include <postwin.h>
#endif

#include <include/wrapper/cef_message_router.h>
#include <string>
#include <atomic>

namespace officelabs {

/// Pure JSON serialization of the getActiveTheme response, split out from
/// handleGetActiveTheme so it is unit-testable without a CefBrowser/CefFrame.
OFFICELABS_DLLPUBLIC std::string buildActiveThemeJson(const std::string& themeName);

class WebViewPanel;

class WebViewMessageHandler final : public CefMessageRouterBrowserSide::Handler
{
public:
    explicit WebViewMessageHandler(WebViewPanel* pPanel);

    /// Update the panel pointer (called when panel is destroyed/recreated).
    /// Thread-safe: uses atomic store.
    void setPanel(WebViewPanel* pPanel);

    bool OnQuery(CefRefPtr<CefBrowser> browser,
                 CefRefPtr<CefFrame> frame,
                 int64_t query_id,
                 const CefString& request,
                 bool persistent,
                 CefRefPtr<Callback> callback) override;

    void OnQueryCanceled(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefFrame> frame,
                         int64_t query_id) override;

private:
    void handleGetDocument(CefRefPtr<Callback> callback);
    void handleGetSelection(CefRefPtr<Callback> callback);
    void handleApplyEdit(const std::string& json,
                         CefRefPtr<Callback> callback);
    void handleGetAppType(CefRefPtr<Callback> callback);
    void handleGetDocumentUrl(CefRefPtr<Callback> callback);
    void handleGetSessionToken(CefRefPtr<Callback> callback);
    void handleRequestConsent(const std::string& json, CefRefPtr<Callback> callback);
    void handleRequestOfficeRestart(CefRefPtr<Callback> callback);
    void handleGetActiveTheme(CefRefPtr<Callback> callback);

    std::atomic<WebViewPanel*> m_pPanel;
};

} // namespace officelabs

#endif // HAVE_FEATURE_CEF
#endif // INCLUDED_OFFICELABS_WEBVIEWMESSAGEHANDLER_HXX

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
