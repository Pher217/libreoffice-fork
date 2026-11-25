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
// (officelabs-project#303). The hosts come from the build's officelabs.config keys
// OFFICELABS_DOCS_URL_<CHANNEL> (officelabs-project#419); the literals are a last resort.
class OLDocsUrlTest : public CppUnit::TestFixture
{
public:
    // GIVEN no override, no channel and no configured hosts WHEN resolved
    // THEN it is the last-resort prod docs host.
    void testDefaultsToProd()
    {
        CPPUNIT_ASSERT_EQUAL(std::string("https://docs.officelabs.tech"),
                             ResolveDocsUrl(nullptr, nullptr));
    }

    // GIVEN the dev channel and no configured hosts WHEN resolved
    // THEN it is the last-resort docs.dev.officelabs.tech.
    void testDevChannel()
    {
        CPPUNIT_ASSERT_EQUAL(std::string("https://docs.dev.officelabs.tech"),
                             ResolveDocsUrl(nullptr, "dev"));
    }

    // GIVEN the development channel (the value officelabs.config carries) and no configured
    // hosts WHEN resolved THEN it is the last-resort docs.dev.officelabs.tech.
    void testDevelopmentChannel()
    {
        CPPUNIT_ASSERT_EQUAL(std::string("https://docs.dev.officelabs.tech"),
                             ResolveDocsUrl(nullptr, "development"));
    }

    // GIVEN the production channel and no configured hosts WHEN resolved
    // THEN it is the last-resort docs.officelabs.tech.
    void testProductionChannel()
    {
        CPPUNIT_ASSERT_EQUAL(std::string("https://docs.officelabs.tech"),
                             ResolveDocsUrl(nullptr, "production"));
    }

    // GIVEN an unknown channel and no configured hosts WHEN resolved THEN it falls back to prod.
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

    // GIVEN both configured hosts and the production channel WHEN resolved
    // THEN the production host is used.
    void testConfiguredProductionHost()
    {
        CPPUNIT_ASSERT_EQUAL(std::string("https://docs.prod.example"),
                             ResolveDocsUrl(nullptr, "production", "https://docs.prod.example",
                                            "https://docs.dev.example"));
    }

    // GIVEN both configured hosts and the development channel WHEN resolved
    // THEN the development host is used.
    void testConfiguredDevelopmentHost()
    {
        CPPUNIT_ASSERT_EQUAL(std::string("https://docs.dev.example"),
                             ResolveDocsUrl(nullptr, "development", "https://docs.prod.example",
                                            "https://docs.dev.example"));
    }

    // GIVEN both configured hosts and the "dev" channel alias WHEN resolved
    // THEN the development host is used.
    void testConfiguredDevAlias()
    {
        CPPUNIT_ASSERT_EQUAL(std::string("https://docs.dev.example"),
                             ResolveDocsUrl(nullptr, "dev", "https://docs.prod.example",
                                            "https://docs.dev.example"));
    }

    // GIVEN both configured hosts and no channel WHEN resolved THEN production is picked.
    void testConfiguredHostsNoChannelIsProd()
    {
        CPPUNIT_ASSERT_EQUAL(std::string("https://docs.prod.example"),
                             ResolveDocsUrl(nullptr, nullptr, "https://docs.prod.example",
                                            "https://docs.dev.example"));
    }

    // GIVEN only the production host configured and the development channel WHEN resolved
    // THEN it falls back to the development literal, never to the other channel's host.
    void testMissingKeyFallsBackPerChannel()
    {
        CPPUNIT_ASSERT_EQUAL(std::string("https://docs.dev.officelabs.tech"),
                             ResolveDocsUrl(nullptr, "development", "https://docs.prod.example",
                                            nullptr));
    }

    // GIVEN an empty configured host WHEN resolved THEN it is ignored in favour of the fallback.
    void testEmptyConfiguredHostIgnored()
    {
        CPPUNIT_ASSERT_EQUAL(std::string("https://docs.officelabs.tech"),
                             ResolveDocsUrl(nullptr, "production", "", nullptr));
    }

    // GIVEN an override and configured hosts WHEN resolved THEN the override still wins.
    void testOverrideBeatsConfiguredHosts()
    {
        CPPUNIT_ASSERT_EQUAL(std::string("http://localhost:3000"),
                             ResolveDocsUrl("http://localhost:3000", "development",
                                            "https://docs.prod.example",
                                            "https://docs.dev.example"));
    }

    CPPUNIT_TEST_SUITE(OLDocsUrlTest);
    CPPUNIT_TEST(testDefaultsToProd);
    CPPUNIT_TEST(testDevChannel);
    CPPUNIT_TEST(testDevelopmentChannel);
    CPPUNIT_TEST(testProductionChannel);
    CPPUNIT_TEST(testUnknownChannelIsProd);
    CPPUNIT_TEST(testOverrideWins);
    CPPUNIT_TEST(testEmptyOverrideIgnored);
    CPPUNIT_TEST(testConfiguredProductionHost);
    CPPUNIT_TEST(testConfiguredDevelopmentHost);
    CPPUNIT_TEST(testConfiguredDevAlias);
    CPPUNIT_TEST(testConfiguredHostsNoChannelIsProd);
    CPPUNIT_TEST(testMissingKeyFallsBackPerChannel);
    CPPUNIT_TEST(testEmptyConfiguredHostIgnored);
    CPPUNIT_TEST(testOverrideBeatsConfiguredHosts);
    CPPUNIT_TEST_SUITE_END();
};

CPPUNIT_TEST_SUITE_REGISTRATION(OLDocsUrlTest);
}

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
