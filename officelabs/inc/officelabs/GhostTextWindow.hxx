/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef INCLUDED_OFFICELABS_GHOSTTEXTWINDOW_HXX
#define INCLUDED_OFFICELABS_GHOSTTEXTWINDOW_HXX

#include <officelabs/officelabsdllapi.h>
#include <rtl/ustring.hxx>
#include <tools/color.hxx>
#include <tools/gen.hxx>
#include <vcl/font.hxx>
#include <vcl/window.hxx>
#include <vcl/vclptr.hxx>

#include <optional>

namespace officelabs {

/// A small non-activating, mouse-transparent child window that renders a
/// dimmed preview of an inline completion suggestion next to the text cursor.
class OFFICELABS_DLLPUBLIC GhostTextWindow final : public vcl::Window
{
public:
    explicit GhostTextWindow(vcl::Window* pEditWin);

    /// Show the ghost text immediately to the right of the caret rectangle.
    /// If there is no room, the window is hidden instead. When rDocFont is
    /// present, it is used verbatim (its height already in pixels); when
    /// absent the app font sized to caret height is used.
    bool showAt(const tools::Rectangle& rCaretPixel, const OUString& rText,
                const std::optional<vcl::Font>& rDocFont = std::nullopt);

    /// Hide the window and clear the stored text.
    void hide();

    bool isShowing() const { return m_bShowing; }
    const OUString& text() const { return m_sText; }

    /// Compute the caret rectangle in pixel coordinates relative to the edit
    /// window's output area. Returns std::nullopt when no usable cursor is
    /// available (no cursor, or a non-vertical cursor -- the latter cannot
    /// be queried from the current public Cursor API).
    static std::optional<tools::Rectangle> caretRectPixel(vcl::Window* pEditWin);

    /// Compute the ghost-text color for a given page background, matching
    /// VS Code's editorGhostText: black at alpha 119/255 on a light page,
    /// white at alpha 86/255 on a dark page, blended onto the background.
    static Color ghostTextColor(Color aPageBackground);

protected:
    virtual void Paint(vcl::RenderContext& rRC, const tools::Rectangle& rRect) override;

private:
    OUString m_sText;
    bool m_bShowing;
};

} // namespace officelabs

#endif // INCLUDED_OFFICELABS_GHOSTTEXTWINDOW_HXX

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
