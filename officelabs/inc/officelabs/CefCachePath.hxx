/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Where CEF keeps its cache (localStorage, cookies, the profile lock).
 *
 * Chromium's process-singleton lock lives in the root cache directory. Two
 * soffice processes that share one directory collide: the second
 * CefInitialize() hands off to the first and fails, leaving its sidebar blank,
 * and the first answers the hand-off like a Chrome relaunch by opening a
 * chrome://newtab window (project#367).
 *
 * Two soffice processes only coexist when they run on different LibreOffice
 * profiles -- a second launch on the same profile is forwarded to the running
 * office by LibreOffice's own pipe before CEF is touched. So the cache is keyed
 * on the profile: the default profile keeps the per-user directory (and the
 * localStorage already in it), and an explicitly chosen profile
 * (-env:UserInstallation=..., as start_all and the eval harness pass) gets a
 * directory inside that profile.
 */

#ifndef INCLUDED_OFFICELABS_CEFCACHEPATH_HXX
#define INCLUDED_OFFICELABS_CEFCACHEPATH_HXX

#include <officelabs/officelabsdllapi.h>

#include <string>
#include <vector>

namespace officelabs {

/// True when the profile was chosen explicitly: a non-empty
/// "-env:UserInstallation=" command-line argument, or a non-empty
/// UserInstallation environment variable (pEnvValue, may be null).
OFFICELABS_DLLPUBLIC bool hasUserInstallationOverride(const std::vector<std::string>& rArgs,
                                                      const char* pEnvValue);

/// The CEF root cache directory: rDefaultRoot when rProfileDir is empty,
/// otherwise rProfileDir/officelabs/cef_data joined with cSep.
OFFICELABS_DLLPUBLIC std::string cefRootCachePath(const std::string& rDefaultRoot,
                                                  const std::string& rProfileDir, char cSep);

} // namespace officelabs

#endif // INCLUDED_OFFICELABS_CEFCACHEPATH_HXX

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
