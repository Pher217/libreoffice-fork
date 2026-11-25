/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

// OfficeLabs: the hook behind the Start Center account entry (project#418).
// sfx2 cannot depend on the officelabs module (it depends on sfx2), so the
// module registers a factory here and BackingWindow fills its empty
// "account_container" with whatever the factory builds. Stock LibreOffice
// registers nothing and the container stays empty.

#ifndef INCLUDED_SFX2_STARTCENTERACCOUNT_HXX
#define INCLUDED_SFX2_STARTCENTERACCOUNT_HXX

#include <sfx2/dllapi.h>

#include <memory>

namespace weld
{
class Container;
}

namespace sfx2
{
/// Owned by the BackingWindow that asked for it; destroyed with it.
class StartCenterAccountEntry
{
public:
    virtual ~StartCenterAccountEntry() = default;

    /// The Start Center gained focus: re-read whatever the entry shows.
    virtual void refresh() = 0;
};

/// Builds the entry as a child of the given container.
using StartCenterAccountFactory = std::unique_ptr<StartCenterAccountEntry> (*)(weld::Container&);

SFX2_DLLPUBLIC void SetStartCenterAccountFactory(StartCenterAccountFactory pFactory);
SFX2_DLLPUBLIC StartCenterAccountFactory GetStartCenterAccountFactory();
}

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
