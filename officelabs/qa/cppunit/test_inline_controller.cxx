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

#include <atomic>
#include <memory>
#include <optional>

#include <com/sun/star/awt/Key.hpp>
#include <com/sun/star/awt/KeyEvent.hpp>
#include <com/sun/star/awt/KeyModifier.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/frame/XController.hpp>
#include <com/sun/star/frame/XModel.hpp>
#include <com/sun/star/lang/XMultiServiceFactory.hpp>
#include <com/sun/star/text/XText.hpp>
#include <com/sun/star/text/XTextCursor.hpp>
#include <com/sun/star/text/XTextDocument.hpp>
#include <com/sun/star/text/XTextViewCursor.hpp>
#include <com/sun/star/text/XTextViewCursorSupplier.hpp>

#include <officelabs/InlineCompletionController.hxx>
#include <osl/thread.hxx>
#include <com/sun/star/awt/XVclWindowPeer.hpp>

#include <sfx2/viewsh.hxx>
#include <tools/gen.hxx>
#include <vcl/scheduler.hxx>
#include <vcl/settings.hxx>
#include <vcl/svapp.hxx>
#include <vcl/window.hxx>

#include <thread>

using namespace css;
using namespace css::uno;

namespace
{

/// GIVEN/WHEN/THEN style tests for the inline-ghost completion controller.
class InlineCompletionControllerTest : public UnoApiTest
{
public:
    InlineCompletionControllerTest()
        : UnoApiTest(u"/officelabs/qa/cppunit/data/"_ustr)
    {
    }

private:
    /// Build a KeyEvent whose Source is the edit window's peer.
    css::awt::KeyEvent makeKeyEvent(vcl::Window* pEditWin, sal_Int16 nKeyCode,
                                    sal_Int16 nModifiers = 0)
    {
        css::awt::KeyEvent aEvent;
        if (pEditWin)
            aEvent.Source = pEditWin->GetComponentInterface(false);
        aEvent.KeyCode = nKeyCode;
        aEvent.Modifiers = nModifiers;
        return aEvent;
    }

    /// Spin the VCL event loop until the controller is no longer waiting for
    /// a worker response (or a safety limit is reached).
    void drainUntilIdle(officelabs::InlineCompletionController* pController)
    {
        for (int i = 0; i < 200 && pController->isInFlight(); ++i)
        {
            Scheduler::ProcessEventsToIdle();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    void setTextAndGotoEnd(const Reference<text::XTextDocument>& xTextDoc)
    {
        Reference<text::XText> xText = xTextDoc->getText();
        xText->setString(u"The quick brown fox"_ustr);

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);
        Reference<text::XTextViewCursorSupplier> xViewCursorSupplier(
            xModel->getCurrentController(), UNO_QUERY_THROW);
        Reference<text::XTextViewCursor> xViewCursor = xViewCursorSupplier->getViewCursor();
        xViewCursor->gotoEnd(false);
    }

    vcl::Window* getEditWindow()
    {
        SfxViewShell* pViewShell = SfxViewShell::Current();
        if (!pViewShell)
            return nullptr;
        return pViewShell->GetWindow();
    }

    /// Returns an injected caret rectangle provider that keeps the edit window
    /// large enough for the clamp inside GhostTextWindow::showAt().
    officelabs::InlineCompletionController::CaretProvider makeCaretProvider(vcl::Window* pEditWin)
    {
        if (pEditWin && pEditWin->GetOutputSizePixel().Width() < 100)
            pEditWin->SetSizePixel(Size(800, 600));
        return []() { return std::optional<tools::Rectangle>(
                          tools::Rectangle(Point(10, 10), Size(1, 16))); };
    }

    officelabs::InlineCompletionController::EnabledProvider makeEnabledProvider(bool bEnabled = true)
    {
        return [bEnabled]() { return bEnabled; };
    }

    // 1. Fetcher returns 200 {"suggestions":[{"text":" jumps"}]} → after
    // requestNow+deliver, pendingSuggestion() equals " jumps" because the
    // injected caret provider lets the ghost window show.
    void testAcceptSuggestion()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDoc(mxComponent, UNO_QUERY_THROW);
        setTextAndGotoEnd(xTextDoc);

        int nCalls = 0;
        officelabs::InlineCompletionController::FetchResult aResponse;
        aResponse.nStatus = 200;
        aResponse.aBody = R"({"suggestions":[{"text":" jumps"}]})";
        officelabs::InlineCompletionController::Fetcher aFetcher =
            [&nCalls, aResponse](const OString& /*rBody*/) mutable {
                ++nCalls;
                return aResponse;
            };

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);
        vcl::Window* pEditWin = getEditWindow();
        rtl::Reference<officelabs::InlineCompletionController> xController(
            new officelabs::InlineCompletionController(
                xModel->getCurrentController(), xModel, pEditWin, aFetcher,
                makeCaretProvider(pEditWin), makeEnabledProvider()));
        xController->start();

        xController->requestNow();
        drainUntilIdle(xController.get());

        CPPUNIT_ASSERT_EQUAL(u" jumps"_ustr, xController->pendingSuggestion());
        CPPUNIT_ASSERT(xController->isGhostVisible());

        xController->dispose();
    }

    // 2. Given a pending/visible suggestion, keyPressed(Tab) returns true and
    // the document text becomes "The quick brown fox jumps".
    void testTabAcceptsSuggestion()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDoc(mxComponent, UNO_QUERY_THROW);
        setTextAndGotoEnd(xTextDoc);

        officelabs::InlineCompletionController::FetchResult aResponse;
        aResponse.nStatus = 200;
        aResponse.aBody = R"({"suggestions":[{"text":" jumps"}]})";
        officelabs::InlineCompletionController::Fetcher aFetcher =
            [aResponse](const OString& /*rBody*/) mutable { return aResponse; };

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);
        vcl::Window* pEditWin = getEditWindow();
        rtl::Reference<officelabs::InlineCompletionController> xController(
            new officelabs::InlineCompletionController(
                xModel->getCurrentController(), xModel, pEditWin, aFetcher,
                makeCaretProvider(pEditWin), makeEnabledProvider()));
        xController->start();

        xController->requestNow();
        drainUntilIdle(xController.get());

        css::awt::KeyEvent aTab = makeKeyEvent(pEditWin, css::awt::Key::TAB);
        sal_Bool bHandled = xController->keyPressed(aTab);
        CPPUNIT_ASSERT(bHandled);

        Reference<text::XText> xText = xTextDoc->getText();
        CPPUNIT_ASSERT_EQUAL(u"The quick brown fox jumps"_ustr, xText->getString());

        xController->dispose();
    }

    // 3. keyPressed(Tab) with no suggestion returns false and text unchanged.
    // GIVEN a shown suggestion WHEN the document changes underneath it with no
    // key event at all -- a sidebar applyEdit, an agent UNO edit, a dialog's
    // replace-all -- THEN Tab does not insert the now-stale suggestion.
    //
    // The caret provider here is constant, so the tracking timer cannot hide
    // the ghost first: this isolates the Tab-time check itself.
    void testTabDoesNotInsertAStaleSuggestion()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDoc(mxComponent, UNO_QUERY_THROW);
        setTextAndGotoEnd(xTextDoc);

        officelabs::InlineCompletionController::FetchResult aResponse;
        aResponse.nStatus = 200;
        aResponse.aBody = R"({"suggestions":[{"text":" jumps"}]})";
        officelabs::InlineCompletionController::Fetcher aFetcher =
            [aResponse](const OString& /*rBody*/) mutable { return aResponse; };

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);
        vcl::Window* pEditWin = getEditWindow();
        rtl::Reference<officelabs::InlineCompletionController> xController(
            new officelabs::InlineCompletionController(
                xModel->getCurrentController(), xModel, pEditWin, aFetcher,
                makeCaretProvider(pEditWin), makeEnabledProvider()));
        xController->start();

        xController->requestNow();
        drainUntilIdle(xController.get());
        CPPUNIT_ASSERT(xController->isGhostVisible());

        // Nobody typed; the document changed anyway.
        Reference<text::XText> xText = xTextDoc->getText();
        xText->setString(u"The quick brown fox runs"_ustr);
        Reference<text::XTextViewCursorSupplier> xViewCursorSupplier(
            xModel->getCurrentController(), UNO_QUERY_THROW);
        xViewCursorSupplier->getViewCursor()->gotoEnd(false);

        css::awt::KeyEvent aTab = makeKeyEvent(pEditWin, css::awt::Key::TAB);
        sal_Bool bHandled = xController->keyPressed(aTab);

        // The keystroke is consumed, so Writer cannot insert a literal tab.
        CPPUNIT_ASSERT(bHandled);
        CPPUNIT_ASSERT_EQUAL(u"The quick brown fox runs"_ustr, xText->getString());
        CPPUNIT_ASSERT(!xController->isGhostVisible());

        xController->dispose();
    }

    void testTabWithoutSuggestionDoesNothing()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDoc(mxComponent, UNO_QUERY_THROW);
        setTextAndGotoEnd(xTextDoc);

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);
        vcl::Window* pEditWin = getEditWindow();
        rtl::Reference<officelabs::InlineCompletionController> xController(
            new officelabs::InlineCompletionController(
                xModel->getCurrentController(), xModel, pEditWin,
                [](const OString& /*rBody*/) {
                    return officelabs::InlineCompletionController::FetchResult{};
                },
                makeCaretProvider(pEditWin), makeEnabledProvider()));
        xController->start();

        css::awt::KeyEvent aTab = makeKeyEvent(pEditWin, css::awt::Key::TAB);
        sal_Bool bHandled = xController->keyPressed(aTab);
        CPPUNIT_ASSERT(!bHandled);

        Reference<text::XText> xText = xTextDoc->getText();
        CPPUNIT_ASSERT_EQUAL(u"The quick brown fox"_ustr, xText->getString());

        xController->dispose();
    }

    // 4. Key event whose Source is a different object (the XModel) → keyPressed
    // returns false and does not bump anything (a following delivered result
    // still shows).
    void testForeignSourceKeyIgnored()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDoc(mxComponent, UNO_QUERY_THROW);
        setTextAndGotoEnd(xTextDoc);

        int nCalls = 0;
        officelabs::InlineCompletionController::FetchResult aResponse;
        aResponse.nStatus = 200;
        aResponse.aBody = R"({"suggestions":[{"text":" jumps"}]})";
        officelabs::InlineCompletionController::Fetcher aFetcher =
            [&nCalls, aResponse](const OString& /*rBody*/) mutable {
                ++nCalls;
                return aResponse;
            };

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);
        vcl::Window* pEditWin = getEditWindow();
        rtl::Reference<officelabs::InlineCompletionController> xController(
            new officelabs::InlineCompletionController(
                xModel->getCurrentController(), xModel, pEditWin, aFetcher,
                makeCaretProvider(pEditWin), makeEnabledProvider()));
        xController->start();

        xController->requestNow();

        css::awt::KeyEvent aForeign;
        aForeign.Source = xModel; // not the edit window
        aForeign.KeyCode = css::awt::Key::A;
        aForeign.Modifiers = 0;
        sal_Bool bHandled = xController->keyPressed(aForeign);
        CPPUNIT_ASSERT(!bHandled);

        drainUntilIdle(xController.get());

        CPPUNIT_ASSERT_EQUAL(1, nCalls);
        CPPUNIT_ASSERT_EQUAL(u" jumps"_ustr, xController->pendingSuggestion());

        xController->dispose();
    }

    // 5. Stale generation: requestNow, then keyPressed(letter 'x') before
    // delivering → after delivery no suggestion.
    void testStaleGenerationCleared()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDoc(mxComponent, UNO_QUERY_THROW);
        setTextAndGotoEnd(xTextDoc);

        std::atomic<bool> bSlow(false);
        officelabs::InlineCompletionController::FetchResult aResponse;
        aResponse.nStatus = 200;
        aResponse.aBody = R"({"suggestions":[{"text":" jumps"}]})";
        officelabs::InlineCompletionController::Fetcher aFetcher =
            [&bSlow, aResponse](const OString& /*rBody*/) {
                bSlow = true;
                while (bSlow.load())
                    osl::Thread::wait(std::chrono::milliseconds(10));
                return aResponse;
            };

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);
        vcl::Window* pEditWin = getEditWindow();
        rtl::Reference<officelabs::InlineCompletionController> xController(
            new officelabs::InlineCompletionController(
                xModel->getCurrentController(), xModel, pEditWin, aFetcher,
                makeCaretProvider(pEditWin), makeEnabledProvider()));
        xController->start();

        xController->requestNow();

        css::awt::KeyEvent aX = makeKeyEvent(pEditWin, css::awt::Key::X);
        xController->keyPressed(aX);

        bSlow = false; // let the worker finish
        drainUntilIdle(xController.get());

        CPPUNIT_ASSERT(xController->pendingSuggestion().isEmpty());

        xController->dispose();
    }

    // 6. Changed text: requestNow, append " x" to the document before
    // delivering → no suggestion.
    void testChangedTextCancelsSuggestion()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDoc(mxComponent, UNO_QUERY_THROW);
        setTextAndGotoEnd(xTextDoc);

        std::atomic<bool> bSlow(false);
        officelabs::InlineCompletionController::FetchResult aResponse;
        aResponse.nStatus = 200;
        aResponse.aBody = R"({"suggestions":[{"text":" jumps"}]})";
        officelabs::InlineCompletionController::Fetcher aFetcher =
            [&bSlow, aResponse](const OString& /*rBody*/) {
                bSlow = true;
                while (bSlow.load())
                    osl::Thread::wait(std::chrono::milliseconds(10));
                return aResponse;
            };

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);
        Reference<text::XTextViewCursorSupplier> xViewCursorSupplier(
            xModel->getCurrentController(), UNO_QUERY_THROW);
        Reference<text::XTextViewCursor> xViewCursor = xViewCursorSupplier->getViewCursor();

        vcl::Window* pEditWin = getEditWindow();
        rtl::Reference<officelabs::InlineCompletionController> xController(
            new officelabs::InlineCompletionController(
                xModel->getCurrentController(), xModel, pEditWin, aFetcher,
                makeCaretProvider(pEditWin), makeEnabledProvider()));
        xController->start();

        xController->requestNow();

        xViewCursor->getText()->insertString(
            xViewCursor->getEnd(), u" x"_ustr, false);

        bSlow = false;
        drainUntilIdle(xController.get());

        CPPUNIT_ASSERT(xController->pendingSuggestion().isEmpty());

        xController->dispose();
    }

    // 7. Three 500 results → fourth requestNow does not call the fetcher
    // (call count stays 3).
    void testBackoffAfterThreeFailures()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDoc(mxComponent, UNO_QUERY_THROW);
        setTextAndGotoEnd(xTextDoc);

        int nCalls = 0;
        officelabs::InlineCompletionController::FetchResult aResponse;
        aResponse.nStatus = 500;
        aResponse.aBody = "error";
        officelabs::InlineCompletionController::Fetcher aFetcher =
            [&nCalls, aResponse](const OString& /*rBody*/) mutable {
                ++nCalls;
                return aResponse;
            };

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);
        vcl::Window* pEditWin = getEditWindow();
        rtl::Reference<officelabs::InlineCompletionController> xController(
            new officelabs::InlineCompletionController(
                xModel->getCurrentController(), xModel, pEditWin, aFetcher,
                makeCaretProvider(pEditWin), makeEnabledProvider()));
        xController->start();

        for (int i = 0; i < 3; ++i)
        {
            xController->requestNow();
            drainUntilIdle(xController.get());
        }

        // The fourth request should be blocked by backoff.
        xController->requestNow();
        drainUntilIdle(xController.get());

        CPPUNIT_ASSERT_EQUAL(3, nCalls);

        xController->dispose();
    }

    // 8. dispose() while in flight, then deliver events → no crash and fetcher
    // called once (use a fetcher that sleeps 50ms).
    void testDisposeWhileInFlightSafe()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDoc(mxComponent, UNO_QUERY_THROW);
        setTextAndGotoEnd(xTextDoc);

        int nCalls = 0;
        officelabs::InlineCompletionController::Fetcher aFetcher =
            [&nCalls](const OString& /*rBody*/) {
                ++nCalls;
                osl::Thread::wait(std::chrono::milliseconds(50));
                return officelabs::InlineCompletionController::FetchResult{200, R"({"suggestions":[]})"};
            };

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);
        vcl::Window* pEditWin = getEditWindow();
        rtl::Reference<officelabs::InlineCompletionController> xController(
            new officelabs::InlineCompletionController(
                xModel->getCurrentController(), xModel, pEditWin, aFetcher,
                makeCaretProvider(pEditWin), makeEnabledProvider()));
        xController->start();

        xController->requestNow();
        osl::Thread::wait(std::chrono::milliseconds(10));
        xController->dispose();

        // Drain the event loop to make sure the worker callback, if posted,
        // is processed. The shared pointer was nulled, so it must be a no-op.
        for (int i = 0; i < 50; ++i)
        {
            Scheduler::ProcessEventsToIdle();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        CPPUNIT_ASSERT_EQUAL(1, nCalls);

        xController->dispose();
    }

    // 9. Ineligible (cursor in the middle via gotoStart+goRight(4)) → requestNow
    // does not call the fetcher.
    void testIneligibleNoRequest()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDoc(mxComponent, UNO_QUERY_THROW);
        Reference<text::XText> xText = xTextDoc->getText();
        xText->setString(u"The quick brown fox"_ustr);

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);
        Reference<text::XTextViewCursorSupplier> xViewCursorSupplier(
            xModel->getCurrentController(), UNO_QUERY_THROW);
        Reference<text::XTextViewCursor> xViewCursor = xViewCursorSupplier->getViewCursor();
        xViewCursor->gotoStart(false);
        CPPUNIT_ASSERT(xViewCursor->goRight(4, false));

        int nCalls = 0;
        officelabs::InlineCompletionController::Fetcher aFetcher =
            [&nCalls](const OString& /*rBody*/) {
                ++nCalls;
                return officelabs::InlineCompletionController::FetchResult{};
            };

        vcl::Window* pEditWin = getEditWindow();
        rtl::Reference<officelabs::InlineCompletionController> xController(
            new officelabs::InlineCompletionController(
                xModel->getCurrentController(), xModel, pEditWin, aFetcher,
                makeCaretProvider(pEditWin), makeEnabledProvider()));
        xController->start();

        xController->requestNow();
        drainUntilIdle(xController.get());

        CPPUNIT_ASSERT_EQUAL(0, nCalls);

        xController->dispose();
    }

    // 10. Escape with a suggestion → keyPressed returns true, suggestion cleared.
    void testEscapeClearsSuggestion()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDoc(mxComponent, UNO_QUERY_THROW);
        setTextAndGotoEnd(xTextDoc);

        officelabs::InlineCompletionController::FetchResult aResponse;
        aResponse.nStatus = 200;
        aResponse.aBody = R"({"suggestions":[{"text":" jumps"}]})";
        officelabs::InlineCompletionController::Fetcher aFetcher =
            [aResponse](const OString& /*rBody*/) mutable { return aResponse; };

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);
        vcl::Window* pEditWin = getEditWindow();
        rtl::Reference<officelabs::InlineCompletionController> xController(
            new officelabs::InlineCompletionController(
                xModel->getCurrentController(), xModel, pEditWin, aFetcher,
                makeCaretProvider(pEditWin), makeEnabledProvider()));
        xController->start();

        xController->requestNow();
        drainUntilIdle(xController.get());

        CPPUNIT_ASSERT(!xController->pendingSuggestion().isEmpty());

        css::awt::KeyEvent aEscape = makeKeyEvent(pEditWin, css::awt::Key::ESCAPE);
        sal_Bool bHandled = xController->keyPressed(aEscape);
        CPPUNIT_ASSERT(bHandled);
        CPPUNIT_ASSERT(xController->pendingSuggestion().isEmpty());

        xController->dispose();
    }

    // 11. Provider returns nullopt → a successful result does not become a
    // suggestion and Tab returns false.
    void testNoShowWhenCaretUnavailable()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDoc(mxComponent, UNO_QUERY_THROW);
        setTextAndGotoEnd(xTextDoc);

        officelabs::InlineCompletionController::FetchResult aResponse;
        aResponse.nStatus = 200;
        aResponse.aBody = R"({"suggestions":[{"text":" jumps"}]})";
        officelabs::InlineCompletionController::Fetcher aFetcher =
            [aResponse](const OString& /*rBody*/) mutable { return aResponse; };

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);
        vcl::Window* pEditWin = getEditWindow();
        officelabs::InlineCompletionController::CaretProvider aProvider =
            []() { return std::optional<tools::Rectangle>(); };
        rtl::Reference<officelabs::InlineCompletionController> xController(
            new officelabs::InlineCompletionController(
                xModel->getCurrentController(), xModel, pEditWin, aFetcher, aProvider,
                makeEnabledProvider()));
        xController->start();

        xController->requestNow();
        drainUntilIdle(xController.get());

        CPPUNIT_ASSERT(xController->pendingSuggestion().isEmpty());
        CPPUNIT_ASSERT(!xController->isGhostVisible());

        css::awt::KeyEvent aTab = makeKeyEvent(pEditWin, css::awt::Key::TAB);
        sal_Bool bHandled = xController->keyPressed(aTab);
        CPPUNIT_ASSERT(!bHandled);

        xController->dispose();
    }

    // 12. When the enabled provider returns false, requestNow does not call the
    // fetcher and keeps the ghost hidden.
    void testDisabledProviderNoRequest()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDoc(mxComponent, UNO_QUERY_THROW);
        setTextAndGotoEnd(xTextDoc);

        int nCalls = 0;
        officelabs::InlineCompletionController::Fetcher aFetcher =
            [&nCalls](const OString& /*rBody*/) {
                ++nCalls;
                return officelabs::InlineCompletionController::FetchResult{200, R"({"suggestions":[{"text":" jumps"}]})"};
            };

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);
        vcl::Window* pEditWin = getEditWindow();
        rtl::Reference<officelabs::InlineCompletionController> xController(
            new officelabs::InlineCompletionController(
                xModel->getCurrentController(), xModel, pEditWin, aFetcher,
                makeCaretProvider(pEditWin), makeEnabledProvider(false)));
        xController->start();

        xController->requestNow();
        drainUntilIdle(xController.get());

        CPPUNIT_ASSERT_EQUAL(0, nCalls);
        CPPUNIT_ASSERT(xController->pendingSuggestion().isEmpty());
        CPPUNIT_ASSERT(!xController->isGhostVisible());

        xController->dispose();
    }

    /// Shows a suggestion, then presses and releases Escape. Returns the
    /// controller; nCalls counts fetcher calls.
    rtl::Reference<officelabs::InlineCompletionController> showThenEscape(int& nCalls)
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDoc(mxComponent, UNO_QUERY_THROW);
        setTextAndGotoEnd(xTextDoc);

        officelabs::InlineCompletionController::Fetcher aFetcher =
            [&nCalls](const OString& /*rBody*/) {
                ++nCalls;
                return officelabs::InlineCompletionController::FetchResult{
                    200, R"({"suggestions":[{"text":" jumps"}]})"};
            };

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);
        vcl::Window* pEditWin = getEditWindow();
        rtl::Reference<officelabs::InlineCompletionController> xController(
            new officelabs::InlineCompletionController(
                xModel->getCurrentController(), xModel, pEditWin, aFetcher,
                makeCaretProvider(pEditWin), makeEnabledProvider()));
        xController->start();

        xController->requestNow();
        drainUntilIdle(xController.get());
        CPPUNIT_ASSERT(xController->isGhostVisible());

        css::awt::KeyEvent aEscape = makeKeyEvent(pEditWin, css::awt::Key::ESCAPE);
        xController->keyPressed(aEscape);
        return xController;
    }

    // 15. GIVEN a request still in flight WHEN Escape is pressed before the
    // suggestion arrives and the debounce fires again THEN no new request is made.
    void testEscapeWhileInFlightSuppressesRefetch()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDoc(mxComponent, UNO_QUERY_THROW);
        setTextAndGotoEnd(xTextDoc);

        std::atomic<int> nCalls{ 0 };
        std::atomic<bool> bRelease{ false };
        officelabs::InlineCompletionController::Fetcher aFetcher =
            [&nCalls, &bRelease](const OString& /*rBody*/) {
                ++nCalls;
                while (!bRelease.load())
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                return officelabs::InlineCompletionController::FetchResult{
                    200, R"({"suggestions":[{"text":" jumps"}]})"};
            };

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);
        vcl::Window* pEditWin = getEditWindow();
        rtl::Reference<officelabs::InlineCompletionController> xController(
            new officelabs::InlineCompletionController(
                xModel->getCurrentController(), xModel, pEditWin, aFetcher,
                makeCaretProvider(pEditWin), makeEnabledProvider()));
        xController->start();

        xController->requestNow();
        CPPUNIT_ASSERT(xController->isInFlight());
        xController->keyPressed(makeKeyEvent(pEditWin, css::awt::Key::ESCAPE));
        bRelease = true;
        drainUntilIdle(xController.get());

        xController->requestNow();
        drainUntilIdle(xController.get());

        CPPUNIT_ASSERT_EQUAL(1, nCalls.load());

        xController->dispose();
    }

    // GIVEN a request in flight WHEN the user keeps typing, so the debounce
    // fires again and is suppressed, THEN the suppressed fire is re-armed once
    // the reply lands and a fresh request is made for the newer text.
    //
    // This is the trace-1 failure: the in-flight reply arrives stale (every
    // keyPressed bumps the generation) and, because the user has stopped
    // typing, nothing re-arms the one-shot timer -- so the text they actually
    // finished never gets a request at all.
    void testSuppressedDebounceFireIsReArmed()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDoc(mxComponent, UNO_QUERY_THROW);
        setTextAndGotoEnd(xTextDoc);

        std::atomic<int> nCalls{ 0 };
        std::atomic<bool> bRelease{ false };
        officelabs::InlineCompletionController::Fetcher aFetcher =
            [&nCalls, &bRelease](const OString& /*rBody*/) {
                ++nCalls;
                while (!bRelease.load())
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                return officelabs::InlineCompletionController::FetchResult{
                    200, R"({"suggestions":[{"text":" jumps"}]})"};
            };

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);
        vcl::Window* pEditWin = getEditWindow();
        rtl::Reference<officelabs::InlineCompletionController> xController(
            new officelabs::InlineCompletionController(
                xModel->getCurrentController(), xModel, pEditWin, aFetcher,
                makeCaretProvider(pEditWin), makeEnabledProvider()));
        xController->start();

        xController->requestNow();
        CPPUNIT_ASSERT(xController->isInFlight());

        // nCalls is incremented on the fetcher thread, which requestNow only
        // spawns -- wait for it to actually enter the fetcher before counting.
        for (int i = 0; i < 200 && nCalls.load() == 0; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        CPPUNIT_ASSERT_EQUAL(1, nCalls.load());

        // The user types on: the generation moves past the in-flight request,
        // and the debounce fire that follows is suppressed.
        xController->keyPressed(makeKeyEvent(pEditWin, css::awt::Key::A));
        xController->requestNow();
        CPPUNIT_ASSERT_EQUAL(1, nCalls.load());

        bRelease = true;
        drainUntilIdle(xController.get());

        // The reply was dropped as stale, but the suppressed fire must have
        // been re-armed, so a second request follows without further typing.
        bool bSecond = false;
        for (int i = 0; i < 150 && !bSecond; ++i)
        {
            Application::Reschedule(true);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            bSecond = nCalls.load() == 2;
        }
        CPPUNIT_ASSERT_EQUAL(2, nCalls.load());

        xController->dispose();
    }

    // 13. GIVEN a suggestion dismissed with Escape WHEN the debounce fires again
    // with the text unchanged THEN no new request is made.
    void testEscapeSuppressesRefetch()
    {
        int nCalls = 0;
        rtl::Reference<officelabs::InlineCompletionController> xController = showThenEscape(nCalls);

        xController->requestNow();
        drainUntilIdle(xController.get());

        CPPUNIT_ASSERT_EQUAL(1, nCalls);

        xController->dispose();
    }

    // 14. GIVEN a suggestion dismissed with Escape WHEN the paragraph text then
    // changes and the debounce fires THEN a new request is made.
    void testEscapeSuppressionEndsWhenTextChanges()
    {
        int nCalls = 0;
        rtl::Reference<officelabs::InlineCompletionController> xController = showThenEscape(nCalls);

        Reference<text::XTextDocument> xTextDoc(mxComponent, UNO_QUERY_THROW);
        xTextDoc->getText()->setString(u"The quick brown fox runs"_ustr);
        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);
        Reference<text::XTextViewCursorSupplier> xViewCursorSupplier(
            xModel->getCurrentController(), UNO_QUERY_THROW);
        xViewCursorSupplier->getViewCursor()->gotoEnd(false);

        xController->requestNow();
        drainUntilIdle(xController.get());

        CPPUNIT_ASSERT_EQUAL(2, nCalls);

        xController->dispose();
    }

    // 15. GIVEN a white page WHEN the ghost color is computed THEN black is
    // blended at VS Code's light editorGhostText alpha (119/255): #888888.
    void testGhostTextColorLightPage()
    {
        const Color aResult = officelabs::GhostTextWindow::ghostTextColor(COL_WHITE);
        CPPUNIT_ASSERT_EQUAL(Color(0x88, 0x88, 0x88), aResult);
    }

    // 16. GIVEN a #1E1E1E page WHEN the ghost color is computed THEN white is
    // blended at VS Code's dark editorGhostText alpha (86/255): #696969.
    void testGhostTextColorDarkPage()
    {
        const Color aResult = officelabs::GhostTextWindow::ghostTextColor(Color(0x1E, 0x1E, 0x1E));
        CPPUNIT_ASSERT_EQUAL(Color(0x69, 0x69, 0x69), aResult);
    }

    // 17. GIVEN an injected FontProvider returning "Liberation Mono" WHEN a
    // suggestion is shown THEN the ghost is rendered in that font.
    void testFontProviderAppliesDocFont()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDoc(mxComponent, UNO_QUERY_THROW);
        setTextAndGotoEnd(xTextDoc);

        officelabs::InlineCompletionController::FetchResult aResponse;
        aResponse.nStatus = 200;
        aResponse.aBody = R"({"suggestions":[{"text":" jumps"}]})";
        officelabs::InlineCompletionController::Fetcher aFetcher =
            [aResponse](const OString& /*rBody*/) mutable { return aResponse; };

        officelabs::InlineCompletionController::FontProvider aFontProvider = []() {
            return std::optional<vcl::Font>(vcl::Font(u"Liberation Mono"_ustr, Size(0, 16)));
        };

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);
        vcl::Window* pEditWin = getEditWindow();
        rtl::Reference<officelabs::InlineCompletionController> xController(
            new officelabs::InlineCompletionController(
                xModel->getCurrentController(), xModel, pEditWin, aFetcher,
                makeCaretProvider(pEditWin), makeEnabledProvider(), aFontProvider));
        xController->start();

        xController->requestNow();
        drainUntilIdle(xController.get());

        CPPUNIT_ASSERT(xController->isGhostVisible());
        CPPUNIT_ASSERT_EQUAL(u"Liberation Mono"_ustr, xController->ghostFontFamily());

        xController->dispose();
    }

    /// Creates a controller whose fetcher returns " jumps", with the given
    /// font provider (empty = the controller's default), and shows the ghost.
    rtl::Reference<officelabs::InlineCompletionController>
    showWithFontProvider(officelabs::InlineCompletionController::FontProvider aFontProvider)
    {
        officelabs::InlineCompletionController::Fetcher aFetcher = [](const OString& /*rBody*/) {
            return officelabs::InlineCompletionController::FetchResult{
                200, R"({"suggestions":[{"text":" jumps"}]})"};
        };

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);
        vcl::Window* pEditWin = getEditWindow();
        rtl::Reference<officelabs::InlineCompletionController> xController(
            new officelabs::InlineCompletionController(
                xModel->getCurrentController(), xModel, pEditWin, aFetcher,
                makeCaretProvider(pEditWin), makeEnabledProvider(), std::move(aFontProvider)));
        xController->start();

        xController->requestNow();
        drainUntilIdle(xController.get());
        CPPUNIT_ASSERT(xController->isGhostVisible());
        return xController;
    }

    // 18. GIVEN 18 pt text at the caret and no injected font provider WHEN a
    // suggestion is shown THEN the ghost font height is 18 pt in edit-window pixels.
    void testDefaultFontProviderUsesCursorHeight()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDoc(mxComponent, UNO_QUERY_THROW);
        setTextAndGotoEnd(xTextDoc);
        Reference<text::XText> xText = xTextDoc->getText();
        Reference<beans::XPropertySet> xRange(xText->createTextCursorByRange(xText), UNO_QUERY_THROW);
        xRange->setPropertyValue(u"CharHeight"_ustr, Any(float(18)));

        rtl::Reference<officelabs::InlineCompletionController> xController = showWithFontProvider({});

        const tools::Long nExpected = getEditWindow()->LogicToPixel(Size(0, 18 * 20)).Height();
        CPPUNIT_ASSERT_EQUAL(nExpected, xController->ghostFontHeight());

        xController->dispose();
    }

    // 19. GIVEN a font provider that finds no font WHEN a suggestion is shown
    // THEN the ghost falls back to the UI app font.
    void testNoDocFontFallsBackToAppFont()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDoc(mxComponent, UNO_QUERY_THROW);
        setTextAndGotoEnd(xTextDoc);

        rtl::Reference<officelabs::InlineCompletionController> xController
            = showWithFontProvider([]() { return std::optional<vcl::Font>(); });

        const OUString aAppFamily
            = getEditWindow()->GetSettings().GetStyleSettings().GetAppFont().GetFamilyName();
        CPPUNIT_ASSERT_EQUAL(aAppFamily, xController->ghostFontFamily());

        xController->dispose();
    }

    // 20. GIVEN a 40 px document font and a 16 px caret WHEN a suggestion is
    // shown THEN the ghost window height is exactly the text height, so
    // Paint's centring term is zero and the ghost sits on the document
    // baseline (project#194).
    void testGhostWindowHeightIsTextHeight()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDoc(mxComponent, UNO_QUERY_THROW);
        setTextAndGotoEnd(xTextDoc);

        rtl::Reference<officelabs::InlineCompletionController> xController = showWithFontProvider(
            []() { return std::optional<vcl::Font>(vcl::Font(u"Liberation Serif"_ustr, Size(0, 40))); });

        CPPUNIT_ASSERT_EQUAL(xController->ghostTextHeight(), xController->ghostWindowHeight());

        xController->dispose();
    }

    // 21. GIVEN a shown suggestion " jumps" WHEN the leading space (matching
    // the head of the suggestion) is typed THEN the ghost stays showing and
    // the pending suggestion shortens by that one character.
    void testMatchingLowercaseTypesThrough()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDoc(mxComponent, UNO_QUERY_THROW);
        setTextAndGotoEnd(xTextDoc);

        rtl::Reference<officelabs::InlineCompletionController> xController = showWithFontProvider({});
        vcl::Window* pEditWin = getEditWindow();

        CPPUNIT_ASSERT_EQUAL(u" jumps"_ustr, xController->pendingSuggestion());

        css::awt::KeyEvent aKey = makeKeyEvent(pEditWin, css::awt::Key::SPACE);
        aKey.KeyChar = u' ';
        sal_Bool bHandled = xController->keyPressed(aKey);
        // Not consumed: Writer must insert the character itself. keyPressed
        // only posts the re-anchor callback, so simulate Writer's own
        // insertion of the typed key before draining it.
        CPPUNIT_ASSERT(!bHandled);

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);
        Reference<text::XTextViewCursorSupplier> xViewCursorSupplier(
            xModel->getCurrentController(), UNO_QUERY_THROW);
        Reference<text::XTextViewCursor> xViewCursor = xViewCursorSupplier->getViewCursor();
        xViewCursor->getText()->insertString(xViewCursor->getEnd(), u" "_ustr, false);

        Scheduler::ProcessEventsToIdle();

        CPPUNIT_ASSERT(xController->isGhostVisible());
        CPPUNIT_ASSERT_EQUAL(u"jumps"_ustr, xController->pendingSuggestion());

        xController->dispose();
    }

    // 22. GIVEN a shown suggestion starting with an uppercase letter WHEN that
    // letter is typed with the Shift modifier THEN it still types through
    // rather than being treated as a shortcut and dismissing the ghost.
    void testShiftedMatchingCharacterTypesThrough()
    {
        officelabs::InlineCompletionController::Fetcher aFetcher = [](const OString& /*rBody*/) {
            return officelabs::InlineCompletionController::FetchResult{
                200, R"({"suggestions":[{"text":"Jumps"}]})"};
        };

        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDoc(mxComponent, UNO_QUERY_THROW);
        setTextAndGotoEnd(xTextDoc);

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);
        vcl::Window* pEditWin = getEditWindow();
        rtl::Reference<officelabs::InlineCompletionController> xController(
            new officelabs::InlineCompletionController(
                xModel->getCurrentController(), xModel, pEditWin, aFetcher,
                makeCaretProvider(pEditWin), makeEnabledProvider()));
        xController->start();

        xController->requestNow();
        drainUntilIdle(xController.get());
        CPPUNIT_ASSERT(xController->isGhostVisible());
        CPPUNIT_ASSERT_EQUAL(u"Jumps"_ustr, xController->pendingSuggestion());

        css::awt::KeyEvent aKey
            = makeKeyEvent(pEditWin, css::awt::Key::J, css::awt::KeyModifier::SHIFT);
        aKey.KeyChar = u'J';
        sal_Bool bHandled = xController->keyPressed(aKey);
        CPPUNIT_ASSERT(!bHandled);

        // keyPressed only posts the re-anchor callback; simulate Writer's own
        // insertion of the typed key before draining it.
        Reference<text::XTextViewCursorSupplier> xViewCursorSupplier(
            xModel->getCurrentController(), UNO_QUERY_THROW);
        Reference<text::XTextViewCursor> xViewCursor = xViewCursorSupplier->getViewCursor();
        xViewCursor->getText()->insertString(xViewCursor->getEnd(), u"J"_ustr, false);

        Scheduler::ProcessEventsToIdle();

        CPPUNIT_ASSERT(xController->isGhostVisible());
        CPPUNIT_ASSERT_EQUAL(u"umps"_ustr, xController->pendingSuggestion());

        xController->dispose();
    }

    // 23. GIVEN a shown suggestion " jumps" WHEN a non-matching character is
    // typed THEN the ghost is dismissed rather than typed through.
    void testNonMatchingCharacterDismissesGhost()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDoc(mxComponent, UNO_QUERY_THROW);
        setTextAndGotoEnd(xTextDoc);

        rtl::Reference<officelabs::InlineCompletionController> xController = showWithFontProvider({});
        vcl::Window* pEditWin = getEditWindow();

        CPPUNIT_ASSERT_EQUAL(u" jumps"_ustr, xController->pendingSuggestion());

        css::awt::KeyEvent aKey = makeKeyEvent(pEditWin, css::awt::Key::Z);
        aKey.KeyChar = u'z';
        sal_Bool bHandled = xController->keyPressed(aKey);
        CPPUNIT_ASSERT(!bHandled);

        CPPUNIT_ASSERT(!xController->isGhostVisible());
        CPPUNIT_ASSERT(xController->pendingSuggestion().isEmpty());

        xController->dispose();
    }

    // 24. GIVEN a type-through callback posted but not yet run WHEN Tab is
    // pressed before it runs THEN the key is consumed, nothing is inserted
    // into the document, and the ghost is dismissed.
    void testTabWhileTypeThroughPendingDoesNotInsert()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDoc(mxComponent, UNO_QUERY_THROW);
        setTextAndGotoEnd(xTextDoc);

        officelabs::InlineCompletionController::Fetcher aFetcher = [](const OString& /*rBody*/) {
            return officelabs::InlineCompletionController::FetchResult{
                200, R"({"suggestions":[{"text":" jumps"}]})"};
        };

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);
        vcl::Window* pEditWin = getEditWindow();
        rtl::Reference<officelabs::InlineCompletionController> xController(
            new officelabs::InlineCompletionController(
                xModel->getCurrentController(), xModel, pEditWin, aFetcher,
                makeCaretProvider(pEditWin), makeEnabledProvider()));
        xController->start();

        xController->requestNow();
        drainUntilIdle(xController.get());
        CPPUNIT_ASSERT(xController->isGhostVisible());

        // Type through the leading space, but do NOT drain the event loop, so
        // the posted reanchorAfterTypeThrough callback stays outstanding.
        css::awt::KeyEvent aSpace = makeKeyEvent(pEditWin, css::awt::Key::SPACE);
        aSpace.KeyChar = u' ';
        CPPUNIT_ASSERT(!xController->keyPressed(aSpace));

        css::awt::KeyEvent aTab = makeKeyEvent(pEditWin, css::awt::Key::TAB);
        sal_Bool bHandled = xController->keyPressed(aTab);
        CPPUNIT_ASSERT(bHandled);

        Reference<text::XText> xText = xTextDoc->getText();
        CPPUNIT_ASSERT_EQUAL(u"The quick brown fox"_ustr, xText->getString());
        CPPUNIT_ASSERT(!xController->isGhostVisible());

        xController->dispose();
    }

    // 25. GIVEN a type-through callback posted WHEN the document does not
    // contain the expected insertion by the time it runs (the typed character
    // was buffered rather than inserted, or autocorrect rewrote the text)
    // THEN the ghost is hidden and the debounce timer is left running rather
    // than dead -- observed here as a second fetch happening on its own,
    // with no further key event.
    void testTypeThroughGuardFailureHidesGhostAndRearmsDebounce()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDoc(mxComponent, UNO_QUERY_THROW);
        setTextAndGotoEnd(xTextDoc);

        std::atomic<int> nCalls{ 0 };
        officelabs::InlineCompletionController::Fetcher aFetcher =
            [&nCalls](const OString& /*rBody*/) {
                ++nCalls;
                return officelabs::InlineCompletionController::FetchResult{
                    200, R"({"suggestions":[{"text":" jumps"}]})"};
            };

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);
        vcl::Window* pEditWin = getEditWindow();
        rtl::Reference<officelabs::InlineCompletionController> xController(
            new officelabs::InlineCompletionController(
                xModel->getCurrentController(), xModel, pEditWin, aFetcher,
                makeCaretProvider(pEditWin), makeEnabledProvider()));
        xController->start();

        xController->requestNow();
        drainUntilIdle(xController.get());
        CPPUNIT_ASSERT(xController->isGhostVisible());
        CPPUNIT_ASSERT_EQUAL(1, nCalls.load());

        // Type through the leading space, but let the document diverge from
        // what keyPressed predicted before the posted callback runs -- as
        // Writer's own input buffering or autocorrect would.
        css::awt::KeyEvent aSpace = makeKeyEvent(pEditWin, css::awt::Key::SPACE);
        aSpace.KeyChar = u' ';
        CPPUNIT_ASSERT(!xController->keyPressed(aSpace));

        Reference<text::XTextViewCursorSupplier> xViewCursorSupplier(
            xModel->getCurrentController(), UNO_QUERY_THROW);
        Reference<text::XTextViewCursor> xViewCursor = xViewCursorSupplier->getViewCursor();
        xViewCursor->getText()->insertString(xViewCursor->getEnd(), u"!"_ustr, false);

        Scheduler::ProcessEventsToIdle();

        CPPUNIT_ASSERT(!xController->isGhostVisible());
        CPPUNIT_ASSERT(xController->pendingSuggestion().isEmpty());

        // No further key event: the 150ms debounce alone must issue the next
        // request, which it can only do if it was re-armed.
        bool bSecondCall = false;
        for (int i = 0; i < 100 && !bSecondCall; ++i)
        {
            Scheduler::ProcessEventsToIdle();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            bSecondCall = nCalls.load() == 2;
        }
        CPPUNIT_ASSERT_EQUAL(2, nCalls.load());

        xController->dispose();
    }

    // 26. GIVEN a 40 px document font and a 16 px caret WHEN a suggestion is
    // shown THEN the ghost window is as tall as the text, so nothing is clipped.
    void testTallFontGrowsGhostWindow()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDoc(mxComponent, UNO_QUERY_THROW);
        setTextAndGotoEnd(xTextDoc);

        rtl::Reference<officelabs::InlineCompletionController> xController = showWithFontProvider(
            []() { return std::optional<vcl::Font>(vcl::Font(u"Liberation Serif"_ustr, Size(0, 40))); });

        // Guard against the vacuous pass: two zero heights are also equal.
        CPPUNIT_ASSERT(xController->ghostTextHeight() > 0);
        CPPUNIT_ASSERT_EQUAL(xController->ghostTextHeight(), xController->ghostWindowHeight());

        xController->dispose();
    }

    // GIVEN a fetcher that never returns, so the in-flight slot is held
    // indefinitely, WHEN the watchdog deadline has passed, THEN a later
    // requestNow() abandons the stranded request and issues a fresh one
    // instead of returning early at the in-flight guard (project#228).
    void testHungFetcherDoesNotStrandInFlight()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDoc(mxComponent, UNO_QUERY_THROW);
        setTextAndGotoEnd(xTextDoc);

        // Gates are shared_ptr and captured BY VALUE, not by reference: the
        // asserts below run before the fetcher is released, so a failing assert
        // unwinds this frame while a detached thread is still spinning on them.
        // A by-reference capture would leave that thread reading stack that has
        // gone out of scope.
        auto pCalls = std::make_shared<std::atomic<int>>(0);
        auto pRelease = std::make_shared<std::atomic<bool>>(false);
        officelabs::InlineCompletionController::Fetcher aFetcher =
            [pCalls, pRelease](const OString& /*rBody*/) {
                ++*pCalls;
                while (!pRelease->load())
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                return officelabs::InlineCompletionController::FetchResult{
                    200, R"({"suggestions":[{"text":" jumps"}]})"};
            };

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);
        vcl::Window* pEditWin = getEditWindow();
        rtl::Reference<officelabs::InlineCompletionController> xController(
            new officelabs::InlineCompletionController(
                xModel->getCurrentController(), xModel, pEditWin, aFetcher,
                makeCaretProvider(pEditWin), makeEnabledProvider()));
        xController->start();
        // Deadline expires the instant the request is issued, so the test does
        // not have to wait out the real 10 s watchdog. This cannot make the
        // request abandon itself: the watchdog runs at the top of requestNow(),
        // before the new request sets its own deadline.
        xController->setInFlightTimeoutMsForTest(0);

        xController->requestNow();
        CPPUNIT_ASSERT(xController->isInFlight());
        // pCalls is incremented on the fetcher thread, which requestNow only
        // spawns -- wait for it to actually enter the fetcher before counting.
        for (int i = 0; i < 200 && pCalls->load() < 1; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        CPPUNIT_ASSERT_EQUAL(1, pCalls->load());

        // Without the watchdog this fire is swallowed by the in-flight guard
        // and nCalls stays 1 for the rest of the session.
        xController->requestNow();

        // pCalls is incremented on the fetcher thread, which requestNow only
        // spawns -- wait for it to actually enter the fetcher before counting.
        for (int i = 0; i < 200 && pCalls->load() < 2; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        CPPUNIT_ASSERT_EQUAL(2, pCalls->load());

        *pRelease = true;
        drainUntilIdle(xController.get());
        xController->dispose();
    }

    // GIVEN the watchdog abandoned a request and a newer one now holds the
    // slot, WHEN the abandoned request finally returns, THEN its reply must not
    // release the newer request's slot -- otherwise the watchdog fix would
    // itself allow two fetches in flight at once (project#228).
    void testAbandonedReplyDoesNotReleaseNewerRequest()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDoc(mxComponent, UNO_QUERY_THROW);
        setTextAndGotoEnd(xTextDoc);

        // Shared, captured by value -- see the note in the test above.
        auto pCalls = std::make_shared<std::atomic<int>>(0);
        auto pReleaseFirst = std::make_shared<std::atomic<bool>>(false);
        auto pReleaseSecond = std::make_shared<std::atomic<bool>>(false);
        officelabs::InlineCompletionController::Fetcher aFetcher =
            [pCalls, pReleaseFirst, pReleaseSecond](const OString& /*rBody*/) {
                const int nMine = ++*pCalls;
                std::atomic<bool>& rGate = (nMine == 1) ? *pReleaseFirst : *pReleaseSecond;
                while (!rGate.load())
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                return officelabs::InlineCompletionController::FetchResult{
                    200, R"({"suggestions":[{"text":" jumps"}]})"};
            };

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);
        vcl::Window* pEditWin = getEditWindow();
        rtl::Reference<officelabs::InlineCompletionController> xController(
            new officelabs::InlineCompletionController(
                xModel->getCurrentController(), xModel, pEditWin, aFetcher,
                makeCaretProvider(pEditWin), makeEnabledProvider()));
        xController->start();
        xController->setInFlightTimeoutMsForTest(0);

        xController->requestNow();          // generation A, never returns yet
        // Wait for A to actually ENTER the fetcher before issuing B. The
        // fetcher picks its gate by ++pCalls, so if B's increment landed
        // first it would take pReleaseFirst and the release below would
        // free the LIVE request instead of the abandoned one -- isInFlight()
        // would then be false and this test would fail for the wrong reason.
        for (int i = 0; i < 200 && pCalls->load() < 1; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        CPPUNIT_ASSERT_EQUAL(1, pCalls->load());

        xController->requestNow();          // watchdog abandons A, issues B
        // pCalls is incremented on the fetcher thread, which requestNow only
        // spawns -- wait for it to actually enter the fetcher before counting.
        for (int i = 0; i < 200 && pCalls->load() < 2; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        CPPUNIT_ASSERT_EQUAL(2, pCalls->load());
        CPPUNIT_ASSERT(xController->isInFlight());

        // A returns late. Its reply carries the abandoned generation.
        *pReleaseFirst = true;
        for (int i = 0; i < 40; ++i)
        {
            Scheduler::ProcessEventsToIdle();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        // B still owns the slot.
        CPPUNIT_ASSERT(xController->isInFlight());

        *pReleaseSecond = true;
        drainUntilIdle(xController.get());
        CPPUNIT_ASSERT(!xController->isInFlight());

        xController->dispose();
    }

    // GIVEN one live request fails, a second is abandoned by the watchdog and
    // its late reply eventually arrives as 200,
    // WHEN one more live request fails afterwards,
    // THEN backoff engages on that third failure -- proving the late 200 did
    // not reset the failure count to 0 (which would have needed two more
    // failures instead of one). This is the masking half of project#234: a
    // late success from a request the watchdog already gave up on must not
    // erase real, still-relevant failure evidence.
    void testAbandonedReplySuccessDoesNotMaskFailures()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDoc(mxComponent, UNO_QUERY_THROW);
        setTextAndGotoEnd(xTextDoc);

        // Shared, captured by value -- see the note on testHungFetcherDoesNotStrandInFlight.
        auto pCalls = std::make_shared<std::atomic<int>>(0);
        auto pReleaseAbandoned = std::make_shared<std::atomic<bool>>(false);
        auto pReleaseLive = std::make_shared<std::atomic<bool>>(false);
        officelabs::InlineCompletionController::FetchResult aFailure;
        aFailure.nStatus = 500;
        aFailure.aBody = "error";
        officelabs::InlineCompletionController::Fetcher aFetcher =
            [pCalls, pReleaseAbandoned, pReleaseLive, aFailure](const OString& /*rBody*/) {
                const int nMine = ++*pCalls;
                // Call 1 fails immediately. Call 2 is the one the watchdog
                // abandons: it hangs until released, then reports success --
                // the reply the fix must discard rather than count. Call 3 is
                // the live request that takes over the slot after the
                // abandonment; it also hangs so the test controls exactly
                // when it fails, relative to call 2's late reply.
                if (nMine == 1)
                    return aFailure;
                std::atomic<bool>& rGate = (nMine == 2) ? *pReleaseAbandoned : *pReleaseLive;
                while (!rGate.load())
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                if (nMine == 2)
                    return officelabs::InlineCompletionController::FetchResult{
                        200, R"({"suggestions":[{"text":" jumps"}]})"};
                return aFailure;
            };

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);
        vcl::Window* pEditWin = getEditWindow();
        rtl::Reference<officelabs::InlineCompletionController> xController(
            new officelabs::InlineCompletionController(
                xModel->getCurrentController(), xModel, pEditWin, aFetcher,
                makeCaretProvider(pEditWin), makeEnabledProvider()));
        xController->start();

        // Failure 1: an ordinary live request that completes with 500.
        xController->requestNow();
        drainUntilIdle(xController.get());
        CPPUNIT_ASSERT_EQUAL(1, pCalls->load());

        // Failure 2: issue the request that will be abandoned, then let the
        // watchdog fire on the next requestNow() -- zero timeout so the test
        // does not have to wait out the real 10 s deadline.
        xController->setInFlightTimeoutMsForTest(0);
        xController->requestNow();
        for (int i = 0; i < 200 && pCalls->load() < 2; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        CPPUNIT_ASSERT_EQUAL(2, pCalls->load());
        CPPUNIT_ASSERT(xController->isInFlight());

        // The watchdog abandons call 2 (counting its second failure) and
        // immediately issues call 3, which takes the slot and hangs.
        xController->requestNow();
        for (int i = 0; i < 200 && pCalls->load() < 3; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        CPPUNIT_ASSERT_EQUAL(3, pCalls->load());
        CPPUNIT_ASSERT(xController->isInFlight());

        // Call 2's late reply lands: 200, for a generation that no longer
        // owns the slot. It must be discarded entirely, not counted and not
        // allowed to reset the failure count.
        *pReleaseAbandoned = true;
        for (int i = 0; i < 40; ++i)
        {
            Scheduler::ProcessEventsToIdle();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        // Call 3 still owns the slot -- the late reply for call 2 changed
        // nothing about it.
        CPPUNIT_ASSERT(xController->isInFlight());

        // Failure 3 (the live one): call 3 finally completes with 500. If the
        // late 200 above had reset the failure count, this alone would not
        // reach the threshold and the next requestNow() below would fetch
        // again instead of being blocked by backoff.
        *pReleaseLive = true;
        drainUntilIdle(xController.get());
        CPPUNIT_ASSERT(!xController->isInFlight());

        xController->requestNow();
        drainUntilIdle(xController.get());
        CPPUNIT_ASSERT_EQUAL(3, pCalls->load());

        xController->dispose();
    }

    // GIVEN a fetcher that never returns at all,
    // WHEN the watchdog abandons three requests to it in a row,
    // THEN backoff engages on the third abandonment -- proving the fix that
    // stops counting a late reply's own status does not also stop counting
    // the abandonment itself. A persistently hung agent must still reach
    // backoff; it must not be suppressed as a side effect of project#234's
    // fix (a persistently-dead agent is project#228's failure mode, which the
    // watchdog exists to recover from -- backoff still has to engage against
    // it, or every abandonment just spawns another detached thread forever).
    void testRepeatedAbandonmentStillTripsBackoff()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDoc(mxComponent, UNO_QUERY_THROW);
        setTextAndGotoEnd(xTextDoc);

        auto pCalls = std::make_shared<std::atomic<int>>(0);
        auto pRelease = std::make_shared<std::atomic<bool>>(false);
        officelabs::InlineCompletionController::Fetcher aFetcher =
            [pCalls, pRelease](const OString& /*rBody*/) {
                ++*pCalls;
                while (!pRelease->load())
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                return officelabs::InlineCompletionController::FetchResult{500, "error"};
            };

        Reference<frame::XModel> xModel(mxComponent, UNO_QUERY_THROW);
        vcl::Window* pEditWin = getEditWindow();
        rtl::Reference<officelabs::InlineCompletionController> xController(
            new officelabs::InlineCompletionController(
                xModel->getCurrentController(), xModel, pEditWin, aFetcher,
                makeCaretProvider(pEditWin), makeEnabledProvider()));
        xController->start();
        xController->setInFlightTimeoutMsForTest(0);

        xController->requestNow(); // call 1, hangs
        for (int i = 0; i < 200 && pCalls->load() < 1; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

        xController->requestNow(); // watchdog abandons 1 (failure 1), issues 2
        for (int i = 0; i < 200 && pCalls->load() < 2; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

        xController->requestNow(); // watchdog abandons 2 (failure 2), issues 3
        for (int i = 0; i < 200 && pCalls->load() < 3; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        CPPUNIT_ASSERT(xController->isInFlight());

        // Watchdog abandons 3: the third consecutive failure. Backoff engages
        // immediately, so no fourth request is issued even though nothing is
        // in flight any more.
        xController->requestNow();
        CPPUNIT_ASSERT(!xController->isInFlight());
        CPPUNIT_ASSERT_EQUAL(3, pCalls->load());

        // Still backed off: another requestNow() does not fetch either.
        xController->requestNow();
        CPPUNIT_ASSERT_EQUAL(3, pCalls->load());

        *pRelease = true;
        drainUntilIdle(xController.get());
        xController->dispose();
    }

    CPPUNIT_TEST_SUITE(InlineCompletionControllerTest);
    CPPUNIT_TEST(testAcceptSuggestion);
    CPPUNIT_TEST(testTabAcceptsSuggestion);
    CPPUNIT_TEST(testTabDoesNotInsertAStaleSuggestion);
    CPPUNIT_TEST(testTabWithoutSuggestionDoesNothing);
    CPPUNIT_TEST(testForeignSourceKeyIgnored);
    CPPUNIT_TEST(testStaleGenerationCleared);
    CPPUNIT_TEST(testChangedTextCancelsSuggestion);
    CPPUNIT_TEST(testBackoffAfterThreeFailures);
    CPPUNIT_TEST(testDisposeWhileInFlightSafe);
    CPPUNIT_TEST(testIneligibleNoRequest);
    CPPUNIT_TEST(testEscapeClearsSuggestion);
    CPPUNIT_TEST(testNoShowWhenCaretUnavailable);
    CPPUNIT_TEST(testDisabledProviderNoRequest);
    CPPUNIT_TEST(testEscapeSuppressesRefetch);
    CPPUNIT_TEST(testEscapeSuppressionEndsWhenTextChanges);
    CPPUNIT_TEST(testEscapeWhileInFlightSuppressesRefetch);
    CPPUNIT_TEST(testSuppressedDebounceFireIsReArmed);
    CPPUNIT_TEST(testGhostTextColorLightPage);
    CPPUNIT_TEST(testGhostTextColorDarkPage);
    CPPUNIT_TEST(testFontProviderAppliesDocFont);
    CPPUNIT_TEST(testDefaultFontProviderUsesCursorHeight);
    CPPUNIT_TEST(testNoDocFontFallsBackToAppFont);
    CPPUNIT_TEST(testTallFontGrowsGhostWindow);
    CPPUNIT_TEST(testGhostWindowHeightIsTextHeight);
    CPPUNIT_TEST(testMatchingLowercaseTypesThrough);
    CPPUNIT_TEST(testShiftedMatchingCharacterTypesThrough);
    CPPUNIT_TEST(testNonMatchingCharacterDismissesGhost);
    CPPUNIT_TEST(testTabWhileTypeThroughPendingDoesNotInsert);
    CPPUNIT_TEST(testTypeThroughGuardFailureHidesGhostAndRearmsDebounce);
    CPPUNIT_TEST(testHungFetcherDoesNotStrandInFlight);
    CPPUNIT_TEST(testAbandonedReplyDoesNotReleaseNewerRequest);
    CPPUNIT_TEST(testAbandonedReplySuccessDoesNotMaskFailures);
    CPPUNIT_TEST(testRepeatedAbandonmentStillTripsBackoff);
    CPPUNIT_TEST_SUITE_END();
};

CPPUNIT_TEST_SUITE_REGISTRATION(InlineCompletionControllerTest);

} // anonymous namespace

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
