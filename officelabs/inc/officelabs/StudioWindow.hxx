/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * The Macro Studio window (D25 / ADR-027).
 *
 * A CEF Views top-level window -- CefWindow::CreateTopLevelWindow +
 * CefBrowserView -- so Chromium owns geometry, activation, minimize and DPI and
 * the fork writes no platform windowing code for it.
 *
 * WHY THIS CARRIES ITS OWN MESSAGE ROUTER. A CefMessageRouterBrowserSide is
 * per-CefClient. The renderer-side router is per-render-PROCESS, so
 * window.cefQuery exists in every frame and the page's call is made either way
 * -- it is simply never answered by a client that wired no browser-side router.
 * agentClient.ts then does what it was designed to do: time out after 3s,
 * degrade to a null token, and send the request unauthenticated, which the
 * agent answers 401. There is no second token path (__OFFICELABS_SESSION_TOKEN__
 * is injected nowhere, and envToken() compiles out of a release build), so a
 * window without its own router is inert by construction.
 *
 * Established empirically 2026-08-22 by a within-run control: sidebar 200 and
 * Studio window 401 on the same three endpoints, same process, same $HOME, with
 * 0 of 658 trust-gate decisions carrying "?view=studio".
 * See 02 Projects/OfficeLabs/04 Lessons/lessons.md.
 */
#pragma once

namespace officelabs
{
/** Open the Macro Studio window, or focus the existing one.
 *
 * @param hOwnerFrame the native handle (HWND on Windows, NSView* on macOS --
 * see officelabs::NativeWindowHandle) of the LO document frame requesting the
 * Studio. Recorded only so closeStudioWindowForFrame() can close the Studio
 * again when that same frame closes; passing the wrong handle only affects
 * which frame's closing auto-closes the Studio, never document state.
 *
 * Safe to call from any thread; marshals to the CEF UI thread itself. That
 * matters on Windows, where multi_threaded_message_loop makes the CEF UI thread
 * a different thread from LibreOffice's main thread.
 */
void openStudioWindow(void* hOwnerFrame);

/** Close the Studio window if -- and only if -- it is currently owned by
 * hFrame (the same handle passed to openStudioWindow() that created it).
 * A no-op if the Studio is not open, or is owned by a different frame.
 *
 * Call this when hFrame's native handle is destroyed (the document/frame is
 * closing), so the Studio does not outlive the document that opened it. Does
 * not wait for the close to finish -- this is a fire-and-forget request, not
 * the bounded shutdown wait that closeStudioWindowAndWait() below performs.
 * Safe to call from any thread; marshals to the CEF UI thread itself.
 */
void closeStudioWindowForFrame(void* hFrame);

/** Close the Studio window and wait for it to be gone.
 *
 * CEF requires every browser closed before CefShutdown(), and CefShutdown()
 * knows nothing about a browser owned by a Views window. Called from
 * CefInit::shutdown() while the message pump is still running. Bounded: it
 * gives up and returns rather than hanging the quit.
 */
void closeStudioWindowAndWait();
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
