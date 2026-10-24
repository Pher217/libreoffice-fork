/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <rtl/ustring.hxx>
#include <sal/types.h>

namespace sfx2::sidebar
{
/// Mirrors gnWidthOpenThreshold in SidebarController.cxx: a sidebar narrower
/// than the tab bar plus this many pixels opens with its deck closed.
constexpr sal_Int32 SIDEBAR_DECK_OPEN_THRESHOLD = 40;

/// OfficeLabs: the sidebar opens with its deck expanded, whatever width the
/// profile persisted. Closing the deck (the x in its title bar) shrinks the
/// sidebar to the tab-bar strip, and SfxDockingWindow::FillInfo persists that
/// width in the "AL:(align,last,line/pos/hsize/vsize,width;height)" extra
/// string. SidebarController derives the deck's open state from the width, so
/// every later launch showed only the icon strip and the AI deck stayed shut
/// (officelabs-project#352). This is the width counterpart of #350's forced
/// visibility.
///
/// Returns rExtraString with both persisted widths (hsize and width) set to
/// nExpandedWidth when the docked width is below nOpenWidth; any other string,
/// including one that does not parse, is returned unchanged.
inline OUString ExpandCollapsedSidebarWidth(const OUString& rExtraString, sal_Int32 nOpenWidth,
                                            sal_Int32 nExpandedWidth)
{
    const sal_Int32 nStart = rExtraString.indexOf("AL:(");
    if (nStart == -1)
        return rExtraString;
    const sal_Int32 nBodyStart = nStart + 4;
    const sal_Int32 nEnd = rExtraString.indexOf(')', nBodyStart);
    if (nEnd == -1)
        return rExtraString;

    // align , last , line/pos/hsize/vsize , width;height
    const OUString aBody = rExtraString.copy(nBodyStart, nEnd - nBodyStart);
    sal_Int32 nIdx = 0;
    const OUString aAlign = aBody.getToken(0, ',', nIdx);
    const OUString aLast = aBody.getToken(0, ',', nIdx);
    const OUString aPosSize = aBody.getToken(0, ',', nIdx);
    if (nIdx == -1)
        return rExtraString;
    const OUString aSplit = aBody.getToken(0, ',', nIdx);
    if (nIdx != -1)
        return rExtraString;

    sal_Int32 nPosIdx = 0;
    const OUString aLine = aPosSize.getToken(0, '/', nPosIdx);
    const OUString aPos = aPosSize.getToken(0, '/', nPosIdx);
    const OUString aHSize = aPosSize.getToken(0, '/', nPosIdx);
    if (nPosIdx == -1)
        return rExtraString;
    const OUString aVSize = aPosSize.getToken(0, '/', nPosIdx);
    if (nPosIdx != -1)
        return rExtraString;

    const sal_Int32 nSemicolon = aSplit.indexOf(';');
    if (nSemicolon == -1)
        return rExtraString;
    const sal_Int32 nWidth = aSplit.copy(0, nSemicolon).toInt32();
    if (nWidth < 0 || nWidth >= nOpenWidth)
        return rExtraString;

    const OUString aExpanded = OUString::number(nExpandedWidth);
    const sal_Int32 nHSize = aHSize.toInt32();
    const OUString aNewBody = aAlign + "," + aLast + "," + aLine + "/" + aPos + "/"
                              + (nHSize < nOpenWidth ? aExpanded : aHSize) + "/" + aVSize + ","
                              + aExpanded + aSplit.copy(nSemicolon);
    return rExtraString.replaceAt(nBodyStart, nEnd - nBodyStart, aNewBody);
}
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
