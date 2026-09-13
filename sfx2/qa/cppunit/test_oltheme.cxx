/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <sal/types.h>
#include <cppunit/TestAssert.h>
#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/plugin/TestPlugIn.h>

#include <sidebar/OfficelabsTheme.hxx>

using sfx2::sidebar::GetOLAppearanceMode;
using sfx2::sidebar::OLTheme;

namespace
{
// The application appearance decides how native controls draw (on macOS the
// title bar, combo box fields and scrollers). Left on AUTO it follows the
// system, so a dark system painted dark controls inside the light theme
// (officelabs-project#163).
class OLThemeAppearanceTest : public CppUnit::TestFixture
{
public:
    // GIVEN the light theme WHEN its appearance is resolved THEN it is LIGHT,
    // whatever the system appearance is.
    void testLightThemeForcesLightAppearance()
    {
        CPPUNIT_ASSERT_EQUAL(static_cast<int>(AppearanceMode::LIGHT),
                             static_cast<int>(GetOLAppearanceMode(OLTheme::Light)));
    }

    // GIVEN the midnight-blue theme WHEN its appearance is resolved THEN it is
    // DARK on macOS and left on AUTO elsewhere.
    void testMidnightBlueAppearance()
    {
#ifdef MACOSX
        const int nExpected = static_cast<int>(AppearanceMode::DARK);
#else
        const int nExpected = static_cast<int>(AppearanceMode::AUTO);
#endif
        CPPUNIT_ASSERT_EQUAL(nExpected,
                             static_cast<int>(GetOLAppearanceMode(OLTheme::MidnightBlue)));
    }

    // GIVEN the dark theme WHEN its appearance is resolved THEN it is DARK on
    // macOS and left on AUTO elsewhere.
    void testDarkAppearance()
    {
#ifdef MACOSX
        const int nExpected = static_cast<int>(AppearanceMode::DARK);
#else
        const int nExpected = static_cast<int>(AppearanceMode::AUTO);
#endif
        CPPUNIT_ASSERT_EQUAL(nExpected, static_cast<int>(GetOLAppearanceMode(OLTheme::Dark)));
    }

    CPPUNIT_TEST_SUITE(OLThemeAppearanceTest);
    CPPUNIT_TEST(testLightThemeForcesLightAppearance);
    CPPUNIT_TEST(testMidnightBlueAppearance);
    CPPUNIT_TEST(testDarkAppearance);
    CPPUNIT_TEST_SUITE_END();
};

CPPUNIT_TEST_SUITE_REGISTRATION(OLThemeAppearanceTest);
}

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
