/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <test/unoapi_test.hxx>

#include <comphelper/string.hxx>
#include <rtl/ustrbuf.hxx>

#include <cppunit/TestAssert.h>
#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/plugin/TestPlugIn.h>

#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/container/XEnumeration.hpp>
#include <com/sun/star/container/XEnumerationAccess.hpp>
#include <com/sun/star/document/XRedlinesSupplier.hpp>
#include <com/sun/star/document/XUndoManager.hpp>
#include <com/sun/star/document/XUndoManagerSupplier.hpp>
#include <com/sun/star/frame/XModel.hpp>
#include <com/sun/star/lang/XMultiServiceFactory.hpp>
#include <com/sun/star/text/ControlCharacter.hpp>
#include <com/sun/star/text/XText.hpp>
#include <com/sun/star/text/XTextContent.hpp>
#include <com/sun/star/text/XTextCursor.hpp>
#include <com/sun/star/text/XTextDocument.hpp>
#include <com/sun/star/text/XTextRange.hpp>
#include <com/sun/star/text/XTextViewCursor.hpp>
#include <com/sun/star/text/XTextViewCursorSupplier.hpp>

#include <officelabs/DocumentController.hxx>
#include <officelabs/InlineCompletionEligibility.hxx>

using namespace css;
using namespace css::uno;
using officelabs::CursorCharFont;
using officelabs::CursorContext;

namespace
{
class DocumentControllerCursorTest : public UnoApiTest
{
public:
    DocumentControllerCursorTest()
        : UnoApiTest(u"/officelabs/qa/cppunit/data/"_ustr)
    {
    }

    // GIVEN a Writer document with the paragraph "The quick brown fox"
    // WHEN the view cursor is positioned at the end of the paragraph
    // THEN textBefore contains the whole paragraph and textAfter is empty.
    void testCursorContext_endOfParagraph()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDocument(mxComponent, UNO_QUERY_THROW);
        Reference<text::XText> xText = xTextDocument->getText();
        xText->setString(u"The quick brown fox"_ustr);

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);

        officelabs::DocumentController aController;
        aController.setModel(xModel);
        aController.setDocument(xTextDocument);

        Reference<text::XTextViewCursorSupplier> xViewCursorSupplier(
            xModel->getCurrentController(), UNO_QUERY_THROW);
        Reference<text::XTextViewCursor> xViewCursor = xViewCursorSupplier->getViewCursor();
        xViewCursor->gotoEnd(false);

        const CursorContext aContext = aController.getCursorContext();
        CPPUNIT_ASSERT(!aContext.readOnly);
        CPPUNIT_ASSERT(!aContext.hasSelection);
        CPPUNIT_ASSERT_EQUAL(u"The quick brown fox"_ustr, aContext.textBefore);
        CPPUNIT_ASSERT_EQUAL(OUString(), aContext.textAfter);
    }

    // GIVEN a Writer document with the paragraph "The quick brown fox"
    // WHEN the view cursor is positioned after the first four characters
    // THEN textBefore is "The " and textAfter is "quick brown fox".
    void testCursorContext_afterFourChars()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDocument(mxComponent, UNO_QUERY_THROW);
        Reference<text::XText> xText = xTextDocument->getText();
        xText->setString(u"The quick brown fox"_ustr);

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);

        officelabs::DocumentController aController;
        aController.setModel(xModel);
        aController.setDocument(xTextDocument);

        Reference<text::XTextViewCursorSupplier> xViewCursorSupplier(
            xModel->getCurrentController(), UNO_QUERY_THROW);
        Reference<text::XTextViewCursor> xViewCursor = xViewCursorSupplier->getViewCursor();
        xViewCursor->gotoStart(false);
        CPPUNIT_ASSERT(xViewCursor->goRight(4, false));

        const CursorContext aContext = aController.getCursorContext();
        CPPUNIT_ASSERT_EQUAL(u"The "_ustr, aContext.textBefore);
        CPPUNIT_ASSERT_EQUAL(u"quick brown fox"_ustr, aContext.textAfter);
    }

    // GIVEN a Writer document with the paragraph "The quick brown fox"
    // WHEN the view cursor is positioned at the start of the paragraph
    // THEN textBefore is empty.
    void testCursorContext_atStart()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDocument(mxComponent, UNO_QUERY_THROW);
        Reference<text::XText> xText = xTextDocument->getText();
        xText->setString(u"The quick brown fox"_ustr);

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);

        officelabs::DocumentController aController;
        aController.setModel(xModel);
        aController.setDocument(xTextDocument);

        Reference<text::XTextViewCursorSupplier> xViewCursorSupplier(
            xModel->getCurrentController(), UNO_QUERY_THROW);
        Reference<text::XTextViewCursor> xViewCursor = xViewCursorSupplier->getViewCursor();
        xViewCursor->gotoStart(false);

        const CursorContext aContext = aController.getCursorContext();
        CPPUNIT_ASSERT_EQUAL(OUString(), aContext.textBefore);
        CPPUNIT_ASSERT_EQUAL(u"The quick brown fox"_ustr, aContext.textAfter);
    }

    // GIVEN a Writer document with two paragraphs
    // WHEN the view cursor is at the end of the second paragraph
    // THEN textBefore reaches back past the paragraph break and includes the
    // first paragraph too.
    //
    // This case previously asserted paragraph-local "Second para". That was the
    // defect, not the contract: a fact one paragraph above the caret could not
    // reach inline completion however large the agent's window was
    // (officelabs-project#336).
    void testCursorContext_twoParagraphs()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDocument(mxComponent, UNO_QUERY_THROW);
        Reference<text::XText> xText = xTextDocument->getText();
        xText->insertString(xText->getEnd(), u"First para"_ustr, false);
        xText->insertControlCharacter(xText->getEnd(), text::ControlCharacter::PARAGRAPH_BREAK,
                                       false);
        xText->insertString(xText->getEnd(), u"Second para"_ustr, false);

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);

        officelabs::DocumentController aController;
        aController.setModel(xModel);
        aController.setDocument(xTextDocument);

        Reference<text::XTextViewCursorSupplier> xViewCursorSupplier(
            xModel->getCurrentController(), UNO_QUERY_THROW);
        Reference<text::XTextViewCursor> xViewCursor = xViewCursorSupplier->getViewCursor();
        xViewCursor->gotoEnd(false);

        const CursorContext aContext = aController.getCursorContext();
        // Asserted by containment and order rather than by an exact separator:
        // what this pins is that the earlier paragraph is reached at all, not
        // how UNO serialises a paragraph break.
        CPPUNIT_ASSERT(aContext.textBefore.indexOf(u"First para") >= 0);
        CPPUNIT_ASSERT(aContext.textBefore.endsWith(u"Second para"_ustr));
        CPPUNIT_ASSERT_EQUAL(OUString(), aContext.textAfter);
    }

    // GIVEN a document whose earlier paragraphs exceed the 2000-char budget
    // WHEN the caret is at the end
    // THEN textBefore stops at the budget rather than collecting the document.
    void testCursorContext_beforeStopsAtCharBudget()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDocument(mxComponent, UNO_QUERY_THROW);
        Reference<text::XText> xText = xTextDocument->getText();
        // 40 paragraphs x 100 chars = 4000, comfortably past the budget.
        OUStringBuffer aFillBuf(100);
        comphelper::string::padToLength(aFillBuf, 100, 'x');
        const OUString aFill = aFillBuf.makeStringAndClear();
        for (int i = 0; i < 40; ++i)
        {
            xText->insertString(xText->getEnd(), aFill, false);
            xText->insertControlCharacter(xText->getEnd(),
                                          text::ControlCharacter::PARAGRAPH_BREAK, false);
        }
        xText->insertString(xText->getEnd(), u"tail"_ustr, false);

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);
        officelabs::DocumentController aController;
        aController.setModel(xModel);
        aController.setDocument(xTextDocument);

        Reference<text::XTextViewCursorSupplier> xViewCursorSupplier(
            xModel->getCurrentController(), UNO_QUERY_THROW);
        xViewCursorSupplier->getViewCursor()->gotoEnd(false);

        const CursorContext aContext = aController.getCursorContext();

        // Well past one paragraph -- the old behaviour returned 4 -- and
        // actually bounded at the budget, not merely "somewhere under 4000".
        // The loose upper bound could not tell a cap from an overshoot.
        CPPUNIT_ASSERT(aContext.textBefore.getLength() > 1000);
        CPPUNIT_ASSERT_EQUAL(sal_Int32(2000), aContext.textBefore.getLength());
    }

    // GIVEN a document with paragraphs after the caret
    // WHEN the caret is at the very start
    // THEN textAfter reaches past the first paragraph break.
    void testCursorContext_afterStopsAtParagraphEnd()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDocument(mxComponent, UNO_QUERY_THROW);
        Reference<text::XText> xText = xTextDocument->getText();
        xText->insertString(xText->getEnd(), u"First para"_ustr, false);
        xText->insertControlCharacter(xText->getEnd(), text::ControlCharacter::PARAGRAPH_BREAK,
                                       false);
        xText->insertString(xText->getEnd(), u"Second para"_ustr, false);

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);
        officelabs::DocumentController aController;
        aController.setModel(xModel);
        aController.setDocument(xTextDocument);

        Reference<text::XTextViewCursorSupplier> xViewCursorSupplier(
            xModel->getCurrentController(), UNO_QUERY_THROW);
        xViewCursorSupplier->getViewCursor()->gotoStart(false);

        const CursorContext aContext = aController.getCursorContext();

        // textAfter is PARAGRAPH-LOCAL and must stay that way: it is the
        // eligibility gate, not a context budget. #75 asserted the opposite
        // here -- that textAfter reached "Second para" -- and that is precisely
        // what disabled ghost text everywhere except the end of the last
        // non-blank paragraph. See testCursorContext_isEligibleMidDocument.
        CPPUNIT_ASSERT(aContext.textAfter.startsWith(u"First para"_ustr));
        CPPUNIT_ASSERT_EQUAL(sal_Int32(-1), aContext.textAfter.indexOf(u"Second para"));
    }

    // GIVEN a Writer document with the paragraph "The quick brown fox"
    // WHEN the first three characters are selected
    // THEN getCursorContext reports hasSelection.
    void testCursorContext_selection()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDocument(mxComponent, UNO_QUERY_THROW);
        Reference<text::XText> xText = xTextDocument->getText();
        xText->setString(u"The quick brown fox"_ustr);

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);

        officelabs::DocumentController aController;
        aController.setModel(xModel);
        aController.setDocument(xTextDocument);

        Reference<text::XTextViewCursorSupplier> xViewCursorSupplier(
            xModel->getCurrentController(), UNO_QUERY_THROW);
        Reference<text::XTextViewCursor> xViewCursor = xViewCursorSupplier->getViewCursor();
        xViewCursor->gotoStart(false);
        CPPUNIT_ASSERT(xViewCursor->goRight(3, true));

        const CursorContext aContext = aController.getCursorContext();
        CPPUNIT_ASSERT(aContext.hasSelection);
    }

    // GIVEN a Writer document with the paragraph "The quick brown fox" and the
    // cursor at the end
    // WHEN insertAtCursor(" jumps") is called
    // THEN the document text becomes "The quick brown fox jumps".
    void testInsertAtCursor_appendsText()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDocument(mxComponent, UNO_QUERY_THROW);
        Reference<text::XText> xText = xTextDocument->getText();
        xText->setString(u"The quick brown fox"_ustr);

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);

        officelabs::DocumentController aController;
        aController.setModel(xModel);
        aController.setDocument(xTextDocument);

        Reference<text::XTextViewCursorSupplier> xViewCursorSupplier(
            xModel->getCurrentController(), UNO_QUERY_THROW);
        Reference<text::XTextViewCursor> xViewCursor = xViewCursorSupplier->getViewCursor();
        xViewCursor->gotoEnd(false);

        CPPUNIT_ASSERT(aController.insertAtCursor(u" jumps"_ustr));
        CPPUNIT_ASSERT_EQUAL(u"The quick brown fox jumps"_ustr, xText->getString());
    }

    // GIVEN insertAtCursor(" jumps") has just been executed
    // WHEN the document's undo manager performs one undo
    // THEN the document text is restored to "The quick brown fox".
    void testInsertAtCursor_undo()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDocument(mxComponent, UNO_QUERY_THROW);
        Reference<text::XText> xText = xTextDocument->getText();
        xText->setString(u"The quick brown fox"_ustr);

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);

        officelabs::DocumentController aController;
        aController.setModel(xModel);
        aController.setDocument(xTextDocument);

        Reference<text::XTextViewCursorSupplier> xViewCursorSupplier(
            xModel->getCurrentController(), UNO_QUERY_THROW);
        Reference<text::XTextViewCursor> xViewCursor = xViewCursorSupplier->getViewCursor();
        xViewCursor->gotoEnd(false);

        CPPUNIT_ASSERT(aController.insertAtCursor(u" jumps"_ustr));

        Reference<document::XUndoManagerSupplier> xUndoSupplier(xModel, UNO_QUERY_THROW);
        Reference<document::XUndoManager> xUndoManager = xUndoSupplier->getUndoManager();
        xUndoManager->undo();

        CPPUNIT_ASSERT_EQUAL(u"The quick brown fox"_ustr, xText->getString());
    }

    // GIVEN insertAtCursor(" jumps") has just been executed
    // WHEN getCursorContext is read
    // THEN textBefore reflects the inserted text.
    void testInsertAtCursor_cursorAfterInsert()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDocument(mxComponent, UNO_QUERY_THROW);
        Reference<text::XText> xText = xTextDocument->getText();
        xText->setString(u"The quick brown fox"_ustr);

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);

        officelabs::DocumentController aController;
        aController.setModel(xModel);
        aController.setDocument(xTextDocument);

        Reference<text::XTextViewCursorSupplier> xViewCursorSupplier(
            xModel->getCurrentController(), UNO_QUERY_THROW);
        Reference<text::XTextViewCursor> xViewCursor = xViewCursorSupplier->getViewCursor();
        xViewCursor->gotoEnd(false);

        CPPUNIT_ASSERT(aController.insertAtCursor(u" jumps"_ustr));

        const CursorContext aContext = aController.getCursorContext();
        CPPUNIT_ASSERT_EQUAL(u"The quick brown fox jumps"_ustr, aContext.textBefore);
        CPPUNIT_ASSERT_EQUAL(OUString(), aContext.textAfter);
    }

    // GIVEN change recording is enabled in a Writer document
    // WHEN insertAtCursor adds text
    // THEN at least one redline is created.
    void testInsertAtCursor_recordsRedline()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDocument(mxComponent, UNO_QUERY_THROW);
        Reference<text::XText> xText = xTextDocument->getText();
        xText->setString(u"The quick brown fox"_ustr);

        Reference<beans::XPropertySet> xPropertySet(mxComponent, UNO_QUERY_THROW);
        xPropertySet->setPropertyValue(u"RecordChanges"_ustr, uno::Any(true));

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);

        officelabs::DocumentController aController;
        aController.setModel(xModel);
        aController.setDocument(xTextDocument);

        Reference<text::XTextViewCursorSupplier> xViewCursorSupplier(
            xModel->getCurrentController(), UNO_QUERY_THROW);
        Reference<text::XTextViewCursor> xViewCursor = xViewCursorSupplier->getViewCursor();
        xViewCursor->gotoEnd(false);

        CPPUNIT_ASSERT(aController.insertAtCursor(u" jumps"_ustr));

        Reference<document::XRedlinesSupplier> xRedlinesSupplier(xModel, UNO_QUERY_THROW);
        Reference<container::XEnumerationAccess> xEnumerationAccess
            = xRedlinesSupplier->getRedlines();
        Reference<container::XEnumeration> xEnumeration = xEnumerationAccess->createEnumeration();
        CPPUNIT_ASSERT(xEnumeration.is());
        CPPUNIT_ASSERT(xEnumeration->hasMoreElements());
    }

    // GIVEN a Writer document
    // WHEN insertAtCursor is called with an empty string
    // THEN it returns false without changing the document.
    void testInsertAtCursor_emptyStringReturnsFalse()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDocument(mxComponent, UNO_QUERY_THROW);
        Reference<text::XText> xText = xTextDocument->getText();
        xText->setString(u"The quick brown fox"_ustr);

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);

        officelabs::DocumentController aController;
        aController.setModel(xModel);
        aController.setDocument(xTextDocument);

        CPPUNIT_ASSERT(!aController.insertAtCursor(u""_ustr));
        CPPUNIT_ASSERT_EQUAL(u"The quick brown fox"_ustr, xText->getString());
    }

    // GIVEN a Writer document with a protected text section
    // WHEN the view cursor is inside that section
    // THEN getCursorContext().readOnly is true and insertAtCursor returns
    // false without changing the document text.
    void testInsertAtCursor_protectedSectionIsReadOnly()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDocument(mxComponent, UNO_QUERY_THROW);
        Reference<text::XText> xText = xTextDocument->getText();
        xText->setString(u"Before section after."_ustr);

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);
        Reference<lang::XMultiServiceFactory> xFactory(xModel, UNO_QUERY_THROW);
        Reference<uno::XInterface> xSection = xFactory->createInstance(
            u"com.sun.star.text.TextSection"_ustr);
        Reference<beans::XPropertySet> xSectionProps(xSection, UNO_QUERY_THROW);
        xSectionProps->setPropertyValue(u"IsProtected"_ustr, Any(true));
        Reference<text::XTextContent> xSectionContent(xSection, UNO_QUERY_THROW);
        xText->insertTextContent(xText->getEnd(), xSectionContent, false);

        Reference<text::XTextViewCursorSupplier> xViewCursorSupplier(
            xModel->getCurrentController(), UNO_QUERY_THROW);
        Reference<text::XTextViewCursor> xViewCursor = xViewCursorSupplier->getViewCursor();
        Reference<text::XTextRange> xAnchor = xSectionContent->getAnchor();
        xViewCursor->gotoRange(xAnchor, false);

        officelabs::DocumentController aController;
        aController.setModel(xModel);
        aController.setController(xModel->getCurrentController());
        aController.setDocument(xTextDocument);

        // Inserting the section adds its own paragraph, so compare against the
        // text as it stands right before the refused insert.
        const OUString sBeforeInsert = xText->getString();
        const CursorContext aContext = aController.getCursorContext();
        CPPUNIT_ASSERT(aContext.readOnly);
        CPPUNIT_ASSERT(!aController.insertAtCursor(u" inserted"_ustr));
        CPPUNIT_ASSERT_EQUAL(sBeforeInsert, xText->getString());
    }

    // GIVEN a Writer document whose paragraph is set to "Liberation Mono" at
    // the end
    // WHEN getCursorCharFont is read
    // THEN familyName is "Liberation Mono".
    void testCursorCharFont_familyName()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDocument(mxComponent, UNO_QUERY_THROW);
        Reference<text::XText> xText = xTextDocument->getText();
        xText->setString(u"The quick brown fox"_ustr);

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);

        Reference<text::XTextViewCursorSupplier> xViewCursorSupplier(
            xModel->getCurrentController(), UNO_QUERY_THROW);
        Reference<text::XTextViewCursor> xViewCursor = xViewCursorSupplier->getViewCursor();
        xViewCursor->gotoStart(false);
        xViewCursor->gotoEnd(true);

        Reference<beans::XPropertySet> xCursorProps(xViewCursor, UNO_QUERY_THROW);
        xCursorProps->setPropertyValue(u"CharFontName"_ustr, Any(u"Liberation Mono"_ustr));
        xCursorProps->setPropertyValue(u"CharHeight"_ustr, Any(float(18)));

        xViewCursor->gotoEnd(false);

        officelabs::DocumentController aController;
        aController.setModel(xModel);
        aController.setDocument(xTextDocument);

        const std::optional<CursorCharFont> aFont = aController.getCursorCharFont();
        CPPUNIT_ASSERT(aFont.has_value());
        CPPUNIT_ASSERT_EQUAL(u"Liberation Mono"_ustr, aFont->familyName);
    }

    // GIVEN a Writer document whose paragraph is set to CharHeight 18 at the
    // end
    // WHEN getCursorCharFont is read
    // THEN heightPt is 18.
    void testCursorCharFont_heightPt()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDocument(mxComponent, UNO_QUERY_THROW);
        Reference<text::XText> xText = xTextDocument->getText();
        xText->setString(u"The quick brown fox"_ustr);

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);

        Reference<text::XTextViewCursorSupplier> xViewCursorSupplier(
            xModel->getCurrentController(), UNO_QUERY_THROW);
        Reference<text::XTextViewCursor> xViewCursor = xViewCursorSupplier->getViewCursor();
        xViewCursor->gotoStart(false);
        xViewCursor->gotoEnd(true);

        Reference<beans::XPropertySet> xCursorProps(xViewCursor, UNO_QUERY_THROW);
        xCursorProps->setPropertyValue(u"CharFontName"_ustr, Any(u"Liberation Mono"_ustr));
        xCursorProps->setPropertyValue(u"CharHeight"_ustr, Any(float(18)));

        xViewCursor->gotoEnd(false);

        officelabs::DocumentController aController;
        aController.setModel(xModel);
        aController.setDocument(xTextDocument);

        const std::optional<CursorCharFont> aFont = aController.getCursorCharFont();
        CPPUNIT_ASSERT(aFont.has_value());
        CPPUNIT_ASSERT_EQUAL(float(18), aFont->heightPt);
    }


    // GIVEN a caret at the end of a paragraph that has another paragraph below
    // it WHEN the context is fed to the eligibility gate THEN a completion is
    // still requested.
    //
    // This is the SEAM test. getCursorContext() and isEligible() each had their
    // own suite and nothing called one into the other, so #75 could widen
    // textAfter across paragraphs with 15/15 green while making isEligible --
    // which returns textAfter.trim().isEmpty() -- false for every caret except
    // the end of the last non-blank paragraph in the document.
    void testCursorContext_isEligibleMidDocument()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDocument(mxComponent, UNO_QUERY_THROW);
        Reference<text::XText> xText = xTextDocument->getText();
        xText->insertString(xText->getEnd(), u"The quick brown fox jumps over"_ustr, false);
        xText->insertControlCharacter(xText->getEnd(), text::ControlCharacter::PARAGRAPH_BREAK,
                                       false);
        xText->insertString(xText->getEnd(), u"Second para"_ustr, false);

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);
        officelabs::DocumentController aController;
        aController.setModel(xModel);
        aController.setDocument(xTextDocument);

        // Caret at the END of the FIRST paragraph -- text follows, in a later
        // paragraph. gotoStart then gotoEndOfParagraph, so this does not depend
        // on how many paragraphs the document has.
        Reference<text::XTextViewCursorSupplier> xViewCursorSupplier(
            xModel->getCurrentController(), UNO_QUERY_THROW);
        Reference<text::XTextViewCursor> xViewCursor = xViewCursorSupplier->getViewCursor();
        xViewCursor->gotoStart(false);
        // goRight by the first paragraph's length rather than XParagraphCursor:
        // the VIEW cursor does not implement XParagraphCursor, and querying it
        // throws.
        xViewCursor->goRight(
            static_cast<sal_Int16>(OUString(u"The quick brown fox jumps over"_ustr).getLength()),
            false);

        const CursorContext aContext = aController.getCursorContext();
        CPPUNIT_ASSERT(aContext.textBefore.endsWith(u"jumps over"_ustr));
        // The gate is what this test exists for.
        CPPUNIT_ASSERT_EQUAL(OUString(), aContext.textAfter);
        CPPUNIT_ASSERT(officelabs::isEligible(aContext));
    }

    // GIVEN the same caret -- end of a paragraph with another below it
    // WHEN the forward context is read
    // THEN it reaches the following paragraph AND the request body carries it,
    // while textAfter stays empty and the context stays eligible.
    //
    // The companion to testCursorContext_isEligibleMidDocument above: that one
    // pins that widening the GATE breaks the feature, this one pins that the
    // trailing context still reaches the model. Asserting both in one test is
    // the point -- they are the two halves that #75 could not satisfy at once,
    // and a test that checks only one of them is what let it merge.
    void testCursorContext_forwardContextReachesTheModel()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDocument(mxComponent, UNO_QUERY_THROW);
        Reference<text::XText> xText = xTextDocument->getText();
        xText->insertString(xText->getEnd(), u"The quick brown fox jumps over"_ustr, false);
        xText->insertControlCharacter(xText->getEnd(), text::ControlCharacter::PARAGRAPH_BREAK,
                                       false);
        xText->insertString(xText->getEnd(), u"Second para"_ustr, false);

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);
        officelabs::DocumentController aController;
        aController.setModel(xModel);
        aController.setDocument(xTextDocument);

        Reference<text::XTextViewCursorSupplier> xViewCursorSupplier(
            xModel->getCurrentController(), UNO_QUERY_THROW);
        Reference<text::XTextViewCursor> xViewCursor = xViewCursorSupplier->getViewCursor();
        xViewCursor->gotoStart(false);
        // goRight by the first paragraph's length, as the sibling test above
        // does: the VIEW cursor does not implement XParagraphCursor.
        CPPUNIT_ASSERT(xViewCursor->goRight(
            static_cast<sal_Int16>(OUString(u"The quick brown fox jumps over"_ustr).getLength()),
            false));

        const CursorContext aContext = aController.getCursorContext();

        // The gate is untouched by the widening.
        CPPUNIT_ASSERT_EQUAL(OUString(), aContext.textAfter);
        CPPUNIT_ASSERT(officelabs::isEligible(aContext));

        // ...and the model gets the following paragraph, on the wire.
        CPPUNIT_ASSERT(aContext.textAfterContext.indexOf(u"Second para") >= 0);
        CPPUNIT_ASSERT(officelabs::buildCompletionRequest(aContext).indexOf("Second para") >= 0);
    }

    // GIVEN a document with more trailing text than the 500-character budget
    // WHEN the forward context is read
    // THEN it stops AT the budget rather than overshooting by a paragraph.
    //
    // An exact bound, not a range: "under 4000" could not tell a cap from an
    // overshoot, and the loop stops BEFORE a hop that would exceed the budget,
    // so the final hop can overshoot by a whole paragraph without the clip.
    void testCursorContext_forwardContextStopsAtCharBudget()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDocument(mxComponent, UNO_QUERY_THROW);
        Reference<text::XText> xText = xTextDocument->getText();
        xText->insertString(xText->getEnd(), u"The quick brown fox jumps over"_ustr, false);
        OUStringBuffer aFillBuf(100);
        comphelper::string::padToLength(aFillBuf, 100, 'y');
        const OUString aFill = aFillBuf.makeStringAndClear();
        for (int i = 0; i < 20; ++i)
        {
            xText->insertControlCharacter(xText->getEnd(),
                                          text::ControlCharacter::PARAGRAPH_BREAK, false);
            xText->insertString(xText->getEnd(), aFill, false);
        }

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);
        officelabs::DocumentController aController;
        aController.setModel(xModel);
        aController.setDocument(xTextDocument);

        Reference<text::XTextViewCursorSupplier> xViewCursorSupplier(
            xModel->getCurrentController(), UNO_QUERY_THROW);
        Reference<text::XTextViewCursor> xViewCursor = xViewCursorSupplier->getViewCursor();
        xViewCursor->gotoStart(false);
        CPPUNIT_ASSERT(xViewCursor->goRight(
            static_cast<sal_Int16>(OUString(u"The quick brown fox jumps over"_ustr).getLength()),
            false));

        const CursorContext aContext = aController.getCursorContext();

        CPPUNIT_ASSERT_EQUAL(OUString(), aContext.textAfter);
        CPPUNIT_ASSERT_EQUAL(sal_Int32(500), aContext.textAfterContext.getLength());
    }

    CPPUNIT_TEST_SUITE(DocumentControllerCursorTest);
    CPPUNIT_TEST(testCursorContext_endOfParagraph);
    CPPUNIT_TEST(testCursorContext_afterFourChars);
    CPPUNIT_TEST(testCursorContext_atStart);
    CPPUNIT_TEST(testCursorContext_twoParagraphs);
    CPPUNIT_TEST(testCursorContext_beforeStopsAtCharBudget);
    CPPUNIT_TEST(testCursorContext_afterStopsAtParagraphEnd);
    CPPUNIT_TEST(testCursorContext_selection);
    CPPUNIT_TEST(testCursorContext_isEligibleMidDocument);
    CPPUNIT_TEST(testCursorContext_forwardContextReachesTheModel);
    CPPUNIT_TEST(testCursorContext_forwardContextStopsAtCharBudget);
    CPPUNIT_TEST(testInsertAtCursor_appendsText);
    CPPUNIT_TEST(testInsertAtCursor_undo);
    CPPUNIT_TEST(testInsertAtCursor_cursorAfterInsert);
    CPPUNIT_TEST(testInsertAtCursor_recordsRedline);
    CPPUNIT_TEST(testInsertAtCursor_emptyStringReturnsFalse);
    CPPUNIT_TEST(testInsertAtCursor_protectedSectionIsReadOnly);
    CPPUNIT_TEST(testCursorCharFont_familyName);
    CPPUNIT_TEST(testCursorCharFont_heightPt);
    CPPUNIT_TEST_SUITE_END();
};

CPPUNIT_TEST_SUITE_REGISTRATION(DocumentControllerCursorTest);
}

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
