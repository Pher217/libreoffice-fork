/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Whether the native CEF host (the Win32 popup / macOS child view) may be on
 * screen. The host is a native window over the frame, not part of the VCL
 * sidebar, so it does not disappear on its own when the sidebar has nothing to
 * show: the Start Center has no document, and a chat panel there floats over
 * the backing window with no deck frame around it (project#399).
 */

#ifndef INCLUDED_OFFICELABS_PANELHOSTVISIBILITY_HXX
#define INCLUDED_OFFICELABS_PANELHOSTVISIBILITY_HXX

namespace officelabs {

/// Whether a document is open for THIS panel's frame. When the panel's own view
/// frame resolves, only that frame's object shell counts: the globally current
/// shell belongs to the focused window, so with two windows it would report
/// window B's document for window A's Start Center. The current shell is only
/// a fallback for a frame that cannot be resolved at all.
inline constexpr bool documentOpenForFrame(bool bFrameResolved, bool bFrameHasShell,
                                           bool bCurrentShellExists)
{
    return bFrameResolved ? bFrameHasShell : bCurrentShellExists;
}

enum class HostAction
{
    None,
    Hide,
    Show
};

/// What the host needs for the current document state. Without a document it
/// is always hidden (idempotent: a re-attach may have shown it again). When a
/// document appears after the host was hidden for lack of one, it is shown
/// again, because macOS setBounds() does not unhide. With a document and no
/// earlier hide the native host keeps its own show/hide gating (minimize,
/// fullscreen), so nothing is done.
inline constexpr HostAction hostActionForDocumentState(bool bDocumentOpen,
                                                       bool bHiddenForNoDocument)
{
    if (!bDocumentOpen)
        return HostAction::Hide;
    return bHiddenForNoDocument ? HostAction::Show : HostAction::None;
}

} // namespace officelabs

#endif // INCLUDED_OFFICELABS_PANELHOSTVISIBILITY_HXX

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
