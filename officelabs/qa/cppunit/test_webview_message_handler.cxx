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

#include <officelabs/WebViewMessageHandler.hxx>

using officelabs::buildActiveThemeJson;

namespace
{
class WebViewMessageHandlerTest : public CppUnit::TestFixture
{
public:
    // GIVEN a plain theme name WHEN the getActiveTheme response is built
    // THEN it comes back as {"theme":"<name>"}.
    void testPlainThemeName()
    {
        CPPUNIT_ASSERT_EQUAL(std::string(R"({"theme":"midnight-blue"})"),
                              buildActiveThemeJson("midnight-blue"));
    }

    // GIVEN a theme name containing a double quote WHEN the response is built
    // THEN the quote is escaped so the payload stays valid JSON.
    void testEscapesDoubleQuote()
    {
        CPPUNIT_ASSERT_EQUAL(std::string(R"({"theme":"a\"b"})"), buildActiveThemeJson("a\"b"));
    }

    // GIVEN a theme name containing a backslash WHEN the response is built
    // THEN the backslash is escaped.
    void testEscapesBackslash()
    {
        CPPUNIT_ASSERT_EQUAL(std::string(R"({"theme":"a\\b"})"), buildActiveThemeJson("a\\b"));
    }

    // GIVEN an empty theme name WHEN the response is built THEN the theme
    // field is an empty string, not omitted.
    void testEmptyThemeName()
    {
        CPPUNIT_ASSERT_EQUAL(std::string(R"({"theme":""})"), buildActiveThemeJson(""));
    }

    CPPUNIT_TEST_SUITE(WebViewMessageHandlerTest);
    CPPUNIT_TEST(testPlainThemeName);
    CPPUNIT_TEST(testEscapesDoubleQuote);
    CPPUNIT_TEST(testEscapesBackslash);
    CPPUNIT_TEST(testEmptyThemeName);
    CPPUNIT_TEST_SUITE_END();
};

CPPUNIT_TEST_SUITE_REGISTRATION(WebViewMessageHandlerTest);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
