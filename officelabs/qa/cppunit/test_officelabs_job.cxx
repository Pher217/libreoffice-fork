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

#include <com/sun/star/beans/NamedValue.hpp>
#include <com/sun/star/document/XUndoManager.hpp>
#include <com/sun/star/document/XUndoManagerSupplier.hpp>
#include <com/sun/star/frame/XModel.hpp>
#include <com/sun/star/lang/IllegalArgumentException.hpp>
#include <com/sun/star/task/XJob.hpp>
#include <com/sun/star/text/XText.hpp>
#include <com/sun/star/text/XTextDocument.hpp>
#include <com/sun/star/uno/XInterface.hpp>

using namespace css;
using namespace css::uno;

namespace
{

/// The three named values undo_agent_top returns, unpacked for assertions.
struct UndoAgentTopResult
{
    sal_Int32 nUndone = 0;
    bool bRefused = false;
    OUString sReason;
};

UndoAgentTopResult toResult(const Sequence<beans::NamedValue>& rSeq)
{
    UndoAgentTopResult aResult;
    for (const auto& rItem : rSeq)
    {
        if (rItem.Name == "Undone")
            rItem.Value >>= aResult.nUndone;
        else if (rItem.Name == "Refused")
            rItem.Value >>= aResult.bRefused;
        else if (rItem.Name == "Reason")
            rItem.Value >>= aResult.sReason;
    }
    return aResult;
}

/// GIVEN/WHEN/THEN style tests for the OfficeLabsJob undo_agent_top operation.
class OfficeLabsJobTest : public UnoApiTest
{
public:
    OfficeLabsJobTest()
        : UnoApiTest(u"/officelabs/qa/cppunit/data/"_ustr)
    {
    }

private:
    Reference<task::XJob> createJob()
    {
        Reference<XInterface> xInstance = m_xFactory->createInstanceWithContext(
            u"ai.officelabs.OfficeLabsJob"_ustr, m_xContext);
        return Reference<task::XJob>(xInstance, UNO_QUERY_THROW);
    }

    Reference<document::XUndoManager> getUndoManager()
    {
        Reference<document::XUndoManagerSupplier> xSupplier(mxComponent, UNO_QUERY_THROW);
        return xSupplier->getUndoManager();
    }

    /// Wraps a single text insertion in its own "AI Edits" undo context, so it
    /// lands on the stack as one entry titled "AI Edits" rather than whatever
    /// generic title Writer would give a bare insertString().
    void insertUnderAgentContext(const Reference<text::XText>& xText, const OUString& rText)
    {
        Reference<document::XUndoManager> xUndoManager = getUndoManager();
        xUndoManager->enterUndoContext(u"AI Edits"_ustr);
        xText->insertString(xText->getEnd(), rText, false);
        xUndoManager->leaveUndoContext();
    }

    UndoAgentTopResult callUndoAgentTop(const Reference<task::XJob>& xJob, const Any& rModel,
                                         const OUString& rContextTitle, sal_Int32 nMaxSteps)
    {
        Sequence<beans::NamedValue> aArgs{
            { u"Operation"_ustr, Any(u"undo_agent_top"_ustr) },
            { u"Model"_ustr, rModel },
            { u"ContextTitle"_ustr, Any(rContextTitle) },
            { u"MaxSteps"_ustr, Any(nMaxSteps) },
        };
        Sequence<beans::NamedValue> aOut;
        xJob->execute(aArgs) >>= aOut;
        return toResult(aOut);
    }

    // 1. Two actions inside an "AI Edits" undo context each -> undo_agent_top
    // returns Undone=2, Refused=false, and both edits are reverted. This is
    // also the test that proves the recursive-mutex assumption: execute()
    // holds one SolarMutexGuard while calling XUndoManager methods, which
    // re-acquire it via UndoManagerGuard. A deadlock here would hang the test.
    void testUndoesTwoMatchingEntries()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDoc(mxComponent, UNO_QUERY_THROW);
        Reference<text::XText> xText = xTextDoc->getText();

        insertUnderAgentContext(xText, u"one"_ustr);
        insertUnderAgentContext(xText, u"two"_ustr);
        CPPUNIT_ASSERT_EQUAL(u"onetwo"_ustr, xText->getString());

        Reference<task::XJob> xJob = createJob();
        UndoAgentTopResult aResult
            = callUndoAgentTop(xJob, Any(Reference<frame::XModel>(mxComponent, UNO_QUERY_THROW)),
                                u"AI Edits"_ustr, 10);

        CPPUNIT_ASSERT_EQUAL(sal_Int32(2), aResult.nUndone);
        CPPUNIT_ASSERT(!aResult.bRefused);
        CPPUNIT_ASSERT_EQUAL(u""_ustr, xText->getString());
    }

    // 2. A non-matching entry on top -> Undone=0, Refused=true, stack unchanged.
    void testRefusesNonMatchingTopEntry()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDoc(mxComponent, UNO_QUERY_THROW);
        Reference<text::XText> xText = xTextDoc->getText();

        // A bare insertString gets Writer's own default undo title, not ours.
        xText->insertString(xText->getEnd(), u"hello"_ustr, false);
        CPPUNIT_ASSERT_EQUAL(u"hello"_ustr, xText->getString());

        Reference<task::XJob> xJob = createJob();
        UndoAgentTopResult aResult
            = callUndoAgentTop(xJob, Any(Reference<frame::XModel>(mxComponent, UNO_QUERY_THROW)),
                                u"AI Edits"_ustr, 10);

        CPPUNIT_ASSERT_EQUAL(sal_Int32(0), aResult.nUndone);
        CPPUNIT_ASSERT(aResult.bRefused);
        CPPUNIT_ASSERT(!aResult.sReason.isEmpty());
        CPPUNIT_ASSERT_EQUAL(u"hello"_ustr, xText->getString());
    }

    // 3. MaxSteps caps the count: three matching entries, MaxSteps=2.
    void testMaxStepsCapsCount()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDoc(mxComponent, UNO_QUERY_THROW);
        Reference<text::XText> xText = xTextDoc->getText();

        insertUnderAgentContext(xText, u"one"_ustr);
        insertUnderAgentContext(xText, u"two"_ustr);
        insertUnderAgentContext(xText, u"three"_ustr);
        CPPUNIT_ASSERT_EQUAL(u"onetwothree"_ustr, xText->getString());

        Reference<task::XJob> xJob = createJob();
        UndoAgentTopResult aResult
            = callUndoAgentTop(xJob, Any(Reference<frame::XModel>(mxComponent, UNO_QUERY_THROW)),
                                u"AI Edits"_ustr, 2);

        CPPUNIT_ASSERT_EQUAL(sal_Int32(2), aResult.nUndone);
        CPPUNIT_ASSERT(!aResult.bRefused);
        CPPUNIT_ASSERT_EQUAL(u"one"_ustr, xText->getString());
    }

    // 4. Empty undo stack -> Undone=0, Refused=false.
    void testEmptyUndoStackIsNotARefusal()
    {
        loadFromURL(u"private:factory/swriter"_ustr);

        Reference<task::XJob> xJob = createJob();
        UndoAgentTopResult aResult
            = callUndoAgentTop(xJob, Any(Reference<frame::XModel>(mxComponent, UNO_QUERY_THROW)),
                                u"AI Edits"_ustr, 10);

        CPPUNIT_ASSERT_EQUAL(sal_Int32(0), aResult.nUndone);
        CPPUNIT_ASSERT(!aResult.bRefused);
    }

    // 5. Missing Model -> IllegalArgumentException.
    void testMissingMaxStepsThrows()
    {
        // MaxSteps omitted leaves the loop bound at 0, which would undo
        // nothing and report success. Refusing is the point.
        loadFromURL(u"private:factory/swriter"_ustr);
        css::uno::Reference<css::task::XJob> xJob(
            m_xFactory->createInstanceWithContext(u"ai.officelabs.OfficeLabsJob"_ustr, m_xContext),
            css::uno::UNO_QUERY_THROW);
        css::uno::Sequence<css::beans::NamedValue> aArgs{
            { u"Operation"_ustr, css::uno::Any(u"undo_agent_top"_ustr) },
            { u"Model"_ustr, css::uno::Any(mxComponent) },
            { u"ContextTitle"_ustr, css::uno::Any(u"AI Edits"_ustr) },
        };
        CPPUNIT_ASSERT_THROW(xJob->execute(aArgs), css::lang::IllegalArgumentException);
    }

    void testMissingModelThrows()
    {
        loadFromURL(u"private:factory/swriter"_ustr);

        Reference<task::XJob> xJob = createJob();
        Sequence<beans::NamedValue> aArgs{
            { u"Operation"_ustr, Any(u"undo_agent_top"_ustr) },
            { u"ContextTitle"_ustr, Any(u"AI Edits"_ustr) },
            { u"MaxSteps"_ustr, Any(sal_Int32(10)) },
        };

        CPPUNIT_ASSERT_THROW(xJob->execute(aArgs), lang::IllegalArgumentException);
    }

    CPPUNIT_TEST_SUITE(OfficeLabsJobTest);
    CPPUNIT_TEST(testUndoesTwoMatchingEntries);
    CPPUNIT_TEST(testRefusesNonMatchingTopEntry);
    CPPUNIT_TEST(testMaxStepsCapsCount);
    CPPUNIT_TEST(testEmptyUndoStackIsNotARefusal);
    CPPUNIT_TEST(testMissingModelThrows);
    CPPUNIT_TEST(testMissingMaxStepsThrows);
    CPPUNIT_TEST_SUITE_END();
};

CPPUNIT_TEST_SUITE_REGISTRATION(OfficeLabsJobTest);

} // namespace

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
