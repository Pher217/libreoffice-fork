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

#include <officelabs/CefCachePath.hxx>

using officelabs::cefRootCachePath;
using officelabs::hasUserInstallationOverride;

namespace
{
const std::string DEFAULT_ROOT = "/Users/u/Library/Application Support/OfficeLabs/cef_data";

class CefCachePathTest : public CppUnit::TestFixture
{
public:
    // GIVEN no arguments and no environment variable WHEN checked
    // THEN the profile is the default one.
    void testNoOverride()
    {
        CPPUNIT_ASSERT(!hasUserInstallationOverride({ "--writer", "--nologo" }, nullptr));
    }

    // GIVEN an -env:UserInstallation= argument WHEN checked THEN it is an override.
    void testArgOverride()
    {
        CPPUNIT_ASSERT(hasUserInstallationOverride(
            { "--writer", "-env:UserInstallation=file:///Users/u/.officelabs/lo_fork_profile" },
            nullptr));
    }

    // GIVEN an -env:UserInstallation= argument with no value WHEN checked
    // THEN it is not an override.
    void testEmptyArgIsNotOverride()
    {
        CPPUNIT_ASSERT(!hasUserInstallationOverride({ "-env:UserInstallation=" }, nullptr));
    }

    // GIVEN a different -env: variable WHEN checked THEN it is not an override.
    void testOtherEnvArgIsNotOverride()
    {
        CPPUNIT_ASSERT(!hasUserInstallationOverride({ "-env:URE_BOOTSTRAP=file:///x" }, nullptr));
    }

    // GIVEN a UserInstallation environment variable WHEN checked THEN it is an override.
    void testEnvOverride()
    {
        CPPUNIT_ASSERT(hasUserInstallationOverride({}, "file:///tmp/profile"));
    }

    // GIVEN an empty UserInstallation environment variable WHEN checked
    // THEN it is not an override.
    void testEmptyEnvIsNotOverride() { CPPUNIT_ASSERT(!hasUserInstallationOverride({}, "")); }

    // GIVEN the default profile WHEN the cache path is computed
    // THEN the per-user default is kept, so existing localStorage survives.
    void testDefaultProfileKeepsDefaultRoot()
    {
        CPPUNIT_ASSERT_EQUAL(DEFAULT_ROOT, cefRootCachePath(DEFAULT_ROOT, "", '/'));
    }

    // GIVEN an explicit profile WHEN the cache path is computed
    // THEN the cache lives inside that profile.
    void testExplicitProfileGetsOwnRoot()
    {
        CPPUNIT_ASSERT_EQUAL(std::string("/Users/u/.officelabs/lo_fork_profile/officelabs/cef_data"),
                             cefRootCachePath(DEFAULT_ROOT, "/Users/u/.officelabs/lo_fork_profile",
                                              '/'));
    }

    // GIVEN a profile path with a trailing separator WHEN the cache path is
    // computed THEN no doubled separator appears.
    void testTrailingSeparator()
    {
        CPPUNIT_ASSERT_EQUAL(std::string("/p/officelabs/cef_data"),
                             cefRootCachePath(DEFAULT_ROOT, "/p/", '/'));
    }

    // GIVEN a Windows profile path WHEN the cache path is computed
    // THEN it is joined with backslashes.
    void testWindowsSeparator()
    {
        CPPUNIT_ASSERT_EQUAL(std::string("C:\\p\\officelabs\\cef_data"),
                             cefRootCachePath("C:\\d", "C:\\p", '\\'));
    }

    // GIVEN no default root and the default profile WHEN the cache path is
    // computed THEN it is empty (the caller leaves CEF at its default).
    void testNoDefaultRootStaysEmpty()
    {
        CPPUNIT_ASSERT_EQUAL(std::string(), cefRootCachePath("", "", '/'));
    }

    CPPUNIT_TEST_SUITE(CefCachePathTest);
    CPPUNIT_TEST(testNoOverride);
    CPPUNIT_TEST(testArgOverride);
    CPPUNIT_TEST(testEmptyArgIsNotOverride);
    CPPUNIT_TEST(testOtherEnvArgIsNotOverride);
    CPPUNIT_TEST(testEnvOverride);
    CPPUNIT_TEST(testEmptyEnvIsNotOverride);
    CPPUNIT_TEST(testDefaultProfileKeepsDefaultRoot);
    CPPUNIT_TEST(testExplicitProfileGetsOwnRoot);
    CPPUNIT_TEST(testTrailingSeparator);
    CPPUNIT_TEST(testWindowsSeparator);
    CPPUNIT_TEST(testNoDefaultRootStaysEmpty);
    CPPUNIT_TEST_SUITE_END();
};

CPPUNIT_TEST_SUITE_REGISTRATION(CefCachePathTest);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
