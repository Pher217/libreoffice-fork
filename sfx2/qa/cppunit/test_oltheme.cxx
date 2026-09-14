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
#include <vcl/officelabstheme.hxx>

using sfx2::sidebar::GetOLAppearanceMode;
using sfx2::sidebar::OLTheme;
using vcl::officelabs::ResolveOLTheme;

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

// Resolution order shared by vcl and sfx2: user profile file > env var > install
// share file > "midnight-blue" (unconfigured).
class OLThemeResolveTest : public CppUnit::TestFixture
{
public:
    // GIVEN a user file, an env var and a share file all set WHEN resolved THEN
    // the user file wins.
    void testUserFileWinsOverEnvAndShare()
    {
        CPPUNIT_ASSERT_EQUAL(std::string("light"), ResolveOLTheme("light", "dark", "midnight-blue").name);
    }

    // GIVEN no user file but an env var and a share file WHEN resolved THEN the
    // env var wins.
    void testEnvWinsOverShareWhenUserFileEmpty()
    {
        CPPUNIT_ASSERT_EQUAL(std::string("dark"), ResolveOLTheme("", "dark", "midnight-blue").name);
    }

    // GIVEN no user file and no env var but a share file WHEN resolved THEN the
    // share file is used.
    void testShareUsedWhenUserFileAndEnvEmpty()
    {
        CPPUNIT_ASSERT_EQUAL(std::string("midnight-blue"), ResolveOLTheme("", nullptr, "midnight-blue").name);
    }

    // GIVEN an empty env var string WHEN resolved THEN it is ignored in favour
    // of the share file.
    void testEmptyEnvStringIsIgnored()
    {
        CPPUNIT_ASSERT_EQUAL(std::string("midnight-blue"), ResolveOLTheme("", "", "midnight-blue").name);
    }

    // GIVEN nothing configured WHEN resolved THEN the name defaults to
    // "midnight-blue".
    void testNothingConfiguredDefaultsToMidnightBlue()
    {
        CPPUNIT_ASSERT_EQUAL(std::string("midnight-blue"), ResolveOLTheme("", nullptr, "").name);
    }

    // GIVEN nothing configured WHEN resolved THEN configured is false.
    void testNothingConfiguredIsNotConfigured()
    {
        CPPUNIT_ASSERT(!ResolveOLTheme("", nullptr, "").configured);
    }

    CPPUNIT_TEST_SUITE(OLThemeResolveTest);
    CPPUNIT_TEST(testUserFileWinsOverEnvAndShare);
    CPPUNIT_TEST(testEnvWinsOverShareWhenUserFileEmpty);
    CPPUNIT_TEST(testShareUsedWhenUserFileAndEnvEmpty);
    CPPUNIT_TEST(testEmptyEnvStringIsIgnored);
    CPPUNIT_TEST(testNothingConfiguredDefaultsToMidnightBlue);
    CPPUNIT_TEST(testNothingConfiguredIsNotConfigured);
    CPPUNIT_TEST_SUITE_END();
};

CPPUNIT_TEST_SUITE_REGISTRATION(OLThemeResolveTest);
}

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
