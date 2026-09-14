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

#include <officelabs/InlineCompletionEligibility.hxx>

#include <rtl/strbuf.hxx>
#include <rtl/ustrbuf.hxx>
#include <rtl/ustring.hxx>

#include <string>

using officelabs::CursorContext;
using officelabs::buildCompletionRequest;
using officelabs::isEligible;
using officelabs::parseFirstSuggestion;
using officelabs::sanitizeSuggestion;
using officelabs::stillValid;

namespace
{
class InlineCompletionEligibilityTest : public CppUnit::TestFixture
{
public:
    // GIVEN the cursor has an active selection WHEN eligibility is checked
    // THEN no completion is requested.
    void testIsEligible_selectionDisallowed()
    {
        const CursorContext aContext{ u"enough chars here"_ustr, OUString(), true, false };
        CPPUNIT_ASSERT(!isEligible(aContext));
    }

    // GIVEN the document is read-only WHEN eligibility is checked THEN no
    // completion is requested.
    void testIsEligible_readOnlyDisallowed()
    {
        const CursorContext aContext{ u"enough chars here"_ustr, OUString(), false, true };
        CPPUNIT_ASSERT(!isEligible(aContext));
    }

    // GIVEN non-whitespace text follows the cursor WHEN eligibility is checked
    // THEN no completion is requested.
    void testIsEligible_nonWhitespaceAfterDisallowed()
    {
        const CursorContext aContext{ u"enough chars here"_ustr, u"next word"_ustr, false, false };
        CPPUNIT_ASSERT(!isEligible(aContext));
    }

    // GIVEN only whitespace follows the cursor WHEN eligibility is checked
    // THEN a completion may be requested.
    void testIsEligible_whitespaceAfterAllowed()
    {
        const CursorContext aContext{ u"enough chars here"_ustr, u"   "_ustr, false, false };
        CPPUNIT_ASSERT(isEligible(aContext));
    }

    // GIVEN fewer than ten characters precede the cursor WHEN eligibility is
    // checked THEN no completion is requested.
    void testIsEligible_nineCharsBeforeDisallowed()
    {
        const CursorContext aContext{ u"123456789"_ustr, OUString(), false, false };
        CPPUNIT_ASSERT(!isEligible(aContext));
    }

    // GIVEN exactly ten characters precede the cursor WHEN eligibility is
    // checked THEN a completion may be requested.
    void testIsEligible_tenCharsBeforeAllowed()
    {
        const CursorContext aContext{ u"1234567890"_ustr, OUString(), false, false };
        CPPUNIT_ASSERT(isEligible(aContext));
    }

    // GIVEN the cursor context is unchanged since the request WHEN staleness is
    // checked THEN the suggestion is still valid.
    void testStillValid_unchanged()
    {
        const CursorContext aContext{ u"1234567890"_ustr, u"   "_ustr, false, false };
        CPPUNIT_ASSERT(stillValid(aContext, aContext));
    }

    // GIVEN the text before the cursor changed since the request WHEN staleness
    // is checked THEN the suggestion is no longer valid.
    void testStillValid_textBeforeChanged()
    {
        const CursorContext aRequested{ u"1234567890"_ustr, OUString(), false, false };
        const CursorContext aCurrent{ u"1234567890x"_ustr, OUString(), false, false };
        CPPUNIT_ASSERT(!stillValid(aRequested, aCurrent));
    }

    // GIVEN the text after the cursor changed since the request WHEN staleness
    // is checked THEN the suggestion is no longer valid.
    void testStillValid_textAfterChanged()
    {
        const CursorContext aRequested{ u"1234567890"_ustr, u"   "_ustr, false, false };
        const CursorContext aCurrent{ u"1234567890"_ustr, u"  x"_ustr, false, false };
        CPPUNIT_ASSERT(!stillValid(aRequested, aCurrent));
    }

    // GIVEN a multi-line suggestion WHEN it is sanitized THEN it is cut at the
    // first line break.
    void testSanitizeSuggestion_cutAtNewline()
    {
        CPPUNIT_ASSERT_EQUAL(u"jumps over"_ustr,
                             sanitizeSuggestion(u"jumps over\nthe lazy dog"_ustr));
    }

    // GIVEN a suggestion that starts with a carriage-return/line-feed pair
    // WHEN it is sanitized THEN the result collapses to empty.
    void testSanitizeSuggestion_leadingCrLfBecomesEmpty()
    {
        CPPUNIT_ASSERT_EQUAL(OUString(), sanitizeSuggestion(u"\r\nx"_ustr));
    }

    // GIVEN a whitespace-only suggestion WHEN it is sanitized THEN it is
    // collapsed to empty.
    void testSanitizeSuggestion_whitespaceOnlyBecomesEmpty()
    {
        CPPUNIT_ASSERT_EQUAL(OUString(), sanitizeSuggestion(u"   "_ustr));
    }

    // GIVEN a suggestion whose only content is a leading space WHEN it is
    // sanitized THEN the leading space is preserved.
    void testSanitizeSuggestion_keepsLeadingSpace()
    {
        CPPUNIT_ASSERT_EQUAL(u" leading"_ustr, sanitizeSuggestion(u" leading"_ustr));
    }

    // GIVEN text_before containing characters that must be escaped in JSON
    // WHEN the completion request body is built THEN the result is exactly the
    // expected JSON with the quote, backslash and newline escaped and the UTF-8
    // byte sequence passed through unescaped.
    void testBuildCompletionRequest_escapesSpecialCharacters()
    {
        OUStringBuffer aBefore;
        aBefore.append('"');
        aBefore.append('\\');
        aBefore.append('\n');
        aBefore.append(u'\u00E9');
        const CursorContext aContext{ aBefore.makeStringAndClear(), OUString(), false, false };

        OStringBuffer aExpected;
        aExpected.append("{\"text_before\":\"");
        aExpected.append("\\\"");     // escaped double quote
        aExpected.append("\\\\");     // escaped backslash
        aExpected.append("\\n");      // escaped newline
        aExpected.append("\xC3\xA9");  // UTF-8 for U+00E9
        aExpected.append("\",\"text_after\":\"\",\"mode\":\"inline\",\"max_suggestions\":1}");

        CPPUNIT_ASSERT_EQUAL(aExpected.makeStringAndClear(), buildCompletionRequest(aContext));
    }

    // GIVEN more than 2000 characters before the cursor WHEN the completion
    // request body is built THEN only the last 2000 characters are kept.
    void testBuildCompletionRequest_capsTextBefore()
    {
        OUStringBuffer aBefore;
        for (int i = 0; i < 500; ++i)
            aBefore.append('a');
        for (int i = 0; i < 2000; ++i)
            aBefore.append('b');
        const CursorContext aContext{ aBefore.makeStringAndClear(), OUString(), false, false };

        // The JSON keys themselves contain 'a', so pin the whole text_before value.
        const std::string sExpected = "{\"text_before\":\"" + std::string(2000, 'b') + "\",";
        CPPUNIT_ASSERT(buildCompletionRequest(aContext).startsWith(
            std::string_view(sExpected.data(), sExpected.size())));
    }

    // GIVEN a well-formed response with one suggestion WHEN the first
    // suggestion is parsed THEN its text is returned.
    void testParseFirstSuggestion_returnsText()
    {
        CPPUNIT_ASSERT_EQUAL(
            u"jumps"_ustr,
            parseFirstSuggestion("{\"suggestions\":[{\"text\":\"jumps\",\"confidence\":1.0,\"type\":\"word\"}]}"));
    }

    // GIVEN a well-formed response with an empty suggestions array WHEN the
    // first suggestion is parsed THEN an empty string is returned.
    void testParseFirstSuggestion_emptySuggestions()
    {
        CPPUNIT_ASSERT_EQUAL(OUString(), parseFirstSuggestion("{\"suggestions\":[]}"));
    }

    // GIVEN a response that is not valid JSON WHEN the first suggestion is
    // parsed THEN an empty string is returned.
    void testParseFirstSuggestion_invalidJson()
    {
        CPPUNIT_ASSERT_EQUAL(OUString(), parseFirstSuggestion("not json"));
    }

    CPPUNIT_TEST_SUITE(InlineCompletionEligibilityTest);
    CPPUNIT_TEST(testIsEligible_selectionDisallowed);
    CPPUNIT_TEST(testIsEligible_readOnlyDisallowed);
    CPPUNIT_TEST(testIsEligible_nonWhitespaceAfterDisallowed);
    CPPUNIT_TEST(testIsEligible_whitespaceAfterAllowed);
    CPPUNIT_TEST(testIsEligible_nineCharsBeforeDisallowed);
    CPPUNIT_TEST(testIsEligible_tenCharsBeforeAllowed);
    CPPUNIT_TEST(testStillValid_unchanged);
    CPPUNIT_TEST(testStillValid_textBeforeChanged);
    CPPUNIT_TEST(testStillValid_textAfterChanged);
    CPPUNIT_TEST(testSanitizeSuggestion_cutAtNewline);
    CPPUNIT_TEST(testSanitizeSuggestion_leadingCrLfBecomesEmpty);
    CPPUNIT_TEST(testSanitizeSuggestion_whitespaceOnlyBecomesEmpty);
    CPPUNIT_TEST(testSanitizeSuggestion_keepsLeadingSpace);
    CPPUNIT_TEST(testBuildCompletionRequest_escapesSpecialCharacters);
    CPPUNIT_TEST(testBuildCompletionRequest_capsTextBefore);
    CPPUNIT_TEST(testParseFirstSuggestion_returnsText);
    CPPUNIT_TEST(testParseFirstSuggestion_emptySuggestions);
    CPPUNIT_TEST(testParseFirstSuggestion_invalidJson);
    CPPUNIT_TEST_SUITE_END();
};

CPPUNIT_TEST_SUITE_REGISTRATION(InlineCompletionEligibilityTest);
}

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
