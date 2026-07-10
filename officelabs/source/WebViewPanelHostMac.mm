/* -*- Mode: ObjC++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * OfficeLabs INativeCefHost — macOS implementation (mac port, Decision 5 of
 * ~/cef-spike/drafts/MAC_PORT_DESIGN.md).
 *
 * Embeds the CEF browser as a child NSView of the LO frame's NSView (the
 * mac analog of the Windows owned-WS_POPUP approach). Reference for the
 * SetAsChild() embedding pattern: ~/cef-spike/cef/tests/cefsimple/
 * cefsimple_mac.mm (proven building/running CEF 144 on this mac).
 *
 * THREADING: every method here assumes it runs on the AppKit main thread.
 * Per Decision 1 of the design doc, CEF's UI thread, LO's VCL main thread,
 * and the AppKit main thread are the SAME thread on macOS (external message
 * pump), and WebViewPanel.cxx already only calls into the native host from
 * VCL-thread contexts (ctor, postToVclThread() callbacks) -- so no
 * additional thread-hop is needed here.
 *
 * SCOPE (tonight): create/destroy/setBounds/show/hide/cefParent/setPanel
 * are implemented for real -- enough to link and do a basic embed.
 * raiseAbove/keepBelowFullscreen/onFrameActivated/forwardKeyEvent/
 * forceFrameRelayout are deliberate no-ops with a rationale comment each;
 * onFrameMoved/onFrameResized delegate back to WebViewPanel::syncCefWindowSize()
 * (mirrors the Windows host) but nothing yet calls them on mac (no
 * NSView-frame-change / NSWindowDelegate observer is installed in create()
 * yet -- revisit once mac move/resize tracking is designed; the 500ms
 * ResizeTimerHdl polling fallback still runs regardless).
 */

#ifdef HAVE_FEATURE_CEF
#ifdef MACOSX

// premac.h/postmac.h rename the Carbon-era global Point/Size/TimeValue/
// TimeRecord identifiers (pulled in transitively by Cocoa.h via MacTypes.h)
// out of the way for the scope of the Cocoa import only, so LO's own
// tools::Point/Size classes (via officelabs/WebViewPanel.hxx below, which
// drags in vcl/weld -> tools/gen.hxx) don't collide with the SDK's plain
// C Point/Size structs. This is the same pattern vcl/osx/*.mm uses.
#include <premac.h>
#import <Cocoa/Cocoa.h>
#include <postmac.h>

#include <officelabs/INativeCefHost.hxx>
#include <officelabs/WebViewPanel.hxx>

#include <sal/log.hxx>

namespace officelabs {

class WebViewPanelHostMac final : public INativeCefHost
{
public:
    explicit WebViewPanelHostMac(WebViewPanel* pPanel)
        : m_pPanel(pPanel)
    {
    }

    ~WebViewPanelHostMac() override
    {
        destroyInternal();
    }

    NativeWindowHandle create(NativeParent frameParent) override
    {
        NSView* pSuperview = static_cast<NSView*>(frameParent.handle);
        if (!pSuperview)
        {
            SAL_WARN("officelabs.cef", "WebViewPanelHostMac::create: no frame NSView");
            return nullptr;
        }

        // Non-owning: this is the LO frame's own view, not ours.
        m_pSuperview = pSuperview;

        // Initial rect is (0,0,0,0); the caller (WebViewPanel::initCefBrowser)
        // immediately follows up with setBounds() -- same two-step pattern
        // as the Windows host's CreateWindowExW + setBounds().
        NSView* pChild = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 0, 0)];
        [pChild setWantsLayer:YES];
        [pSuperview addSubview:pChild];

        // alloc/init gave us +1 ownership under MRC; under ARC the __strong
        // member below manages it automatically. addSubview: adds its OWN
        // separate reference via the view hierarchy either way -- see
        // destroyInternal() for the matching teardown.
        m_pChildView = pChild;

        SAL_INFO("officelabs.cef", "WebViewPanelHostMac::create: child NSView attached");
        return static_cast<NativeWindowHandle>(m_pChildView);
    }

    void destroy() override
    {
        destroyInternal();
    }

    void setBounds(int x, int y, int w, int h) override
    {
        if (!m_pChildView)
            return;

        // x,y,w,h are SCREEN-PIXEL, top-left origin (the VCL convention on
        // every platform -- see INativeCefHost::setBounds()). Cocoa's
        // screen space is bottom-left origin, so flip against the primary
        // screen's height, then convert the resulting screen rect into our
        // superview's local coordinate space via its NSWindow. Going
        // through -convertRectFromScreen: (rather than a bare per-screen
        // flip) keeps this correct across multi-monitor layouts.
        NSArray<NSScreen*>* pScreens = [NSScreen screens];
        CGFloat primaryHeight = (pScreens.count > 0)
            ? pScreens[0].frame.size.height
            : [NSScreen mainScreen].frame.size.height;

        NSRect screenRect = NSMakeRect(x, primaryHeight - (y + h), w, h);

        NSWindow* pWindow = [m_pSuperview window];
        NSRect targetRect = pWindow
            ? [m_pSuperview convertRect:[pWindow convertRectFromScreen:screenRect]
                               fromView:nil]
            : screenRect; // defensive fallback if not yet attached to a window

        [m_pChildView setFrame:targetRect];
    }

    void show() override
    {
        if (m_pChildView)
            [m_pChildView setHidden:NO];
    }

    void hide() override
    {
        if (m_pChildView)
            [m_pChildView setHidden:YES];
    }

    // TODO(mac): the sidebar has no competing floating NSViews today, so
    // native z-order among siblings is not yet a concern the way Windows'
    // popup-vs-frame HWND z-order is. Revisit if a mac-native floating
    // panel (menu, popover) is ever found to render behind the CEF view.
    void raiseAbove() override
    {
    }

    // TODO(mac): an Impress slideshow going fullscreen on macOS moves to its
    // own Space rather than becoming a borderless window that overlaps the
    // document window in place (which is what the Windows guard defends
    // against) -- so there is no equivalent "other window is covering me"
    // condition to detect here yet. Revisit if mac fullscreen is ever
    // implemented as an in-place overlay instead of a Space transition.
    void keepBelowFullscreen() override
    {
    }

    void onFrameMoved() override
    {
        // Mirrors the Windows host: ask the panel to recompute VCL geometry
        // and re-sync. Nothing calls this on mac yet (see file header) --
        // the 500ms ResizeTimerHdl poll in WebViewPanel.cxx is the fallback
        // that currently drives resync on this platform.
        if (m_pPanel)
            m_pPanel->syncCefWindowSize();
    }

    void onFrameResized() override
    {
        onFrameMoved();
    }

    // TODO(mac): Windows drives this from WM_ACTIVATE on the frame subclass
    // to broadcast-sync all panels and raise this one to the front. No
    // NSWindowDelegate / NSApplicationDidBecomeActiveNotification observer
    // is installed yet to produce the equivalent signal on mac. Revisit
    // once multi-window activation switching is exercised.
    void onFrameActivated(bool /*active*/) override
    {
    }

    // TODO(mac): F5/Esc slideshow-hotkey forwarding needs a mac equivalent
    // of Windows' SetForegroundWindow + PostMessage(WM_KEYDOWN) -- likely
    // re-dispatching an NSEvent to the frame's NSWindow, or invoking the
    // .uno: command directly via the LO dispatcher instead of synthesizing
    // a key event. Revisit before keyboard parity is required on mac.
    void forwardKeyEvent(int /*vkCode*/, bool /*bShift*/) override
    {
    }

    NativeWindowHandle cefParent() const override
    {
        return static_cast<NativeWindowHandle>(m_pChildView);
    }

    void setPanel(WebViewPanel* pPanel) override
    {
        m_pPanel = pPanel;
    }

    // TODO(mac): the Windows ctor's synthetic-WM_SIZE sidebar re-layout kick
    // has no known mac equivalent requirement -- AppKit/VCL layout on mac
    // has not been observed to need this nudge. Revisit if the sidebar
    // mis-sizes on first CEF attach once a mac build can be exercised.
    void forceFrameRelayout() override
    {
    }

private:
    void destroyInternal()
    {
        if (m_pChildView)
        {
            [m_pChildView removeFromSuperview];
#if !__has_feature(objc_arc)
            [m_pChildView release];
#endif
            m_pChildView = nil;
        }
        m_pSuperview = nil;
    }

    WebViewPanel* m_pPanel = nullptr;
    NSView* m_pSuperview = nullptr; // non-owning: the LO frame's view
    NSView* m_pChildView = nullptr; // owning: the CEF host view
};

std::unique_ptr<INativeCefHost> createNativeCefHost(WebViewPanel* pPanel)
{
    return std::make_unique<WebViewPanelHostMac>(pPanel);
}

} // namespace officelabs

#endif // MACOSX
#endif // HAVE_FEATURE_CEF

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
