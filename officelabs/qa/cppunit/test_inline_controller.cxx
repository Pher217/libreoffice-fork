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
#include <optional>

#include <com/sun/star/awt/Key.hpp>
#include <com/sun/star/awt/KeyEvent.hpp>
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
        xController->keyReleased(aEscape);
        return xController;
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
    // shown THEN the ghost window is as tall as the text, so nothing is clipped.
    void testTallFontGrowsGhostWindow()
    {
        loadFromURL(u"private:factory/swriter"_ustr);
        Reference<text::XTextDocument> xTextDoc(mxComponent, UNO_QUERY_THROW);
        setTextAndGotoEnd(xTextDoc);

        rtl::Reference<officelabs::InlineCompletionController> xController = showWithFontProvider(
            []() { return std::optional<vcl::Font>(vcl::Font(u"Liberation Serif"_ustr, Size(0, 40))); });

        CPPUNIT_ASSERT_EQUAL(xController->ghostTextHeight(), xController->ghostWindowHeight());

        xController->dispose();
    }

    CPPUNIT_TEST_SUITE(InlineCompletionControllerTest);
    CPPUNIT_TEST(testAcceptSuggestion);
    CPPUNIT_TEST(testTabAcceptsSuggestion);
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
    CPPUNIT_TEST(testGhostTextColorLightPage);
    CPPUNIT_TEST(testGhostTextColorDarkPage);
    CPPUNIT_TEST(testFontProviderAppliesDocFont);
    CPPUNIT_TEST(testDefaultFontProviderUsesCursorHeight);
    CPPUNIT_TEST(testNoDocFontFallsBackToAppFont);
    CPPUNIT_TEST(testTallFontGrowsGhostWindow);
    CPPUNIT_TEST_SUITE_END();
};

CPPUNIT_TEST_SUITE_REGISTRATION(InlineCompletionControllerTest);

} // anonymous namespace

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
