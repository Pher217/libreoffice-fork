/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <test/unoapi_test.hxx>

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

using namespace css;
using namespace css::uno;
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
    // THEN textBefore is paragraph-local "Second para".
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
        CPPUNIT_ASSERT_EQUAL(u"Second para"_ustr, aContext.textBefore);
        CPPUNIT_ASSERT_EQUAL(OUString(), aContext.textAfter);
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

    CPPUNIT_TEST_SUITE(DocumentControllerCursorTest);
    CPPUNIT_TEST(testCursorContext_endOfParagraph);
    CPPUNIT_TEST(testCursorContext_afterFourChars);
    CPPUNIT_TEST(testCursorContext_atStart);
    CPPUNIT_TEST(testCursorContext_twoParagraphs);
    CPPUNIT_TEST(testCursorContext_selection);
    CPPUNIT_TEST(testInsertAtCursor_appendsText);
    CPPUNIT_TEST(testInsertAtCursor_undo);
    CPPUNIT_TEST(testInsertAtCursor_cursorAfterInsert);
    CPPUNIT_TEST(testInsertAtCursor_recordsRedline);
    CPPUNIT_TEST(testInsertAtCursor_emptyStringReturnsFalse);
    CPPUNIT_TEST(testInsertAtCursor_protectedSectionIsReadOnly);
    CPPUNIT_TEST_SUITE_END();
};

CPPUNIT_TEST_SUITE_REGISTRATION(DocumentControllerCursorTest);
}

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
