/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/* OfficeLabs theme helper — resolves the UI theme once per process.
 * Header-only so vcl (which cannot depend on sfx2) and sfx2 share one
 * resolution order. */
#pragma once

#include <config_folders.h>
#include <osl/file.hxx>
#include <rtl/bootstrap.hxx>
#include <rtl/byteseq.hxx>
#include <rtl/ustring.hxx>
#include <cstdlib>
#include <string>
#include <string_view>

namespace vcl::officelabs {

struct OLThemeSource
{
    std::string name;
    bool configured;
};

// Pure resolution: the per-user profile file wins, then the env var, then the
// install share file; falls back to "midnight-blue" (unconfigured) otherwise.
inline OLThemeSource ResolveOLTheme(std::string_view aUserFile, const char* pEnv,
                                     std::string_view aShareFile)
{
    if (!aUserFile.empty())
        return OLThemeSource{ std::string(aUserFile), true };
    if (pEnv && *pEnv)
        return OLThemeSource{ std::string(pEnv), true };
    if (!aShareFile.empty())
        return OLThemeSource{ std::string(aShareFile), true };
    return OLThemeSource{ "midnight-blue", false };
}

// First line of the file at a bootstrap-macro file URL, trimmed; empty when unreadable.
// osl::File opens the URL directly, so non-ASCII profile paths work on every platform.
inline std::string ReadOLThemeFileURL(OUString aURL)
{
    rtl::Bootstrap::expandMacros(aURL);
    osl::File aFile(aURL);
    if (aFile.open(osl_File_OpenFlag_Read) != osl::FileBase::E_None)
        return {};
    rtl::ByteSequence aBytes;
    if (aFile.readLine(aBytes) != osl::FileBase::E_None)
        return {};
    std::string aLine(reinterpret_cast<const char*>(aBytes.getConstArray()), aBytes.getLength());
    while (!aLine.empty() && (aLine.back() == '\r' || aLine.back() == '\n' || aLine.back() == ' '))
        aLine.pop_back();
    return aLine;
}

// Resolved once: the per-user profile file the agent writes
// (<UserInstallation>/user/officelabs/theme.txt) wins, then env OFFICELABS_THEME, then the
// install share file. The file ranks above the env var because an office restart inherits
// the launcher's environment: with env first, a theme switched in Settings would never apply.
inline const OLThemeSource& GetOLThemeSource()
{
    static const OLThemeSource s = [] {
        std::string aUser = ReadOLThemeFileURL(
            u"${$BRAND_BASE_DIR/" LIBO_ETC_FOLDER "/" SAL_CONFIGFILE("bootstrap")
             ":UserInstallation}/user/officelabs/theme.txt"_ustr);
        std::string aShare
            = ReadOLThemeFileURL(u"$BRAND_BASE_DIR/" LIBO_SHARE_FOLDER "/officelabs_theme.txt"_ustr);
        return ResolveOLTheme(aUser, std::getenv("OFFICELABS_THEME"), aShare);
    }();
    return s;
}

} // namespace vcl::officelabs

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
