/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <sal/types.h>
#include <sfx2/sfxsids.hrc>

namespace sfx2::sidebar
{
/// OfficeLabs: the sidebar opens with every new document window, whatever
/// visibility the profile persisted. The AI Assistant deck is the product, and
/// upstream persists the sidebar's visibility per module: once a user hid it
/// (View > Sidebar), the profile stored "H" and every later launch opened with
/// no sidebar and no tab bar, nothing on screen pointing back to it
/// (officelabs-project#350). Hiding it still works for the window it is hidden
/// in. LibreOfficeKit keeps upstream behaviour: its clients open the sidebar on
/// demand.
inline bool ResolveInitialChildWindowVisibility(sal_uInt16 nId, bool bPersistedVisible,
                                                bool bLibreOfficeKit)
{
    if (nId == SID_SIDEBAR && !bLibreOfficeKit)
        return true;
    return bPersistedVisible;
}
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
