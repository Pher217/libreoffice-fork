/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <officelabs/CefCachePath.hxx>

namespace officelabs {

bool hasUserInstallationOverride(const std::vector<std::string>& rArgs, const char* pEnvValue)
{
    static const std::string aPrefix = "-env:UserInstallation=";
    for (const std::string& rArg : rArgs)
    {
        if (rArg.size() > aPrefix.size() && rArg.compare(0, aPrefix.size(), aPrefix) == 0)
            return true;
    }
    return pEnvValue && *pEnvValue;
}

std::string cefRootCachePath(const std::string& rDefaultRoot, const std::string& rProfileDir,
                             char cSep)
{
    if (rProfileDir.empty())
        return rDefaultRoot;

    std::string aRoot = rProfileDir;
    if (aRoot.back() != cSep)
        aRoot += cSep;
    return aRoot + "officelabs" + cSep + "cef_data";
}

} // namespace officelabs

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
