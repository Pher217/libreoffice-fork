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
#include <com/sun/star/document/UndoFailedException.hpp>
#include <com/sun/star/document/XUndoAction.hpp>
#include <com/sun/star/document/XUndoManager.hpp>
#include <com/sun/star/document/XUndoManagerSupplier.hpp>
#include <com/sun/star/frame/XModel.hpp>
#include <com/sun/star/lang/IllegalArgumentException.hpp>
#include <com/sun/star/task/XJob.hpp>
#include <com/sun/star/text/XText.hpp>
#include <com/sun/star/text/XTextDocument.hpp>
#include <com/sun/star/uno/XInterface.hpp>

#include <cppuhelper/implbase.hxx>
#include <osl/thread.hxx>
#include <vcl/svapp.hxx>

#include <atomic>
#include <chrono>
#include <exception>
#include <thread>
#include <utility>

using namespace css;
using namespace css::uno;

namespace
{

/// The three named values undo_agent_top returns, unpacked for assertions.
struct UndoAgentTopResult
{
    sal_Int32 nUndone = 0;
    bool bRefused = false;
    bool bStackCleared = false;
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
        else if (rItem.Name == "UndoStackCleared")
            rItem.Value >>= aResult.bStackCleared;
        else if (rItem.Name == "Reason")
            rItem.Value >>= aResult.sReason;
    }
    return aResult;
}

/// An undo action that always fails, carrying the agent's context title so the
/// walk accepts it and then trips over it. There is no way to make a real
/// Writer undo fail on demand, and the partial-count path cannot be tested
/// without one.
class ThrowingUndoAction final : public cppu::WeakImplHelper<document::XUndoAction>
{
    OUString m_sTitle;

public:
    explicit ThrowingUndoAction(OUString sTitle)
        : m_sTitle(std::move(sTitle))
    {
    }

    OUString SAL_CALL getTitle() override { return m_sTitle; }
    void SAL_CALL undo() override
    {
        throw document::UndoFailedException(u"this action always fails"_ustr, *this, Any());
    }
    void SAL_CALL redo() override {}
};

/// An undo action that records which thread actually ran it. This is the only
/// way to observe the marshalling from inside a test: everything else about
/// undo_agent_top looks identical whether or not it hops to the solar thread.
class RecordingUndoAction final : public cppu::WeakImplHelper<document::XUndoAction>
{
    OUString m_sTitle;
    std::atomic<oslThreadIdentifier> m_nThread{ 0 };

public:
    explicit RecordingUndoAction(OUString sTitle)
        : m_sTitle(std::move(sTitle))
    {
    }

    oslThreadIdentifier getRecordedThread() const { return m_nThread.load(); }

    OUString SAL_CALL getTitle() override { return m_sTitle; }
    void SAL_CALL undo() override { m_nThread = osl::Thread::getCurrentIdentifier(); }
    void SAL_CALL redo() override {}
};

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

    /// Runs callUndoAgentTop on a worker thread while this (solar) thread pumps,
    /// which is the only arrangement that reaches the marshalling at all.
    ///
    /// Reschedule() and not Yield(): Yield() waits for input and would block
    /// here once the last event has been dispatched. The deadline turns the
    /// failure this covers -- a hang -- into a named assertion instead of a
    /// test runner that never returns.
    UndoAgentTopResult callUndoAgentTopFromWorker(const Reference<task::XJob>& xJob,
                                                   const Any& rModel,
                                                   const OUString& rContextTitle,
                                                   sal_Int32 nMaxSteps)
    {
        std::atomic<bool> bFinished(false);
        UndoAgentTopResult aResult;
        std::exception_ptr aWorkerException;

        std::thread aWorker([&] {
            try
            {
                aResult = callUndoAgentTop(xJob, rModel, rContextTitle, nMaxSteps);
            }
            catch (...)
            {
                aWorkerException = std::current_exception();
            }
            bFinished = true;
        });

        {
            SolarMutexGuard aGuard;
            const auto aDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            while (!bFinished.load())
            {
                CPPUNIT_ASSERT_MESSAGE("undo_agent_top did not return within 30s when called "
                                       "from a non-solar thread",
                                       std::chrono::steady_clock::now() < aDeadline);
                Application::Reschedule(true);
            }
        }
        aWorker.join();

        if (aWorkerException)
            std::rethrow_exception(aWorkerException);
        return aResult;
    }

    // 1. Two actions inside an "AI Edits" undo context each -> undo_agent_top
    // returns Undone=2, Refused=false, and both edits are reverted.
    //
    // Like every test here except the last two, this one calls from the solar
    // thread, where syncExecute short-circuits to a direct call
    // (vcl/source/helper/threadex.cxx:44-49). It says nothing about threading.
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

    // 5. MaxSteps omitted -> IllegalArgumentException.
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

    // 7. GIVEN an undo context left open on the document, WHEN undo_agent_top
    // runs, THEN it refuses and says why -- rather than reporting Undone=0 as
    // a success, which is what isUndoPossible() alone would have produced.
    void testOpenUndoContextIsRefusedNotReportedAsSuccess()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDoc(mxComponent, UNO_QUERY_THROW);
        Reference<text::XText> xText = xTextDoc->getText();

        insertUnderAgentContext(xText, u"one"_ustr);

        // Enter a context and deliberately do not leave it.
        Reference<document::XUndoManager> xUndoManager = getUndoManager();
        xUndoManager->enterUndoContext(u"someone else is mid-edit"_ustr);
        xText->insertString(xText->getEnd(), u"two"_ustr, false);
        CPPUNIT_ASSERT(!xUndoManager->isUndoPossible());

        Reference<task::XJob> xJob = createJob();
        UndoAgentTopResult aResult
            = callUndoAgentTop(xJob, Any(Reference<frame::XModel>(mxComponent, UNO_QUERY_THROW)),
                                u"AI Edits"_ustr, 10);

        CPPUNIT_ASSERT_EQUAL(sal_Int32(0), aResult.nUndone);
        CPPUNIT_ASSERT(aResult.bRefused);
        CPPUNIT_ASSERT(aResult.sReason.indexOf("undo context is open") >= 0);
        // Nothing was touched.
        CPPUNIT_ASSERT_EQUAL(u"onetwo"_ustr, xText->getString());

        xUndoManager->leaveUndoContext();
    }

    // 8. GIVEN one of our entries undoes cleanly and the next one throws,
    // WHEN undo_agent_top runs, THEN the caller still learns that one step
    // happened. Letting the exception out would have replaced the count, and
    // the count is exactly what the caller cannot reconstruct afterwards.
    void testUndoFailureKeepsThePartialCount()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDoc(mxComponent, UNO_QUERY_THROW);
        Reference<text::XText> xText = xTextDoc->getText();

        // Bottom of the stack: an action that will refuse to be undone.
        getUndoManager()->addUndoAction(new ThrowingUndoAction(u"AI Edits"_ustr));
        // Top of the stack: a real edit of ours, which undoes fine.
        insertUnderAgentContext(xText, u"one"_ustr);
        CPPUNIT_ASSERT_EQUAL(u"one"_ustr, xText->getString());

        Reference<task::XJob> xJob = createJob();
        UndoAgentTopResult aResult
            = callUndoAgentTop(xJob, Any(Reference<frame::XModel>(mxComponent, UNO_QUERY_THROW)),
                                u"AI Edits"_ustr, 10);

        CPPUNIT_ASSERT_EQUAL(sal_Int32(1), aResult.nUndone);
        CPPUNIT_ASSERT(aResult.bRefused);
        CPPUNIT_ASSERT(aResult.sReason.indexOf("after 1 step(s)") >= 0);
        CPPUNIT_ASSERT_EQUAL(u""_ustr, xText->getString());

        // The part that matters more than the count. A failed undo does not
        // merely stop -- SfxUndoManager::Undo calls ImplClearUndo() and rethrows
        // (svl/source/undo/undo.cxx:744-752), which XUndoManager.idl:177-179
        // states as a guarantee. The whole stack is gone, and the result has to
        // say so or the caller will believe it can still undo.
        CPPUNIT_ASSERT(aResult.bStackCleared);
        CPPUNIT_ASSERT(aResult.sReason.indexOf("CLEARED THE WHOLE UNDO STACK") >= 0);
        CPPUNIT_ASSERT(!getUndoManager()->getAllUndoActionTitles().hasElements());
    }

    // 9. GIVEN the call arrives on a thread that is not the solar thread --
    // which is how it always arrives in production, over URP -- WHEN
    // undo_agent_top runs, THEN it completes and the edits are reverted.
    void testRunsWhenCalledFromANonSolarThread()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDoc(mxComponent, UNO_QUERY_THROW);
        Reference<text::XText> xText = xTextDoc->getText();

        insertUnderAgentContext(xText, u"one"_ustr);
        insertUnderAgentContext(xText, u"two"_ustr);
        CPPUNIT_ASSERT_EQUAL(u"onetwo"_ustr, xText->getString());

        Reference<task::XJob> xJob = createJob();
        UndoAgentTopResult aResult = callUndoAgentTopFromWorker(
            xJob, Any(Reference<frame::XModel>(mxComponent, UNO_QUERY_THROW)), u"AI Edits"_ustr, 10);

        CPPUNIT_ASSERT_EQUAL(sal_Int32(2), aResult.nUndone);
        CPPUNIT_ASSERT(!aResult.bRefused);
        CPPUNIT_ASSERT_EQUAL(u""_ustr, xText->getString());
    }

    // 10. GIVEN the call arrives on a non-solar thread, WHEN an undo action
    // runs, THEN it runs ON THE SOLAR THREAD.
    //
    // This is the test that distinguishes the two implementations, and the
    // reason it exists: without it, deleting the syncExecute hop and calling
    // doUndoAgentTop directly leaves the whole suite green, so the marshalling
    // would be load-bearing in intent and unpinned in fact. Here the direct
    // call records the worker's id and this fails; the marshalled call records
    // the solar thread's and it passes.
    //
    // It pins the behaviour. It does not, and cannot, show the behaviour is
    // necessary -- see the comment block in OfficeLabsJob.cxx, which is blunt
    // about what the hop does and does not buy.
    void testUndoRunsOnTheSolarThread()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        CPPUNIT_ASSERT_MESSAGE("the fixture itself must be on the solar thread, or this "
                               "test compares two of the same thing",
                               Application::IsMainThread());
        const oslThreadIdentifier nSolarThread = osl::Thread::getCurrentIdentifier();

        rtl::Reference<RecordingUndoAction> pAction(new RecordingUndoAction(u"AI Edits"_ustr));
        getUndoManager()->addUndoAction(pAction);

        Reference<task::XJob> xJob = createJob();
        UndoAgentTopResult aResult = callUndoAgentTopFromWorker(
            xJob, Any(Reference<frame::XModel>(mxComponent, UNO_QUERY_THROW)), u"AI Edits"_ustr, 1);

        CPPUNIT_ASSERT_EQUAL(sal_Int32(1), aResult.nUndone);
        CPPUNIT_ASSERT_MESSAGE("the undo action never ran at all",
                               pAction->getRecordedThread() != 0);
        CPPUNIT_ASSERT_EQUAL(nSolarThread, pAction->getRecordedThread());
    }

    CPPUNIT_TEST_SUITE(OfficeLabsJobTest);
    CPPUNIT_TEST(testUndoesTwoMatchingEntries);
    CPPUNIT_TEST(testRefusesNonMatchingTopEntry);
    CPPUNIT_TEST(testMaxStepsCapsCount);
    CPPUNIT_TEST(testEmptyUndoStackIsNotARefusal);
    CPPUNIT_TEST(testMissingModelThrows);
    CPPUNIT_TEST(testMissingMaxStepsThrows);
    CPPUNIT_TEST(testOpenUndoContextIsRefusedNotReportedAsSuccess);
    CPPUNIT_TEST(testUndoFailureKeepsThePartialCount);
    CPPUNIT_TEST(testRunsWhenCalledFromANonSolarThread);
    CPPUNIT_TEST(testUndoRunsOnTheSolarThread);
    CPPUNIT_TEST_SUITE_END();
};

CPPUNIT_TEST_SUITE_REGISTRATION(OfficeLabsJobTest);

} // namespace

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
