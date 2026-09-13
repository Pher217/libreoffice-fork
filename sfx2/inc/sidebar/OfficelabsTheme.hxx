/* OfficeLabs theme helper — resolves the UI theme once per process */
#pragma once

#include <config_folders.h>
#include <osl/file.hxx>
#include <rtl/bootstrap.hxx>
#include <rtl/byteseq.hxx>
#include <rtl/ustring.hxx>
#include <tools/color.hxx>
#include <cstdlib>
#include <string>

namespace sfx2::sidebar {

enum class OLTheme { Light, MidnightBlue, Dark };

namespace detail {

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

struct OLThemeSource
{
    std::string name;
    bool configured;
};

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
        if (!aUser.empty())
            return OLThemeSource{ aUser, true };
        if (const char* p = std::getenv("OFFICELABS_THEME"); p && *p)
            return OLThemeSource{ p, true };
        std::string aShare
            = ReadOLThemeFileURL(u"$BRAND_BASE_DIR/" LIBO_SHARE_FOLDER "/officelabs_theme.txt"_ustr);
        if (!aShare.empty())
            return OLThemeSource{ aShare, true };
        return OLThemeSource{ "midnight-blue", false };
    }();
    return s;
}

}

inline bool IsOLThemeConfigured() { return detail::GetOLThemeSource().configured; }

inline OLTheme GetOLTheme()
{
    const std::string& theme = detail::GetOLThemeSource().name;
    if (theme == "light")
        return OLTheme::Light;
    if (theme == "dark")
        return OLTheme::Dark;
    return OLTheme::MidnightBlue;
}

struct OLColors
{
    Color bg;
    Color surface;
    Color border;
    Color text;
    Color subtext;
};

inline OLColors GetOLColors()
{
    switch (GetOLTheme())
    {
        case OLTheme::Light:
            return { Color(0xFA, 0xFA, 0xFA),   // bg
                     Color(0xF0, 0xF1, 0xF3),   // surface
                     Color(0xD0, 0xD0, 0xD0),   // border — darker for ruler contrast
                     Color(0x1A, 0x1A, 0x1A),   // text
                     Color(0x8B, 0x8B, 0x8B) };  // subtext
        case OLTheme::Dark:
            return { Color(0x1A, 0x1A, 0x1A),   // bg
                     Color(0x2A, 0x2A, 0x2A),   // surface
                     Color(0x3A, 0x3A, 0x3A),   // border
                     Color(0xFF, 0xFF, 0xFF),   // text
                     Color(0x77, 0x77, 0x77) };  // subtext
        case OLTheme::MidnightBlue:
        default:
            return { Color(0x21, 0x22, 0x2C),   // bg — VS Code Dracula sidebar
                     Color(0x28, 0x2A, 0x36),   // surface — Dracula editor bg
                     Color(0x44, 0x47, 0x5A),   // border
                     Color(0xF8, 0xF8, 0xF2),   // text
                     Color(0x62, 0x72, 0xA4) };  // subtext
    }
}

} // namespace
