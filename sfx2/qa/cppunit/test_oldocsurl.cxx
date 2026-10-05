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

#include <officelabsdocs.hxx>

using sfx2::officelabs::ResolveDocsUrl;

namespace
{
// OfficeLabs Help (F1) opens the docs site, never LibreOffice's help
// (officelabs-project#303).
class OLDocsUrlTest : public CppUnit::TestFixture
{
public:
    // GIVEN no override and no channel WHEN resolved THEN it is the prod docs host.
    void testDefaultsToProd()
    {
        CPPUNIT_ASSERT_EQUAL(std::string("https://docs.officelabs.tech"),
                             ResolveDocsUrl(nullptr, nullptr));
    }

    // GIVEN the dev channel WHEN resolved THEN it is docs.dev.officelabs.tech.
    void testDevChannel()
    {
        CPPUNIT_ASSERT_EQUAL(std::string("https://docs.dev.officelabs.tech"),
                             ResolveDocsUrl(nullptr, "dev"));
    }

    // GIVEN an unknown channel WHEN resolved THEN it falls back to prod.
    void testUnknownChannelIsProd()
    {
        CPPUNIT_ASSERT_EQUAL(std::string("https://docs.officelabs.tech"),
                             ResolveDocsUrl(nullptr, "beta"));
    }

    // GIVEN an override and the dev channel WHEN resolved THEN the override wins.
    void testOverrideWins()
    {
        CPPUNIT_ASSERT_EQUAL(std::string("http://localhost:3000"),
                             ResolveDocsUrl("http://localhost:3000", "dev"));
    }

    // GIVEN an empty override WHEN resolved THEN it is ignored.
    void testEmptyOverrideIgnored()
    {
        CPPUNIT_ASSERT_EQUAL(std::string("https://docs.officelabs.tech"),
                             ResolveDocsUrl("", nullptr));
    }

    CPPUNIT_TEST_SUITE(OLDocsUrlTest);
    CPPUNIT_TEST(testDefaultsToProd);
    CPPUNIT_TEST(testDevChannel);
    CPPUNIT_TEST(testUnknownChannelIsProd);
    CPPUNIT_TEST(testOverrideWins);
    CPPUNIT_TEST(testEmptyOverrideIgnored);
    CPPUNIT_TEST_SUITE_END();
};

CPPUNIT_TEST_SUITE_REGISTRATION(OLDocsUrlTest);
}

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
