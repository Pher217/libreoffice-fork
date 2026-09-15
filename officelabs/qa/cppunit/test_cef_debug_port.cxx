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

#include <officelabs/CefDebugPort.hxx>

using officelabs::parseRemoteDebuggingPort;

namespace
{
class CefDebugPortTest : public CppUnit::TestFixture
{
public:
    // GIVEN OFFICELABS_CEF_DEBUG_PORT is unset WHEN the port is parsed
    // THEN remote debugging stays off (0).
    void testUnsetDisables() { CPPUNIT_ASSERT_EQUAL(0, parseRemoteDebuggingPort(nullptr)); }

    // GIVEN the variable is empty WHEN the port is parsed THEN it is 0.
    void testEmptyDisables() { CPPUNIT_ASSERT_EQUAL(0, parseRemoteDebuggingPort("")); }

    // GIVEN "9222" WHEN the port is parsed THEN it is 9222.
    void testValidPort() { CPPUNIT_ASSERT_EQUAL(9222, parseRemoteDebuggingPort("9222")); }

    // GIVEN surrounding whitespace WHEN the port is parsed THEN it is 9222.
    void testTrimsWhitespace() { CPPUNIT_ASSERT_EQUAL(9222, parseRemoteDebuggingPort(" 9222 ")); }

    // GIVEN a non-numeric value WHEN the port is parsed THEN it is 0.
    void testGarbageDisables() { CPPUNIT_ASSERT_EQUAL(0, parseRemoteDebuggingPort("yes")); }

    // GIVEN trailing garbage WHEN the port is parsed THEN it is 0.
    void testTrailingGarbageDisables()
    {
        CPPUNIT_ASSERT_EQUAL(0, parseRemoteDebuggingPort("9222abc"));
    }

    // GIVEN a privileged port WHEN the port is parsed THEN it is 0
    // (CEF only accepts 1024-65535).
    void testBelowRangeDisables() { CPPUNIT_ASSERT_EQUAL(0, parseRemoteDebuggingPort("1023")); }

    // GIVEN the lowest accepted port WHEN the port is parsed THEN it is 1024.
    void testLowestAccepted() { CPPUNIT_ASSERT_EQUAL(1024, parseRemoteDebuggingPort("1024")); }

    // GIVEN the highest accepted port WHEN the port is parsed THEN it is 65535.
    void testHighestAccepted() { CPPUNIT_ASSERT_EQUAL(65535, parseRemoteDebuggingPort("65535")); }

    // GIVEN a port above 65535 WHEN the port is parsed THEN it is 0.
    void testAboveRangeDisables() { CPPUNIT_ASSERT_EQUAL(0, parseRemoteDebuggingPort("65536")); }

    // GIVEN a huge number WHEN the port is parsed THEN it is 0, not an overflow.
    void testOverflowDisables()
    {
        CPPUNIT_ASSERT_EQUAL(0, parseRemoteDebuggingPort("99999999999999999999"));
    }

    // GIVEN a negative number WHEN the port is parsed THEN it is 0.
    void testNegativeDisables() { CPPUNIT_ASSERT_EQUAL(0, parseRemoteDebuggingPort("-9222")); }

    // GIVEN a leading zero WHEN the port is parsed THEN it is read as decimal 9222.
    void testLeadingZero() { CPPUNIT_ASSERT_EQUAL(9222, parseRemoteDebuggingPort("09222")); }

    // GIVEN whitespace inside the digits WHEN the port is parsed THEN it is 0.
    void testInteriorWhitespaceDisables()
    {
        CPPUNIT_ASSERT_EQUAL(0, parseRemoteDebuggingPort("92 22"));
    }

    // GIVEN a tab before and a newline after WHEN the port is parsed THEN it is 9222.
    void testTrimsTabAndNewline()
    {
        CPPUNIT_ASSERT_EQUAL(9222, parseRemoteDebuggingPort("\t9222\n"));
    }

    CPPUNIT_TEST_SUITE(CefDebugPortTest);
    CPPUNIT_TEST(testUnsetDisables);
    CPPUNIT_TEST(testEmptyDisables);
    CPPUNIT_TEST(testValidPort);
    CPPUNIT_TEST(testTrimsWhitespace);
    CPPUNIT_TEST(testGarbageDisables);
    CPPUNIT_TEST(testTrailingGarbageDisables);
    CPPUNIT_TEST(testBelowRangeDisables);
    CPPUNIT_TEST(testLowestAccepted);
    CPPUNIT_TEST(testHighestAccepted);
    CPPUNIT_TEST(testAboveRangeDisables);
    CPPUNIT_TEST(testOverflowDisables);
    CPPUNIT_TEST(testNegativeDisables);
    CPPUNIT_TEST(testLeadingZero);
    CPPUNIT_TEST(testInteriorWhitespaceDisables);
    CPPUNIT_TEST(testTrimsTabAndNewline);
    CPPUNIT_TEST_SUITE_END();
};

CPPUNIT_TEST_SUITE_REGISTRATION(CefDebugPortTest);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
