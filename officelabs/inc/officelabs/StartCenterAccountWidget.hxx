/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * The VCL shell of the Start Center sign-in entry (project#418).
 *
 * All decisions live in StartCenterAccount.hxx; this only renders them and
 * carries the agent calls. sfx2 cannot link this module, so BackingWindow
 * reaches it through a factory registered with sfx2/startcenteraccount.hxx.
 */

#ifndef INCLUDED_OFFICELABS_STARTCENTERACCOUNTWIDGET_HXX
#define INCLUDED_OFFICELABS_STARTCENTERACCOUNTWIDGET_HXX

#include <officelabs/officelabsdllapi.h>

namespace officelabs {

/// Registers the entry factory with sfx2. Idempotent; called from OfficeLabsJob
/// the first time a Start Center asks for it.
OFFICELABS_DLLPUBLIC void registerStartCenterAccount();

} // namespace officelabs

#endif // INCLUDED_OFFICELABS_STARTCENTERACCOUNTWIDGET_HXX

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
