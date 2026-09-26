/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * OfficeLabs WebView Message Handler
 *
 * Routes JS -> C++ requests from the React UI via cefQuery().
 * Only handles document operations (getDocument, getSelection, applyEdit).
 * Chat/streaming is handled directly by React via HTTP/SSE to the Python agent.
 *
 * THREADING: cefQuery callbacks arrive on the browser-process UI thread.
 *            Document operations MUST be dispatched to the VCL main thread
 *            via Application::PostUserEvent().
 *            m_pPanel is atomic — read on the CEF browser UI thread, written
 *            on the VCL thread.
 */

#ifdef HAVE_FEATURE_CEF

#include <officelabs/WebViewMessageHandler.hxx>
#include <officelabs/TrustedUrl.hxx>
#include <officelabs/WebViewPanel.hxx>
#include <officelabs/DocumentController.hxx>
#include <officelabs/AgentIdentity.hxx>
#include <officelabs/ConsentBridge.hxx>

#include <vcl/officelabstheme.hxx>

#include <sal/log.hxx>
#include <vcl/svapp.hxx>
#include <tools/link.hxx>

#include <com/sun/star/task/OfficeRestartManager.hpp>
#include <com/sun/star/task/XInteractionHandler.hpp>
#include <com/sun/star/uno/Exception.hpp>
#include <com/sun/star/system/SystemShellExecute.hpp>
#include <com/sun/star/system/SystemShellExecuteFlags.hpp>
#include <com/sun/star/system/XSystemShellExecute.hpp>
#include <comphelper/processfactory.hxx>

#include <functional>

namespace {

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

namespace officelabs {

namespace {

// Simple JSON string escaping (for building response JSON)
std::string escapeJson(const OUString& s)
{
    OString utf8 = OUStringToOString(s, RTL_TEXTENCODING_UTF8);
    std::string result;
    result.reserve(utf8.getLength() + 16);
    for (sal_Int32 i = 0; i < utf8.getLength(); ++i)
    {
        char c = utf8[i];
        switch (c)
        {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n";  break;
            case '\r': result += "\\r";  break;
            case '\t': result += "\\t";  break;
            default:   result += c;      break;
        }
    }
    return result;
}

// Extract a string value from JSON by key (simple, no nesting)
std::string extractJsonString(const std::string& json, const std::string& key)
{
    std::string searchKey = "\"" + key + "\"";
    auto pos = json.find(searchKey);
    if (pos == std::string::npos)
        return "";

    pos = json.find(':', pos + searchKey.length());
    if (pos == std::string::npos)
        return "";

    pos++;
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t'))
        pos++;

    if (pos >= json.length() || json[pos] != '"')
        return "";

    pos++; // skip opening quote
    std::string value;
    while (pos < json.length() && json[pos] != '"')
    {
        if (json[pos] == '\\' && pos + 1 < json.length())
        {
            pos++;
            switch (json[pos])
            {
                case 'n':  value += '\n'; break;
                case 'r':  value += '\r'; break;
                case 't':  value += '\t'; break;
                case '"':  value += '"';  break;
                case '\\': value += '\\'; break;
                default:   value += json[pos]; break;
            }
        }
        else
        {
            value += json[pos];
        }
        pos++;
    }
    return value;
}

} // anonymous namespace

std::string buildActiveThemeJson(const std::string& themeName)
{
    // themeName comes from GetOLThemeSource(), not page script, but it can
    // trace back to a file on disk (theme.txt / officelabs_theme.txt) --
    // escape it the same way the other handlers escape untrusted strings.
    std::string escaped;
    escaped.reserve(themeName.size() + 16);
    for (char c : themeName)
    {
        switch (c)
        {
            case '"':  escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\n': escaped += "\\n";  break;
            case '\r': escaped += "\\r";  break;
            case '\t': escaped += "\\t";  break;
            default:   escaped += c;      break;
        }
    }
    return "{\"theme\":\"" + escaped + "\"}";
}

// A device-grant verification URL is short (host + a couple of path
// segments + a user_code query param -- well under 200 characters in
// practice). 2048 is the classic "URL length" ceiling browsers and Windows
// shell tooling have historically enforced in practice (e.g. IE/old Edge's
// documented limit), so anything under it is generously long for this
// feature and anything over it has no legitimate reason to reach
// SystemShellExecute. Keep this constant identical on the TS side
// (cefBridge.ts isOpenableExternalUrl) -- the two validators must stay in
// lockstep, character for character.
constexpr sal_Int32 kMaxOpenableExternalUrlLength = 2048;

bool isOpenableExternalUrl(const OUString& rUrl)
{
    // Opened via the OS's own URL handler (SystemShellExecute, URIS_ONLY),
    // never a shell -- but a page that reaches this handler is untrusted
    // content, and this is the only check before that handoff. https only:
    // file://, javascript:, data:, mailto:, a bare path, or anything else
    // either escapes "show the user a page" or hands a native handler
    // attacker-chosen input.
    if (!rUrl.startsWith("https://"))
        return false;

    if (rUrl.getLength() > kMaxOpenableExternalUrlLength)
        return false;

    for (sal_Int32 i = 0; i < rUrl.getLength(); ++i)
    {
        const sal_Unicode c = rUrl[i];
        // Control characters, whitespace, and URI/shell metacharacters have
        // no place in a well-formed https URL -- reject rather than try to
        // enumerate every unsafe scheme or encoding trick.
        if (c <= 0x20 || c == 0x7f
            || c == '`' || c == ';' || c == '|' || c == '&' || c == '$'
            || c == '<' || c == '>' || c == '"' || c == '\'' || c == '\\')
            return false;
    }

    return true;
}

WebViewMessageHandler::WebViewMessageHandler(WebViewPanel* pPanel)
    : m_pPanel(pPanel)
{
}

void WebViewMessageHandler::setPanel(WebViewPanel* pPanel)
{
    m_pPanel.store(pPanel, std::memory_order_release);
}

bool WebViewMessageHandler::OnQuery(
    CefRefPtr<CefBrowser> /*browser*/,
    CefRefPtr<CefFrame> frame,
    int64_t /*query_id*/,
    const CefString& request,
    bool /*persistent*/,
    CefRefPtr<Callback> callback)
{
    // WHO is asking, before WHAT they asked. The renderer injects cefQuery into
    // every page it loads, and these handlers answer whoever calls them --
    // getSessionToken hands over the credential for the entire agent API, and
    // requestConsent raises a native dialog the user is expected to trust.
    // Neither may be reachable from a page that is not our own UI.
    //
    // Checked here as well as in OnBeforeBrowse deliberately. Navigation
    // confinement is the first line; this is the one that still holds if a page
    // arrives by a route that never passes through OnBeforeBrowse.
    const OUString sFrameUrl
        = frame ? OUString::fromUtf8(frame->GetURL().ToString().c_str()) : OUString();
    if (!officelabs::isTrustedUiUrl(sFrameUrl))
    {
        SAL_WARN("officelabs.cef", "Refused a cefQuery from an untrusted frame");
        callback->Failure(403, "cefQuery is not available to this page");
        return true;
    }

    std::string req = request.ToString();
    SAL_INFO("officelabs.cef", "cefQuery received: " << req.substr(0, 100));

    // Route based on "type" field in JSON
    if (req.find("\"type\":\"getDocument\"") != std::string::npos
        || req.find("\"type\": \"getDocument\"") != std::string::npos)
    {
        handleGetDocument(callback);
        return true;
    }

    if (req.find("\"type\":\"getSelection\"") != std::string::npos
        || req.find("\"type\": \"getSelection\"") != std::string::npos)
    {
        handleGetSelection(callback);
        return true;
    }

    if (req.find("\"type\":\"applyEdit\"") != std::string::npos
        || req.find("\"type\": \"applyEdit\"") != std::string::npos)
    {
        handleApplyEdit(req, callback);
        return true;
    }

    if (req.find("\"type\":\"getAppType\"") != std::string::npos
        || req.find("\"type\": \"getAppType\"") != std::string::npos)
    {
        handleGetAppType(callback);
        return true;
    }

    if (req.find("\"type\":\"getDocumentUrl\"") != std::string::npos
        || req.find("\"type\": \"getDocumentUrl\"") != std::string::npos)
    {
        handleGetDocumentUrl(callback);
        return true;
    }

    if (req.find("\"type\":\"getSessionToken\"") != std::string::npos
        || req.find("\"type\": \"getSessionToken\"") != std::string::npos)
    {
        handleGetSessionToken(callback);
        return true;
    }

    if (req.find("\"type\":\"requestConsent\"") != std::string::npos
        || req.find("\"type\": \"requestConsent\"") != std::string::npos)
    {
        handleRequestConsent(req, callback);
        return true;
    }

    if (req.find("\"type\":\"requestOfficeRestart\"") != std::string::npos
        || req.find("\"type\": \"requestOfficeRestart\"") != std::string::npos)
    {
        handleRequestOfficeRestart(callback);
        return true;
    }

    if (req.find("\"type\":\"getActiveTheme\"") != std::string::npos
        || req.find("\"type\": \"getActiveTheme\"") != std::string::npos)
    {
        handleGetActiveTheme(callback);
        return true;
    }

    if (req.find("\"type\":\"openExternalUrl\"") != std::string::npos
        || req.find("\"type\": \"openExternalUrl\"") != std::string::npos)
    {
        handleOpenExternalUrl(req, callback);
        return true;
    }

    // Unknown request type
    SAL_WARN("officelabs.cef", "Unknown cefQuery type: " << req.substr(0, 50));
    callback->Failure(404, "Unknown request type");
    return true;
}

void WebViewMessageHandler::OnQueryCanceled(
    CefRefPtr<CefBrowser> /*browser*/,
    CefRefPtr<CefFrame> /*frame*/,
    int64_t /*query_id*/)
{
    SAL_INFO("officelabs.cef", "cefQuery canceled");
}

// P0b wave B/C seam. The agent requires X-OfficeLabs-Session on every route
// but /, /health and the docs surface; the WebView cannot read the 0600 token
// file itself, so the native host reads it and hands it over.
//
// No VCL dispatch: this touches no document and no panel, so it answers
// inline on the thread OnQuery arrives on -- the browser-process UI thread --
// rather than queueing behind the VCL main thread. That matters because the
// sidebar blocks on this before its first agent call. It is a bounded read of
// a <=256-byte local file, which is why doing it inline is acceptable; a
// larger or remote read here would stall that thread and must not be added.
//
// This does not make the token secret from page script. It cannot: whatever
// the page can request, the page holds. It removes arbitrary local processes
// and arbitrary web pages from the caller set, and nothing more. Consent for
// macro execution is signed with install.secret, which is never handed over.
void WebViewMessageHandler::handleGetSessionToken(CefRefPtr<Callback> callback)
{
    const OString sToken = readSessionToken();
    if (sToken.isEmpty())
    {
        // Missing, unreadable, oversized or malformed all land here. The agent
        // mints this at startup, so the usual cause is that it is not up yet.
        // Failing is right either way: the UI retries, whereas handing back an
        // empty token would look like success and then 401.
        callback->Failure(503, "session token unavailable");
        return;
    }

    callback->Success("{\"token\":\""
                      + escapeJson(OStringToOUString(sToken, RTL_TEXTENCODING_UTF8)) + "\"}");
}

// P0b / D9. The page may ask for the dialog; it may not answer it.
//
// What the user is shown is fetched from the agent by the native side, against
// an install-secret proof the page has never seen. The macro is agent-resolved
// and the grant is keyed on the agent's own digest, so a page cannot show one
// macro and run another. Capabilities are page-proposed but allowlisted, and
// consume_grant refuses a run needing more than the grant carries -- the prompt
// can overstate, never understate.
//
// It does NOT cover document_identity: the agent copies that straight from the
// page's own challenge request and never verifies it. It is therefore not
// displayed at all (see describeRequest); showing it would put attacker-chosen
// text on the line a user reads to decide whether to trust this.
//
// The approval is signed here and posted to the agent directly; the page learns
// only the resulting one-shot grant id, which it could not have produced.
void WebViewMessageHandler::handleRequestConsent(const std::string& json,
                                                 CefRefPtr<Callback> callback)
{
    const std::string sChallengeId = extractJsonString(json, "challengeId");
    if (sChallengeId.empty())
    {
        callback->Failure(400, "challengeId required");
        return;
    }

    CefRefPtr<Callback> cb = callback;
    requestConsentAsync(OString(sChallengeId.c_str(), sChallengeId.size()),
                        [cb](ConsentOutcome aOutcome) {
                            if (!aOutcome.bGranted)
                            {
                                cb->Failure(403, aOutcome.sError.isEmpty()
                                                     ? "consent denied"
                                                     : aOutcome.sError.getStr());
                                return;
                            }
                            const OUString sId
                                = OStringToOUString(aOutcome.sConsentId, RTL_TEXTENCODING_UTF8);
                            cb->Success("{\"consentId\":\"" + escapeJson(sId) + "\"}");
                        });
}

void WebViewMessageHandler::handleGetDocument(CefRefPtr<Callback> callback)
{
    WebViewPanel* panel = m_pPanel.load(std::memory_order_acquire);
    if (!panel)
    {
        callback->Failure(500, "Panel not available");
        return;
    }

    CefRefPtr<Callback> cb = callback;

    // Re-read panel pointer inside VCL lambda (panel may have been
    // swapped between the CEF browser-UI-thread check and VCL dispatch).
    postToVclThread([this, cb]() {
        WebViewPanel* p = m_pPanel.load(std::memory_order_acquire);
        if (!p)
        {
            cb->Failure(500, "Panel destroyed during dispatch");
            return;
        }

        // Always refresh document reference (handles document switches)
        p->detectDocument();

        DocumentController* dc = p->getDocController();
        if (!dc || !dc->hasDocument())
        {
            cb->Success("{\"text\":\"\"}");
            return;
        }

        OUString text = dc->getDocumentText();
        std::string escaped = escapeJson(text);
        std::string response = "{\"text\":\"" + escaped + "\"}";
        cb->Success(response);
    });
}

void WebViewMessageHandler::handleGetSelection(CefRefPtr<Callback> callback)
{
    WebViewPanel* panel = m_pPanel.load(std::memory_order_acquire);
    if (!panel)
    {
        callback->Failure(500, "Panel not available");
        return;
    }

    CefRefPtr<Callback> cb = callback;

    postToVclThread([this, cb]() {
        WebViewPanel* p = m_pPanel.load(std::memory_order_acquire);
        if (!p)
        {
            cb->Failure(500, "Panel destroyed during dispatch");
            return;
        }

        p->detectDocument();

        DocumentController* dc = p->getDocController();
        if (!dc || !dc->hasDocument())
        {
            cb->Success("{\"selection\":\"\"}");
            return;
        }

        OUString selection = dc->getSelectedText();
        std::string escaped = escapeJson(selection);
        std::string response = "{\"selection\":\"" + escaped + "\"}";
        cb->Success(response);
    });
}

void WebViewMessageHandler::handleApplyEdit(
    const std::string& json, CefRefPtr<Callback> callback)
{
    WebViewPanel* panel = m_pPanel.load(std::memory_order_acquire);
    if (!panel)
    {
        callback->Failure(500, "Panel not available");
        return;
    }

    std::string editId = extractJsonString(json, "editId");
    std::string action = extractJsonString(json, "action");

    SAL_INFO("officelabs.cef", "applyEdit: id=" << editId << " action=" << action);

    CefRefPtr<Callback> cb = callback;

    postToVclThread([this, cb, editId, action]() {
        WebViewPanel* p = m_pPanel.load(std::memory_order_acquire);
        if (!p)
        {
            cb->Failure(500, "Panel destroyed during dispatch");
            return;
        }

        p->detectDocument();

        DocumentController* dc = p->getDocController();
        if (!dc || !dc->hasDocument())
        {
            cb->Failure(400, "No document open");
            return;
        }

        if (action == "approve")
        {
            cb->Success("{\"status\":\"approved\"}");
        }
        else if (action == "reject")
        {
            cb->Success("{\"status\":\"rejected\"}");
        }
        else
        {
            cb->Failure(400, "Unknown action");
        }
    });
}

void WebViewMessageHandler::handleGetAppType(CefRefPtr<Callback> callback)
{
    WebViewPanel* panel = m_pPanel.load(std::memory_order_acquire);
    if (!panel)
    {
        callback->Success("{\"appType\":\"writer\"}");
        return;
    }

    CefRefPtr<Callback> cb = callback;

    postToVclThread([this, cb]() {
        WebViewPanel* p = m_pPanel.load(std::memory_order_acquire);
        if (!p)
        {
            cb->Success("{\"appType\":\"writer\"}");
            return;
        }

        p->detectDocument();

        DocumentController* dc = p->getDocController();
        OUString appType = dc ? dc->getApplicationType() : u"writer"_ustr;

        OString utf8AppType = OUStringToOString(appType, RTL_TEXTENCODING_UTF8);
        std::string response = "{\"appType\":\"" + std::string(utf8AppType.getStr()) + "\"}";
        cb->Success(response);
    });
}

void WebViewMessageHandler::handleGetDocumentUrl(CefRefPtr<Callback> callback)
{
    WebViewPanel* panel = m_pPanel.load(std::memory_order_acquire);
    if (!panel)
    {
        callback->Success("{\"url\":\"\"}");
        return;
    }

    CefRefPtr<Callback> cb = callback;

    postToVclThread([this, cb]() {
        WebViewPanel* p = m_pPanel.load(std::memory_order_acquire);
        if (!p)
        {
            cb->Success("{\"url\":\"\"}");
            return;
        }

        p->detectDocument();

        DocumentController* dc = p->getDocController();
        OUString url = dc ? dc->getDocumentUrl() : OUString();

        std::string escaped = escapeJson(url);
        std::string response = "{\"url\":\"" + escaped + "\"}";
        cb->Success(response);
    });
}

// A theme change only takes effect after a restart, so the sidebar asks the
// office to restart itself; the agent never kills the process. requestRestart
// is a process-wide operation, not a panel one -- unlike the document handlers
// it deliberately ignores m_pPanel and works while the panel is gone.
void WebViewMessageHandler::handleRequestOfficeRestart(CefRefPtr<Callback> callback)
{
    CefRefPtr<Callback> cb = callback;

    postToVclThread([cb]() {
        try
        {
            css::task::OfficeRestartManager::get(comphelper::getProcessComponentContext())
                ->requestRestart(css::uno::Reference<css::task::XInteractionHandler>());
            cb->Success("{\"success\":true}");
        }
        catch (const css::uno::Exception&)
        {
            SAL_WARN("officelabs.cef", "requestOfficeRestart: restart request failed");
            cb->Failure(500, "restart request failed");
        }
    });
}

// The sidebar's mount-time theme must match what LibreOffice actually resolved
// at process start, not the agent's saved-but-possibly-pending theme.txt --
// see WebViewMessageHandler.hxx. GetOLThemeSource() resolves once (static
// local) and is cheap thereafter, so this answers inline like
// handleGetSessionToken rather than dispatching to the VCL thread.
void WebViewMessageHandler::handleGetActiveTheme(CefRefPtr<Callback> callback)
{
    const std::string sTheme = vcl::officelabs::GetOLThemeSource().name;
    callback->Success(buildActiveThemeJson(sTheme));
}

// core#117 / core#112. The device authorization grant needs the user to
// approve a code on the verification page in a REAL browser -- CEF's own
// window is not acceptable, since it is exactly the surface the sidebar
// renders remote/untrusted content into. isOpenableExternalUrl() is the
// only gate; SystemShellExecute + URIS_ONLY is the same cross-platform
// mechanism LibreOffice's own hyperlink control uses
// (vcl/source/control/fixedhyper.cxx), never a shell.
//
// Like requestOfficeRestart this touches no document and no panel, so it is
// dispatched to the VCL thread purely to keep UNO component-context calls
// off the CEF browser-process UI thread, not because it needs the panel.
void WebViewMessageHandler::handleOpenExternalUrl(const std::string& json,
                                                   CefRefPtr<Callback> callback)
{
    const std::string sRawUrl = extractJsonString(json, "url");
    const OUString sUrl
        = OStringToOUString(OString(sRawUrl.c_str(), sRawUrl.size()), RTL_TEXTENCODING_UTF8);

    if (!isOpenableExternalUrl(sUrl))
    {
        SAL_WARN("officelabs.cef", "openExternalUrl: refused a non-https or malformed URL");
        callback->Failure(400, "url must be a well-formed https:// URL");
        return;
    }

    CefRefPtr<Callback> cb = callback;

    postToVclThread([sUrl, cb]() {
        try
        {
            css::uno::Reference<css::system::XSystemShellExecute> xSystemShellExecute(
                css::system::SystemShellExecute::create(comphelper::getProcessComponentContext()));
            xSystemShellExecute->execute(sUrl, OUString(),
                                         css::system::SystemShellExecuteFlags::URIS_ONLY);
            cb->Success("{\"success\":true}");
        }
        catch (const css::uno::Exception&)
        {
            SAL_WARN("officelabs.cef", "openExternalUrl: SystemShellExecute failed");
            cb->Failure(500, "failed to open URL");
        }
    });
}

} // namespace officelabs

#endif // HAVE_FEATURE_CEF

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
