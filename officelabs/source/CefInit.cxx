/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * OfficeLabs CEF Initialization
 *
 * Message-loop mode is platform-dependent so CEF never conflicts with
 * LibreOffice's VCL event loop:
 *   Windows: multi_threaded_message_loop = true, external_message_pump = false
 *            (CEF owns its own UI thread; not available on macOS).
 *   macOS:   multi_threaded_message_loop = false, external_message_pump = true
 *            (LO owns the AppKit run loop; CefDoMessageLoopWork() is pumped by
 *            OfficelabsBrowserApp::OnScheduleMessagePumpWork()).
 */

#ifdef HAVE_FEATURE_CEF

#ifdef _WIN32
#include <prewin.h>
#include <windows.h>
#include <postwin.h>
#endif

#include <officelabs/CefDebugPort.hxx>
#include <officelabs/CefInit.hxx>
#include <officelabs/WebViewPanel.hxx>
#include <officelabs/StudioWindow.hxx>

#include <include/cef_app.h>
#include <include/cef_browser.h>

#include <sal/log.hxx>
#include <vcl/svapp.hxx>
#include <osl/file.hxx>
#include <osl/module.hxx>
#include <rtl/bootstrap.hxx>

#include <com/sun/star/frame/Desktop.hpp>
#include <com/sun/star/frame/XDesktop2.hpp>
#include <com/sun/star/frame/XTerminateListener.hpp>
#include <com/sun/star/lang/EventObject.hpp>
#include <com/sun/star/uno/Reference.hxx>
#include <com/sun/star/uno/XComponentContext.hpp>
#include <comphelper/processfactory.hxx>
#include <cppuhelper/implbase.hxx>

#include <cstdlib>
#include <string>

#include <officelabs/OfficelabsBrowserApp.hxx>

#ifdef MACOSX
#include <include/wrapper/cef_library_loader.h>
#include <optional>
#include <sys/stat.h>
#include <pthread.h>
#include <dlfcn.h>
#include <cstdio>
#endif


namespace {

#ifdef MACOSX
// Process-lifetime CEF framework loader. On macOS the framework is dlopen'd at
// runtime (a sandbox requirement) and the loader must outlive every CEF call,
// so it is held in this file-static optional and never reset once loaded.
std::optional<CefScopedLibraryLoader> g_oLibraryLoader;
#endif

// Drives CefShutdown() on the main thread during normal LibreOffice
// termination. CEF is initialized with multi_threaded_message_loop = true,
// so CefShutdown() must run on the thread that called CefInitialize, while
// the process is still alive. Leaving it to the CefInit static destructor
// runs it during CRT teardown after main() returns, racing CEF's worker
// threads and firing the libcef int3 (BREAKPOINT_80000003) on close. This
// listener fires synchronously on the main thread from Desktop::terminate(),
// before static destruction. The static-dtor shutdown() stays as an
// idempotent fallback (guarded by m_bInitialized).
class CefTerminateListener
    : public cppu::WeakImplHelper<css::frame::XTerminateListener>
{
public:
    // XTerminateListener — never veto termination
    void SAL_CALL queryTermination(const css::lang::EventObject&) override {}

    void SAL_CALL notifyTermination(const css::lang::EventObject&) override
    {
        officelabs::CefInit::instance().shutdown();
    }

    // XEventListener
    void SAL_CALL disposing(const css::lang::EventObject&) override {}
};

} // anonymous namespace

namespace officelabs {

CefInit& CefInit::instance()
{
    static CefInit sInstance;
    return sInstance;
}

CefInit::CefInit()
    : m_bInitialized(false)
{
}

CefInit::~CefInit()
{
    shutdown();
}

bool CefInit::initialize()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_bInitialized)
        return true;

    // Headless runs (--headless, unit tests) have no real native frames: on Windows the
    // headless frame handle is not an HWND, and CEF aborts creating a child of it.
    if (Application::IsHeadlessModeEnabled())
    {
        SAL_INFO("officelabs.cef", "headless mode: CEF not started");
        return false;
    }

#ifdef MACOSX
    // Load the CEF framework at runtime before any other CEF call. The loader
    // has process lifetime (see g_oLibraryLoader).
    if (!g_oLibraryLoader)
    {
        g_oLibraryLoader.emplace();
        if (!g_oLibraryLoader->LoadInMain())
        {
            SAL_WARN("officelabs.cef",
                     "CefScopedLibraryLoader::LoadInMain() FAILED");
            g_oLibraryLoader.reset();
            return false;
        }
        SAL_INFO("officelabs.cef", "CEF framework loaded (LoadInMain)");
    }
#endif

#ifdef _WIN32
    CefMainArgs main_args(GetModuleHandle(nullptr));
#else
    CefMainArgs main_args(0, nullptr);
#endif

    CefSettings settings;

    // Message-loop mode is platform-dependent (see file header).
#ifdef MACOSX
    // macOS has no multi_threaded_message_loop. LibreOffice owns the AppKit
    // run loop, so use external_message_pump and drive CefDoMessageLoopWork()
    // from OfficelabsBrowserApp::OnScheduleMessagePumpWork().
    settings.multi_threaded_message_loop = false;
    settings.external_message_pump = true;
#else
    // Windows: CEF owns its own UI thread so it doesn't block VCL.
    settings.multi_threaded_message_loop = true;
    settings.external_message_pump = false;
#endif

    // No sandbox - LibreOffice doesn't support CEF's sandbox model
    settings.no_sandbox = true;

    // DevTools/CDP only when a developer opts in: an open port lets any local
    // process script the sidebar and its document bridge (see CefDebugPort.hxx).
    const int nDebugPort = parseRemoteDebuggingPort(std::getenv(CEF_DEBUG_PORT_ENV));
    settings.remote_debugging_port = nDebugPort;

    // Disable windowless rendering - we use a real HWND
    settings.windowless_rendering_enabled = false;

    // Set subprocess path
    OUString subprocessPath = getSubprocessPath();
    OString utf8Path = OUStringToOString(subprocessPath, RTL_TEXTENCODING_UTF8);
    CefString(&settings.browser_subprocess_path).FromASCII(utf8Path.getStr());

#ifdef MACOSX
    {
        struct stat sSubprocessStat;
        bool bSubprocessExists = (::stat(utf8Path.getStr(), &sSubprocessStat) == 0);
    }
#endif

    // Persistent cache — enables localStorage across CEF browser restarts.
    // Without this, CEF runs in "incognito mode" and localStorage is lost
    // when the sidebar panel is recreated (e.g. during OLE chart activation).
#ifdef _WIN32
    {
        const char* localAppData = std::getenv("LOCALAPPDATA");
        if (localAppData)
        {
            std::string rootCache = std::string(localAppData) + "\\OfficeLabs\\cef_data";
            std::string profileCache = rootCache + "\\Default";
            CefString(&settings.root_cache_path).FromASCII(rootCache.c_str());
            CefString(&settings.cache_path).FromASCII(profileCache.c_str());
            SAL_INFO("officelabs.cef", "CEF cache: " << rootCache);
        }
    }
#elif defined(MACOSX)
    {
        // macOS stores per-user app data under
        // ~/Library/Application Support/OfficeLabs/ (Apple convention, NOT the
        // app bundle's own Contents/). CEF will not create missing parent
        // directories, and a missing cache_path forces incognito mode and loses
        // localStorage on the first run, so create the dirs up front.
        const char* home = std::getenv("HOME");
        if (home)
        {
            std::string rootCache =
                std::string(home) + "/Library/Application Support/OfficeLabs/cef_data";
            std::string profileCache = rootCache + "/Default";

            auto mkdirp = [](const std::string& path) {
                std::size_t pos = 0;
                while ((pos = path.find('/', pos + 1)) != std::string::npos)
                    ::mkdir(path.substr(0, pos).c_str(), 0700);
                ::mkdir(path.c_str(), 0700); // final component (EEXIST is fine)
            };
            mkdirp(rootCache);
            mkdirp(profileCache);

            CefString(&settings.root_cache_path).FromASCII(rootCache.c_str());
            CefString(&settings.cache_path).FromASCII(profileCache.c_str());
            SAL_INFO("officelabs.cef", "CEF cache: " << rootCache);
        }
        else
        {
            SAL_WARN("officelabs.cef", "HOME not set - CEF cache left at default");
        }
    }
#endif

    // Log settings
    // officelabs.cef Phase-1 instrumentation: the previous relative path
    // was silently unwritable (GUI app bundles run with cwd "/"), so
    // Chromium's own init log - the best explainer of init failures - was
    // never produced. Use an absolute path and verbose severity instead.
    settings.log_severity = LOGSEVERITY_VERBOSE;
    CefString(&settings.log_file).FromASCII("/tmp/officelabs_cef_debug.log");

    SAL_INFO("officelabs.cef", "Initializing CEF with subprocess: " << utf8Path);

    // Browser-process app. On macOS this provides the external message-pump
    // integration (OnScheduleMessagePumpWork). On Windows CEF runs its own UI
    // thread, so no browser app is needed and the pointer stays null.
    CefRefPtr<CefApp> browserApp;
#ifdef MACOSX
    browserApp = new OfficelabsBrowserApp();
#endif

    bool bCefInitOk = CefInitialize(main_args, settings, browserApp, nullptr);
    if (!bCefInitOk)
    {
        SAL_WARN("officelabs.cef", "CefInitialize() FAILED");
        return false;
    }

    m_bInitialized = true;
    SAL_INFO("officelabs.cef", "CEF initialized successfully (debug port " << nDebugPort << ")");

#ifdef MACOSX
    // Drive CEF with a 30Hz heartbeat. The pure on-demand external pump
    // (OnScheduleMessagePumpWork) proved unreliable under VCL's nested run
    // loop; see OfficelabsBrowserApp.cxx / MessagePumpMac.mm.
    StartCefPumpHeartbeat();
#endif

    // Register a terminate listener so CefShutdown() runs on the main thread
    // during normal LO termination, before static destructors (see comment on
    // CefTerminateListener). Runs once — initialize() early-returns when
    // already initialized.
    try
    {
        css::uno::Reference<css::uno::XComponentContext> xContext(
            ::comphelper::getProcessComponentContext());
        if (xContext.is())
        {
            css::uno::Reference<css::frame::XDesktop2> xDesktop(
                css::frame::Desktop::create(xContext));
            xDesktop->addTerminateListener(new CefTerminateListener());
            SAL_INFO("officelabs.cef", "Registered CEF terminate listener");
        }
    }
    catch (const css::uno::Exception&)
    {
        // Non-fatal: fall back to the static-destructor shutdown() path.
        SAL_WARN("officelabs.cef",
                 "Failed to register CEF terminate listener; CefShutdown will "
                 "fall back to static destructor");
    }

    return true;
}

void CefInit::shutdown()
{
    // Re-entrancy check BEFORE the lock, because the lock is what would hang.
    // A nested shutdown is a no-op: the outer call is already doing the work,
    // and there is exactly one CEF to shut down.
    bool bExpected = false;
    if (!m_bShuttingDown.compare_exchange_strong(bExpected, true))
        return;

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_bInitialized)
        return;

    SAL_INFO("officelabs.cef", "Shutting down CEF...");

    // The Studio window owns a browser CefShutdown() knows nothing about, and
    // CEF requires every browser closed first. Do it while the message pump is
    // still running, so the close can actually complete.
    officelabs::closeStudioWindowAndWait();

    // Release persistent browser/popup/router BEFORE CefShutdown().
    // Static CefRefPtrs in WebViewPanel.cxx must be cleared while CEF
    // is still alive, otherwise their destructors touch freed CEF state.
    WebViewPanel::cleanupPersistentBrowser();

#ifdef MACOSX
    // Stop the heartbeat BEFORE CefShutdown so no CefDoMessageLoopWork can
    // fire against a torn-down CEF.
    StopCefPumpHeartbeat();
#endif
    CefShutdown();
    m_bInitialized = false;
    SAL_INFO("officelabs.cef", "CEF shutdown complete");
}

OUString CefInit::getSubprocessPath() const
{
    // The subprocess exe lives next to soffice.exe in instdir/program/
    OUString sInstDir(u"$BRAND_BASE_DIR/$BRAND_SHARE_SUBDIR/.."_ustr);
    rtl::Bootstrap::expandMacros(sInstDir);

    OUString sFileUrl;
    osl::FileBase::getAbsoluteFileURL(OUString(), sInstDir, sFileUrl);

    // Convert file:// URL to system path
    OUString sSystemPath;
    osl::FileBase::getSystemPathFromFileURL(sFileUrl, sSystemPath);

#ifdef _WIN32
    return sSystemPath + "\\program\\officelabs_cef_subprocess.exe";
#elif defined(MACOSX)
    // On macOS CEF launches a separate Helper .app bundle, not a bare
    // executable. sSystemPath resolves to "<App>.app/Contents" (BRAND_BASE_DIR
    // is .../Contents and BRAND_SHARE_SUBDIR is Resources, so
    // "$BRAND_BASE_DIR/$BRAND_SHARE_SUBDIR/.." == .../Contents). CEF derives the
    // (GPU)/(Renderer)/(Plugin)/(Alerts) sibling bundles from this main-helper
    // path by suffix. This name MUST match the bundle produced by
    // CustomTarget_cef_mac_bundle.mk (see officelabs/mac/helper-plists).
    return sSystemPath
         + "/Frameworks/OfficeLabs Helper.app/Contents/MacOS/OfficeLabs Helper";
#else
    return sSystemPath + "/program/officelabs_cef_subprocess";
#endif
}

} // namespace officelabs

#endif // HAVE_FEATURE_CEF

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
