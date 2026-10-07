/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <cppunit/TestAssert.h>
#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>

#include <officelabs/BundledAgent.hxx>

#include <algorithm>

using namespace officelabs;

namespace
{
bool has(const std::vector<std::string>& v, const std::string& s)
{
    return std::find(v.begin(), v.end(), s) != v.end();
}

class BundledAgentTest : public CppUnit::TestFixture
{
public:
    // GIVEN two assignments of one key WHEN parsed THEN the later wins.
    void testParseLaterWins()
    {
        std::map<std::string, std::string> m;
        parseConfig("A=1\nA=2\n", m);
        CPPUNIT_ASSERT_EQUAL(std::string("2"), m["A"]);
    }

    // GIVEN comments and blank lines WHEN parsed THEN only real keys are kept.
    void testParseSkipsCommentsAndBlanks()
    {
        std::map<std::string, std::string> m;
        parseConfig("# c\n\n   \n  # indented\nK=v\n", m);
        CPPUNIT_ASSERT_EQUAL(size_t(1), m.size());
        CPPUNIT_ASSERT_EQUAL(std::string("v"), m["K"]);
    }

    // GIVEN invalid keys and a line without '=' WHEN parsed THEN they are ignored.
    void testParseRejectsBadKeys()
    {
        std::map<std::string, std::string> m;
        parseConfig("1BAD=x\nBAD-KEY=y\n=z\nnoequals\nOK_1=fine\n", m);
        CPPUNIT_ASSERT_EQUAL(size_t(1), m.size());
        CPPUNIT_ASSERT_EQUAL(std::string("fine"), m["OK_1"]);
    }

    // GIVEN CRLF line endings WHEN parsed THEN values carry no '\r'.
    void testParseCrlf()
    {
        std::map<std::string, std::string> m;
        parseConfig("A=1\r\nB=two words\r\n", m);
        CPPUNIT_ASSERT_EQUAL(std::string("1"), m["A"]);
        CPPUNIT_ASSERT_EQUAL(std::string("two words"), m["B"]);
    }

    // GIVEN whitespace around key and value start WHEN parsed THEN they are trimmed;
    // a value may itself contain '='.
    void testParseTrimsAndKeepsEqualsInValue()
    {
        std::map<std::string, std::string> m;
        parseConfig("  KEY  =  a=b  \n", m);
        CPPUNIT_ASSERT_EQUAL(std::string("a=b"), m["KEY"]);
    }

    // GIVEN existing entries in io WHEN parsed THEN a later file overrides them.
    void testParseOverridesExisting()
    {
        std::map<std::string, std::string> m{ { "A", "old" }, { "B", "keep" } };
        parseConfig("A=new\n", m);
        CPPUNIT_ASSERT_EQUAL(std::string("new"), m["A"]);
        CPPUNIT_ASSERT_EQUAL(std::string("keep"), m["B"]);
    }

    // GIVEN a free port WHEN deciding THEN Spawn.
    void testDecideFreeSpawns()
    {
        CPPUNIT_ASSERT(decide(false, false) == PortDecision::Spawn);
    }

    // GIVEN a port held by our agent WHEN deciding THEN Reuse.
    void testDecideAgentReuses()
    {
        CPPUNIT_ASSERT(decide(true, true) == PortDecision::Reuse);
    }

    // GIVEN a port held by something else WHEN deciding THEN Blocked.
    void testDecideForeignBlocked()
    {
        CPPUNIT_ASSERT(decide(true, false) == PortDecision::Blocked);
    }

    // GIVEN no config WHEN the env is built THEN the launcher defaults are present.
    void testEnvDefaults()
    {
        auto e = buildEnv({});
        CPPUNIT_ASSERT(has(e, "LLM_COMPLETION_MODEL=officelabs-inline"));
        CPPUNIT_ASSERT(has(e, "OFFICELABS_CHANNEL=development"));
        CPPUNIT_ASSERT(has(e, "SKIP_UNO=1"));
        CPPUNIT_ASSERT(has(e, "PATH=/usr/bin:/bin"));
    }

    // GIVEN a config overriding the channel WHEN the env is built THEN the
    // override replaces the default and is not duplicated.
    void testEnvConfigOverrides()
    {
        auto e = buildEnv({ { "OFFICELABS_CHANNEL", "production" },
                            { "OFFICELABS_API_URL", "https://x" } });
        CPPUNIT_ASSERT(has(e, "OFFICELABS_CHANNEL=production"));
        CPPUNIT_ASSERT(!has(e, "OFFICELABS_CHANNEL=development"));
        CPPUNIT_ASSERT(has(e, "OFFICELABS_API_URL=https://x"));
        CPPUNIT_ASSERT_EQUAL(1, int(std::count_if(e.begin(), e.end(), [](const std::string& s) {
                                 return s.rfind("OFFICELABS_CHANNEL=", 0) == 0;
                             })));
    }

    CPPUNIT_TEST_SUITE(BundledAgentTest);
    CPPUNIT_TEST(testParseLaterWins);
    CPPUNIT_TEST(testParseSkipsCommentsAndBlanks);
    CPPUNIT_TEST(testParseRejectsBadKeys);
    CPPUNIT_TEST(testParseCrlf);
    CPPUNIT_TEST(testParseTrimsAndKeepsEqualsInValue);
    CPPUNIT_TEST(testParseOverridesExisting);
    CPPUNIT_TEST(testDecideFreeSpawns);
    CPPUNIT_TEST(testDecideAgentReuses);
    CPPUNIT_TEST(testDecideForeignBlocked);
    CPPUNIT_TEST(testEnvDefaults);
    CPPUNIT_TEST(testEnvConfigOverrides);
    CPPUNIT_TEST_SUITE_END();
};

CPPUNIT_TEST_SUITE_REGISTRATION(BundledAgentTest);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
