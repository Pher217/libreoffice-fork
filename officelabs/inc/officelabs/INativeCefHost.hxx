/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * OfficeLabs INativeCefHost — platform isolation for the CEF sidebar host
 * window/view (mac port, Decision 5 of
 * ~/cef-spike/drafts/MAC_PORT_DESIGN.md).
 *
 * Every native ("Win32" on Windows, Cocoa on macOS) window operation that
 * WebViewPanel needs in order to embed a CEF browser inside the LO sidebar
 * is isolated behind this interface. WebViewPanel.cxx talks only to
 * INativeCefHost -- it never references HWND, NSView, or any other native
 * type directly.
 *
 *   Windows impl: WebViewPanelHostWin.cxx (today's Win32 code, moved
 *                 verbatim out of WebViewPanel.cxx; behavior-preserving).
 *   macOS impl:   WebViewPanelHostMac.mm (new; create/destroy/setBounds/
 *                 show/hide work, several ops are deliberate no-ops for now
 *                 -- see that file's header comment).
 *
 * Two members (setPanel, forceFrameRelayout) are additions beyond the
 * Decision 5 op list, needed to preserve behavior that predates this
 * refactor:
 *   - setPanel(): the host itself is per-frame persistent (owned by
 *     PerFrameCefState in WebViewPanel.cxx) and survives panel destroy/
 *     recreate across OLE in-place activation (chart/equation edit). Its
 *     "which WebViewPanel do I call back into" pointer must be rebindable,
 *     mirroring WebViewMessageHandler::setPanel() / WebViewCefClient::setPanel().
 *   - forceFrameRelayout(): the WebViewPanel constructor's "Option C"
 *     synthetic-resize kick (force the sidebar deck to re-layout once CEF
 *     attaches) is a native operation on the frame window, not the CEF host
 *     window, but it belongs alongside the other frame-touching ops here.
 * Both are flagged for Opus review.
 */

#ifndef INCLUDED_OFFICELABS_INATIVECEFHOST_HXX
#define INCLUDED_OFFICELABS_INATIVECEFHOST_HXX

#ifdef HAVE_FEATURE_CEF

#include <vcl/sysdata.hxx>

#include <memory>

namespace officelabs {

class WebViewPanel;

/// Platform-neutral native window/view handle passed across the
/// INativeCefHost boundary. Always non-owning; the concrete host impl owns
/// the underlying resource's lifetime.
///   Windows: HWND    (the popup CEF host window).
///   macOS:   NSView*  (the child CEF host view).
using NativeWindowHandle = void*;

/// The LO document frame that a CEF host must anchor itself to, passed to
/// INativeCefHost::create().
///   Windows: the frame HWND -- becomes the popup's WS_POPUP owner window.
///   macOS:   the frame's NSView -- becomes the CEF child NSView's superview.
struct NativeParent
{
    NativeWindowHandle handle = nullptr;
};

/// Extract the platform native handle from a VCL window's system data.
/// Isolates the one #ifdef needed to read the right SystemEnvData member
/// (hWnd vs. mpNSView) so callers (WebViewPanel.cxx) never branch on
/// platform themselves.
inline NativeWindowHandle nativeHandleFromSystemEnvData(const SystemEnvData* pData)
{
    if (!pData)
        return nullptr;
#if defined(_WIN32)
    return static_cast<NativeWindowHandle>(pData->hWnd);
#elif defined(MACOSX)
    return static_cast<NativeWindowHandle>(pData->mpNSView);
#else
    return nullptr;
#endif
}

class INativeCefHost
{
public:
    virtual ~INativeCefHost() = default;

    /// Create the native host window/view anchored to frameParent. Returns
    /// the handle to later pass to CefWindowInfo::SetAsChild() (same value
    /// as cefParent()). Returns nullptr on failure.
    virtual NativeWindowHandle create(NativeParent frameParent) = 0;

    /// Tear down the native host window/view. Idempotent; safe to call
    /// again from the destructor.
    virtual void destroy() = 0;

    /// Reposition/resize the host to the given SCREEN-PIXEL rectangle
    /// (top-left origin -- the LO/VCL convention on every platform; macOS
    /// impls are responsible for their own bottom-left Cocoa y-flip
    /// internally). Also responsible for whatever native-state gating the
    /// platform needs before applying it (fullscreen guard, minimized
    /// check, z-order) -- see WebViewPanelHostWin::setBounds() for the exact
    /// Windows behavior this preserves.
    virtual void setBounds(int x, int y, int w, int h) = 0;

    /// Window-relative variant: x,y are top-left pixels relative to the
    /// frame's own client area, so no screen coordinate (and no cached VCL
    /// frame position that lags a live drag) is involved. Returns false when
    /// the platform host does not position this way, in which case the caller
    /// falls back to setBounds(). Only the macOS host overrides this.
    virtual bool setBoundsInFrame(int /*x*/, int /*y*/, int /*w*/, int /*h*/) { return false; }

    virtual void show() = 0;
    virtual void hide() = 0;

    /// Bring the host to the top of the native z-order (relative to its
    /// frame's siblings, e.g. after the frame is activated).
    virtual void raiseAbove() = 0;

    /// Hide the host while some other native fullscreen window (e.g. an
    /// Impress slideshow) holds the foreground, so the sidebar doesn't
    /// cover the presentation. No-op on macOS -- fullscreen there is its
    /// own Space and does not overlap other windows the way a Windows
    /// borderless monitor-covering window does.
    virtual void keepBelowFullscreen() = 0;

    /// Notifications that the owning LO frame moved / resized, driven by
    /// whatever native tracking hook the host installed in create()
    /// (Windows: the frame subclass; macOS: NSView frame-change
    /// notifications). Hosts call these on themselves and are expected to
    /// re-sync -- typically by calling back into
    /// WebViewPanel::syncCefWindowSize() to get fresh VCL-computed
    /// geometry, then re-applying it via setBounds().
    virtual void onFrameMoved() = 0;
    virtual void onFrameResized() = 0;

    /// Notification that the owning LO frame's activation state changed.
    /// Windows: drives the cross-panel broadcast + z-order raise on
    /// WM_ACTIVATE. No-op on macOS for now -- revisit once mac
    /// activation/z-order parity is designed.
    virtual void onFrameActivated(bool active) = 0;

    /// Forward an intercepted CEF key event (F5 / Esc slideshow hotkeys)
    /// back to the LO frame's accelerator table. No-op on macOS for now --
    /// revisit once mac keyboard routing is designed.
    virtual void forwardKeyEvent(int vkCode, bool bShift) = 0;

    /// The handle to pass to CefWindowInfo::SetAsChild(). Same value as the
    /// return of create() while the host is alive; nullptr after destroy().
    virtual NativeWindowHandle cefParent() const = 0;

    // --- Additions beyond the Decision 5 op list (see file header) ---

    /// Rebind the host to a (possibly different, possibly null)
    /// WebViewPanel instance. See file header comment.
    virtual void setPanel(WebViewPanel* pPanel) = 0;

    /// Force the owning LO frame to re-run its own layout pass (the ctor's
    /// synthetic-resize sidebar re-layout kick). No-op on macOS -- AppKit/
    /// VCL layout there has not been observed to need this nudge; revisit
    /// if the sidebar mis-sizes on first CEF attach.
    virtual void forceFrameRelayout() = 0;
};

/// Factory: constructs the platform-appropriate INativeCefHost impl.
/// Implemented in WebViewPanelHostWin.cxx (Windows) / WebViewPanelHostMac.mm
/// (macOS).
std::unique_ptr<INativeCefHost> createNativeCefHost(WebViewPanel* pPanel);

} // namespace officelabs

#endif // HAVE_FEATURE_CEF
#endif // INCLUDED_OFFICELABS_INATIVECEFHOST_HXX

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
