/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * The CEF remote-debugging (DevTools/CDP) port is opt-in.
 *
 * An open DevTools port lets any local process evaluate script in the sidebar,
 * and through its cefQuery bridge drive the document, so shipped builds must
 * never listen. Development and the headed Playwright tests set
 * OFFICELABS_CEF_DEBUG_PORT (start_all.sh / start_all.ps1 export 9222).
 */

#ifndef INCLUDED_OFFICELABS_CEFDEBUGPORT_HXX
#define INCLUDED_OFFICELABS_CEFDEBUGPORT_HXX

#include <officelabs/officelabsdllapi.h>

namespace officelabs {

/// Environment variable that enables CEF remote debugging on the given port.
inline constexpr char CEF_DEBUG_PORT_ENV[] = "OFFICELABS_CEF_DEBUG_PORT";

/// Parses the value of OFFICELABS_CEF_DEBUG_PORT. Returns the port when it is
/// a plain decimal number in 1024-65535 (surrounding whitespace allowed), and
/// 0 -- remote debugging disabled -- for null, empty or anything else.
OFFICELABS_DLLPUBLIC int parseRemoteDebuggingPort(const char* pValue);

} // namespace officelabs

#endif // INCLUDED_OFFICELABS_CEFDEBUGPORT_HXX

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
