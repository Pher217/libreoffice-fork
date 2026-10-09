/* OfficeLabs theme helper — resolves the UI theme once per process */
#pragma once

#include <tools/color.hxx>
#include <vcl/themecolors.hxx>
#include <vcl/officelabstheme.hxx>
#include <string>

namespace sfx2::sidebar {

using vcl::officelabs::OLTheme;
using vcl::officelabs::OLColors;
using vcl::officelabs::GetOLTheme;
using vcl::officelabs::GetOLColors;

inline bool IsOLThemeConfigured() { return vcl::officelabs::GetOLThemeSource().configured; }

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

} // namespace
