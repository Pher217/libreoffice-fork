/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * This file incorporates work covered by the following license notice:
 *
 *   Licensed to the Apache Software Foundation (ASF) under one or more
 *   contributor license agreements. See the NOTICE file distributed
 *   with this work for additional information regarding copyright
 *   ownership. The ASF licenses this file to you under the Apache
 *   License, Version 2.0 (the "License"); you may not use this file
 *   except in compliance with the License. You may obtain a copy of
 *   the License at http://www.apache.org/licenses/LICENSE-2.0 .
 */

#include <tools/mapunit.hxx>
#include <sfx2/sidebar/TabBar.hxx>
#include <sfx2/bindings.hxx>
#include <sfx2/sidebar/SidebarChildWindow.hxx>
#include <sfx2/sidebar/SidebarDockingWindow.hxx>
#include <sfx2/sfxsids.hrc>
#include <helpids.h>
#include <comphelper/lok.hxx>
#include <sidebar/SidebarRestoredWidth.hxx>

namespace sfx2::sidebar {

SFX_IMPL_DOCKINGWINDOW_WITHID(SidebarChildWindow, SID_SIDEBAR);

SidebarChildWindow::SidebarChildWindow(vcl::Window* pParentWindow, sal_uInt16 nId,
                                       SfxBindings* pBindings, SfxChildWinInfo* pInfo)
    : SfxChildWindow(pParentWindow, nId)
{
    auto pDockWin = VclPtr<SidebarDockingWindow>::Create(
        pBindings, *this, pParentWindow, WB_STDDOCKWIN | WB_OWNERDRAWDECORATION | WB_CLIPCHILDREN
                                             | WB_SIZEABLE | WB_3DLOOK);
    SetWindow(pDockWin);
    SetAlignment(SfxChildAlignment::RIGHT);

    pDockWin->SetHelpId(HID_SIDEBAR_WINDOW);
    pDockWin->SetOutputSizePixel(Size(GetDefaultWidth(pDockWin), 450));

    // OfficeLabs: upstream collapses the sidebar to the bare tab-bar strip on
    // first start in Writer and Calc (Draw, Impress and Math were already
    // exempt), overriding the width set immediately above. That default is
    // reasonable for a suite where the sidebar is an accessory panel; here the
    // AI Assistant deck IS the product, so shipping it collapsed hides the
    // whole feature behind a drag gesture most users never perform
    // (officelabs-project#106 -- making AIDeck the default deck in Sidebar.xcu
    // did not help, because the right deck was still being collapsed).
    //
    // The expanded width from GetDefaultWidth() is inherited from upstream's
    // paragraph-properties panel and is not tuned for a chat surface; picking
    // a deliberate width is deferred until it can be measured against the real
    // window rather than guessed.
    //
    // The same applies to a sidebar the user collapsed in an earlier session:
    // its persisted width would reopen the deck closed (officelabs-project#352).
    if (pInfo && !comphelper::LibreOfficeKit::isActive())
        pInfo->aExtraString = ExpandCollapsedSidebarWidth(
            pInfo->aExtraString, TabBar::GetDefaultWidth() + SIDEBAR_DECK_OPEN_THRESHOLD,
            GetDefaultWidth(pDockWin));

    pDockWin->Initialize(pInfo);

    if (comphelper::LibreOfficeKit::isActive())
    {
        // Undock sidebar in LOK to allow for resizing freely
        // (i.e. when the client window is resized) and collapse
        // it so the client can open it on demand.
        pDockWin->SetFloatingSize(Size(pDockWin->GetSizePixel().Width(),
                                       pDockWin->GetSizePixel().Height()));
        pDockWin->SetFloatingMode(true);
    }

    SetHideNotDelete(true);

    pDockWin->Show();
}

sal_Int32 SidebarChildWindow::GetDefaultWidth(vcl::Window const* pWindow)
{
    if (pWindow != nullptr)
    {
        // Width of the paragraph panel.
        const static sal_Int32 nMaxPropertyPageWidth(146);

        return pWindow->LogicToPixel(Point(nMaxPropertyPageWidth,1), MapMode(MapUnit::MapAppFont)).X()
            + TabBar::GetDefaultWidth();
    }
    else
        return 0;
}

} // end of namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
