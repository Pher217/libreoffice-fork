/* OfficeLabs theme helper — resolves the UI theme once per process */
#pragma once

#include <tools/color.hxx>
#include <vcl/themecolors.hxx>
#include <vcl/officelabstheme.hxx>
#include <string>

namespace sfx2::sidebar {

enum class OLTheme { Light, MidnightBlue, Dark };

inline bool IsOLThemeConfigured() { return vcl::officelabs::GetOLThemeSource().configured; }

inline OLTheme GetOLTheme()
{
    const std::string& theme = vcl::officelabs::GetOLThemeSource().name;
    if (theme == "light")
        return OLTheme::Light;
    if (theme == "dark")
        return OLTheme::Dark;
    return OLTheme::MidnightBlue;
}

/// Native controls (on macOS the title bar, combo box fields and scrollers)
/// follow the application appearance, not the palette. On AUTO they follow the
/// system, so a dark system painted dark controls inside the light theme (#163).
/// Dark themes stay on AUTO outside macOS: Windows was verified that way.
inline AppearanceMode GetOLAppearanceMode(OLTheme eTheme)
{
    if (eTheme == OLTheme::Light)
        return AppearanceMode::LIGHT;
#ifdef MACOSX
    return AppearanceMode::DARK;
#else
    return AppearanceMode::AUTO;
#endif
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
