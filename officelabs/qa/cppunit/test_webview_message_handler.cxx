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
#include <rtl/ustrbuf.hxx>

using officelabs::buildActiveThemeJson;
using officelabs::isOpenableExternalUrl;

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

    // GIVEN a plain https URL WHEN checked THEN it is openable.
    void testOpenableUrlAcceptsHttps()
    {
        CPPUNIT_ASSERT(isOpenableExternalUrl(u"https://officelabs.lindest.eu/device/confirm"_ustr));
    }

    // GIVEN an https URL with a query string WHEN checked THEN it is
    // openable -- the device-confirm link carries a user_code parameter.
    void testOpenableUrlAcceptsQueryString()
    {
        CPPUNIT_ASSERT(
            isOpenableExternalUrl(u"https://officelabs.lindest.eu/device?user_code=ABCD-1234"_ustr));
    }

    // GIVEN a plain http URL WHEN checked THEN it is rejected -- only https
    // is trusted to open externally.
    void testOpenableUrlRejectsHttp()
    {
        CPPUNIT_ASSERT(!isOpenableExternalUrl(u"http://officelabs.lindest.eu/device/confirm"_ustr));
    }

    // GIVEN a file:// URL WHEN checked THEN it is rejected.
    void testOpenableUrlRejectsFileScheme()
    {
        CPPUNIT_ASSERT(!isOpenableExternalUrl(u"file:///etc/passwd"_ustr));
    }

    // GIVEN a javascript: URL WHEN checked THEN it is rejected.
    void testOpenableUrlRejectsJavascriptScheme()
    {
        CPPUNIT_ASSERT(!isOpenableExternalUrl(u"javascript:alert(1)"_ustr));
    }

    // GIVEN a data: URL WHEN checked THEN it is rejected.
    void testOpenableUrlRejectsDataScheme()
    {
        CPPUNIT_ASSERT(!isOpenableExternalUrl(u"data:text/html,<script>alert(1)</script>"_ustr));
    }

    // GIVEN an https URL with an embedded double quote WHEN checked THEN it
    // is rejected -- no well-formed URL needs one.
    void testOpenableUrlRejectsEmbeddedQuote()
    {
        CPPUNIT_ASSERT(!isOpenableExternalUrl(u"https://example.com/\"onload=alert(1)"_ustr));
    }

    // GIVEN an https URL with an embedded semicolon WHEN checked THEN it is
    // rejected.
    void testOpenableUrlRejectsEmbeddedSemicolon()
    {
        CPPUNIT_ASSERT(!isOpenableExternalUrl(u"https://example.com/;rm -rf /"_ustr));
    }

    // GIVEN an https URL with an embedded backtick WHEN checked THEN it is
    // rejected.
    void testOpenableUrlRejectsEmbeddedBacktick()
    {
        CPPUNIT_ASSERT(!isOpenableExternalUrl(u"https://example.com/`whoami`"_ustr));
    }

    // GIVEN an https URL with a control character embedded WHEN checked
    // THEN it is rejected.
    void testOpenableUrlRejectsControlCharacter()
    {
        CPPUNIT_ASSERT(!isOpenableExternalUrl(u"https://example.com/\npath"_ustr));
    }

    // GIVEN an empty string WHEN checked THEN it is rejected.
    void testOpenableUrlRejectsEmpty()
    {
        CPPUNIT_ASSERT(!isOpenableExternalUrl(u""_ustr));
    }

    // GIVEN an https URL exactly at the 2048-character length cap WHEN
    // checked THEN it is accepted -- the cap is inclusive.
    void testOpenableUrlAcceptsAtLengthCap()
    {
        OUStringBuffer aBuf(u"https://example.com/"_ustr);
        while (aBuf.getLength() < 2048)
            aBuf.append('a');
        const OUString sUrl = aBuf.makeStringAndClear();
        CPPUNIT_ASSERT_EQUAL(sal_Int32(2048), sUrl.getLength());
        CPPUNIT_ASSERT(isOpenableExternalUrl(sUrl));
    }

    // GIVEN an https URL one character past the 2048-character length cap
    // WHEN checked THEN it is rejected.
    void testOpenableUrlRejectsOverLengthCap()
    {
        OUStringBuffer aBuf(u"https://example.com/"_ustr);
        while (aBuf.getLength() < 2049)
            aBuf.append('a');
        const OUString sUrl = aBuf.makeStringAndClear();
        CPPUNIT_ASSERT_EQUAL(sal_Int32(2049), sUrl.getLength());
        CPPUNIT_ASSERT(!isOpenableExternalUrl(sUrl));
    }

    CPPUNIT_TEST_SUITE(WebViewMessageHandlerTest);
    CPPUNIT_TEST(testPlainThemeName);
    CPPUNIT_TEST(testEscapesDoubleQuote);
    CPPUNIT_TEST(testEscapesBackslash);
    CPPUNIT_TEST(testEmptyThemeName);
    CPPUNIT_TEST(testOpenableUrlAcceptsHttps);
    CPPUNIT_TEST(testOpenableUrlAcceptsQueryString);
    CPPUNIT_TEST(testOpenableUrlRejectsHttp);
    CPPUNIT_TEST(testOpenableUrlRejectsFileScheme);
    CPPUNIT_TEST(testOpenableUrlRejectsJavascriptScheme);
    CPPUNIT_TEST(testOpenableUrlRejectsDataScheme);
    CPPUNIT_TEST(testOpenableUrlRejectsEmbeddedQuote);
    CPPUNIT_TEST(testOpenableUrlRejectsEmbeddedSemicolon);
    CPPUNIT_TEST(testOpenableUrlRejectsEmbeddedBacktick);
    CPPUNIT_TEST(testOpenableUrlRejectsControlCharacter);
    CPPUNIT_TEST(testOpenableUrlRejectsEmpty);
    CPPUNIT_TEST(testOpenableUrlAcceptsAtLengthCap);
    CPPUNIT_TEST(testOpenableUrlRejectsOverLengthCap);
    CPPUNIT_TEST_SUITE_END();
};

CPPUNIT_TEST_SUITE_REGISTRATION(WebViewMessageHandlerTest);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
