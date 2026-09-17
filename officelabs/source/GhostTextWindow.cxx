/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <officelabs/GhostTextWindow.hxx>

#include <svtools/colorcfg.hxx>
#include <vcl/cursor.hxx>
#include <vcl/settings.hxx>
#include <vcl/vclenum.hxx>

#include <algorithm>

namespace officelabs {

GhostTextWindow::GhostTextWindow(vcl::Window* pEditWin)
    : vcl::Window(pEditWin, WB_NOBORDER)
    , m_bShowing(false)
{
    SetMouseTransparent(true);
    SetBackground(Wallpaper(svtools::ColorConfig().GetColorValue(svtools::DOCCOLOR).nColor));
}

bool GhostTextWindow::showAt(const tools::Rectangle& rCaretPixel, const OUString& rText,
                              const std::optional<vcl::Font>& rDocFont)
{
    m_sText = rText;
    m_bShowing = false;

    if (rDocFont)
    {
        SetFont(*rDocFont);
    }
    else
    {
        vcl::Font aFont(GetSettings().GetStyleSettings().GetAppFont());
        tools::Long nHeight = rCaretPixel.GetHeight() * 4 / 5;
        if (nHeight < 1)
            nHeight = 1;
        aFont.SetFontHeight(nHeight);
        SetFont(aFont);
    }

    const tools::Long nTextWidth = GetTextWidth(rText);
    tools::Long nWidth = nTextWidth + 2;

    vcl::Window* pParent = GetParent();
    if (!pParent)
        return false;

    const tools::Long nParentWidth = pParent->GetOutputSizePixel().Width();
    const tools::Long nX = rCaretPixel.Right() + 1;
    const tools::Long nMaxWidth = nParentWidth - nX;

    if (nMaxWidth <= 0)
        return false;

    if (nWidth > nMaxWidth)
        nWidth = nMaxWidth;

    // Anchor to the caret rect's own top, which for ordinary text is the
    // document line's ascent-top: SwVisibleCursor::SetPos builds the caret
    // rect from the line's char rect. Size the window to exactly this font's
    // text height -- the same ascent+descent Paint measures off the same font
    // -- so Paint's centring term is always zero and the ghost baseline lands
    // at (window top + this font's ascent), i.e. the document baseline.
    //
    // The previous code grew the window around the caret's vertical CENTRE,
    // which only coincides with the line top when the two heights are equal.
    // Caret height is a Writer layout quantity with no relation to the ghost
    // font's metrics, so the text drifted off the baseline by half their
    // difference -- and centring in Paint misplaces it again, since a real
    // typeface's ascent is nearer 80% of its height than 50%.
    const tools::Long nHeight = GetTextHeight();
    const tools::Long nY = rCaretPixel.Top();

    SetPosSizePixel(Point(nX, nY), Size(nWidth, nHeight));
    Show(true, ShowFlags::NoActivate | ShowFlags::NoFocusChange);
    Invalidate();
    m_bShowing = true;
    return true;
}

void GhostTextWindow::hide()
{
    Show(false);
    m_sText.clear();
    m_bShowing = false;
}

void GhostTextWindow::Paint(vcl::RenderContext& rRC, const tools::Rectangle& /*rRect*/)
{
    if (m_sText.isEmpty())
        return;

    const Color aPageBackground(svtools::ColorConfig().GetColorValue(svtools::DOCCOLOR).nColor);

    rRC.SetFont(GetFont());
    rRC.SetTextColor(ghostTextColor(aPageBackground));

    const tools::Long nTextHeight = rRC.GetTextHeight();
    const tools::Long nWinHeight = GetOutputSizePixel().Height();
    const tools::Long nY = (nWinHeight - nTextHeight) / 2;

    rRC.DrawText(Point(0, nY), m_sText);
}

std::optional<tools::Rectangle> GhostTextWindow::caretRectPixel(vcl::Window* pEditWin)
{
    if (!pEditWin)
        return std::nullopt;

    vcl::Cursor* pCursor = pEditWin->GetCursor();
    if (!pCursor)
        return std::nullopt;

    // Cursor::GetOrientation() is not public; the only supported case is a
    // normal vertical cursor, which is the only kind produced by Writer's
    // edit window in practice. If a rotated cursor ever appears we will fall
    // back to not showing a ghost rather than guessing its geometry.
    tools::Rectangle aRect = pEditWin->LogicToPixel(
        tools::Rectangle(pCursor->GetPos(), pCursor->GetSize()));

    if (aRect.GetWidth() == 0)
    {
        Point aPos(aRect.TopLeft());
        aRect = tools::Rectangle(aPos, Size(1, aRect.GetHeight()));
    }

    return aRect;
}

Color GhostTextWindow::ghostTextColor(Color aPageBackground)
{
    const int nFg = aPageBackground.IsDark() ? 255 : 0;
    const int nAlpha = aPageBackground.IsDark() ? 86 : 119;

    auto blend = [nFg, nAlpha](sal_uInt8 nBg) -> sal_uInt8 {
        return static_cast<sal_uInt8>(static_cast<int>(nBg) + (nFg - static_cast<int>(nBg)) * nAlpha / 255);
    };

    return Color(blend(aPageBackground.GetRed()), blend(aPageBackground.GetGreen()),
                 blend(aPageBackground.GetBlue()));
}

} // namespace officelabs

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
