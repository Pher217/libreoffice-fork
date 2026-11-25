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

#include <officelabs/StartCenterAccount.hxx>

#include <string>

using officelabs::mapAuthStatus;
using officelabs::StartCenterAccountAction;
using officelabs::StartCenterAccountState;
using officelabs::StartCenterAccountView;

namespace
{
class StartCenterAccountTest : public CppUnit::TestFixture
{
public:
    // GIVEN the agent says signed_out WHEN the status is mapped
    // THEN the entry is in the signed-out state.
    void testSignedOutState()
    {
        const StartCenterAccountView aView = mapAuthStatus(200, R"({"state":"signed_out"})");
        CPPUNIT_ASSERT(aView.eState == StartCenterAccountState::SignedOut);
    }

    // GIVEN signed_out WHEN mapped THEN the label invites the user to sign in.
    void testSignedOutLabel()
    {
        const StartCenterAccountView aView = mapAuthStatus(200, R"({"state":"signed_out"})");
        CPPUNIT_ASSERT_EQUAL(std::string("Sign in to OfficeLabs"), aView.aLabel);
    }

    // GIVEN signed_out WHEN mapped THEN the button starts the device flow.
    void testSignedOutAction()
    {
        const StartCenterAccountView aView = mapAuthStatus(200, R"({"state":"signed_out"})");
        CPPUNIT_ASSERT(aView.eAction == StartCenterAccountAction::SignIn);
        CPPUNIT_ASSERT_EQUAL(std::string("Sign in"), aView.aButton);
    }

    // GIVEN a pending grant WHEN mapped THEN the entry is in the pending state.
    void testPendingState()
    {
        const StartCenterAccountView aView = mapAuthStatus(
            200,
            R"({"state":"pending","user_code":"ABCD-EFGH",)"
            R"("verification_uri_complete":"https://x.example/device?user_code=ABCD-EFGH"})");
        CPPUNIT_ASSERT(aView.eState == StartCenterAccountState::Pending);
    }

    // GIVEN a pending grant WHEN mapped THEN the user code is shown under the label.
    void testPendingShowsUserCode()
    {
        const StartCenterAccountView aView
            = mapAuthStatus(200, R"({"state":"pending","user_code":"ABCD-EFGH"})");
        CPPUNIT_ASSERT_EQUAL(std::string("Confirm in your browser"), aView.aLabel);
        CPPUNIT_ASSERT_EQUAL(std::string("ABCD-EFGH"), aView.aSublabel);
    }

    // GIVEN a pending grant WHEN mapped THEN the only action is Cancel.
    void testPendingActionIsCancel()
    {
        const StartCenterAccountView aView
            = mapAuthStatus(200, R"({"state":"pending","user_code":"ABCD-EFGH"})");
        CPPUNIT_ASSERT(aView.eAction == StartCenterAccountAction::Cancel);
        CPPUNIT_ASSERT_EQUAL(std::string("Cancel"), aView.aButton);
    }

    // GIVEN a pending grant carrying verification_uri_complete WHEN mapped
    // THEN that URL is handed on for the browser opener.
    void testPendingCarriesVerificationUrl()
    {
        const StartCenterAccountView aView = mapAuthStatus(
            200,
            R"({"state":"pending","user_code":"ABCD-EFGH",)"
            R"("verification_uri_complete":"https://x.example/device?user_code=ABCD-EFGH"})");
        CPPUNIT_ASSERT_EQUAL(std::string("https://x.example/device?user_code=ABCD-EFGH"),
                             aView.aVerificationUrl);
    }

    // GIVEN a signed-in account WHEN mapped THEN the entry is in the signed-in state.
    void testSignedInState()
    {
        const StartCenterAccountView aView = mapAuthStatus(
            200, R"({"state":"signed_in","email":"a@b.ch","plan_tier":"pro"})");
        CPPUNIT_ASSERT(aView.eState == StartCenterAccountState::SignedIn);
    }

    // GIVEN a signed-in account WHEN mapped THEN the e-mail is the label and the plan the sublabel.
    void testSignedInShowsEmailAndPlan()
    {
        const StartCenterAccountView aView = mapAuthStatus(
            200, R"({"state":"signed_in","email":"a@b.ch","plan_tier":"pro"})");
        CPPUNIT_ASSERT_EQUAL(std::string("a@b.ch"), aView.aLabel);
        CPPUNIT_ASSERT_EQUAL(std::string("Plan: Pro"), aView.aSublabel);
    }

    // GIVEN a signed-in account WHEN mapped THEN the button signs out.
    void testSignedInActionIsSignOut()
    {
        const StartCenterAccountView aView = mapAuthStatus(
            200, R"({"state":"signed_in","email":"a@b.ch","plan_tier":"pro"})");
        CPPUNIT_ASSERT(aView.eAction == StartCenterAccountAction::SignOut);
        CPPUNIT_ASSERT_EQUAL(std::string("Sign out"), aView.aButton);
    }

    // GIVEN the agent reports the plan as an upper-case enum ("FREE") WHEN mapped
    // THEN the sublabel shows it in display case ("Free").
    void testPlanIsShownInDisplayCase()
    {
        const StartCenterAccountView aView = mapAuthStatus(
            200, R"({"state":"signed_in","email":"a@b.ch","plan_tier":"FREE"})");
        CPPUNIT_ASSERT_EQUAL(std::string("Plan: Free"), aView.aSublabel);
    }

    // GIVEN a signed-in account without plan_tier WHEN mapped
    // THEN the sublabel is empty, not a placeholder or a crash.
    void testSignedInWithoutPlanHasEmptySublabel()
    {
        const StartCenterAccountView aView
            = mapAuthStatus(200, R"({"state":"signed_in","email":"a@b.ch"})");
        CPPUNIT_ASSERT(aView.eState == StartCenterAccountState::SignedIn);
        CPPUNIT_ASSERT_EQUAL(std::string("a@b.ch"), aView.aLabel);
        CPPUNIT_ASSERT_EQUAL(std::string(), aView.aSublabel);
    }

    // GIVEN a signed-in account without an e-mail WHEN mapped
    // THEN the label falls back to "Signed in".
    void testSignedInWithoutEmailFallsBack()
    {
        const StartCenterAccountView aView
            = mapAuthStatus(200, R"({"state":"signed_in","plan_tier":"pro"})");
        CPPUNIT_ASSERT_EQUAL(std::string("Signed in"), aView.aLabel);
    }

    // GIVEN an account the agent holds while the cloud is unreachable (offline)
    // WHEN mapped THEN it still reads as signed in, with the e-mail shown.
    void testOfflineKeepsTheAccountVisible()
    {
        const StartCenterAccountView aView
            = mapAuthStatus(200, R"({"state":"offline","email":"a@b.ch"})");
        CPPUNIT_ASSERT(aView.eState == StartCenterAccountState::SignedIn);
        CPPUNIT_ASSERT_EQUAL(std::string("a@b.ch"), aView.aLabel);
    }

    // GIVEN a connection failure (HTTP status 0) WHEN mapped
    // THEN the entry is in the unreachable state.
    void testConnectionFailureIsUnreachable()
    {
        const StartCenterAccountView aView = mapAuthStatus(0, std::string());
        CPPUNIT_ASSERT(aView.eState == StartCenterAccountState::Unreachable);
    }

    // GIVEN the unreachable state WHEN mapped THEN it says OfficeLabs is starting
    // (UTF-8 ellipsis), with no button and no action.
    void testUnreachableCopy()
    {
        const StartCenterAccountView aView = mapAuthStatus(0, std::string());
        CPPUNIT_ASSERT_EQUAL(std::string("OfficeLabs is starting\xE2\x80\xA6"), aView.aLabel);
        CPPUNIT_ASSERT_EQUAL(std::string(), aView.aButton);
        CPPUNIT_ASSERT(aView.eAction == StartCenterAccountAction::None);
    }

    // GIVEN the agent answers 401 (no valid session token yet) WHEN mapped
    // THEN the entry is unreachable, even though the body parses.
    void testNon200IsUnreachable()
    {
        const StartCenterAccountView aView = mapAuthStatus(401, R"({"state":"signed_out"})");
        CPPUNIT_ASSERT(aView.eState == StartCenterAccountState::Unreachable);
    }

    // GIVEN a body that is not JSON WHEN mapped THEN the entry is unreachable.
    void testMalformedJsonIsUnreachable()
    {
        const StartCenterAccountView aView = mapAuthStatus(200, "<html>502 Bad Gateway</html>");
        CPPUNIT_ASSERT(aView.eState == StartCenterAccountState::Unreachable);
    }

    // GIVEN a truncated JSON body WHEN mapped THEN the entry is unreachable.
    void testTruncatedJsonIsUnreachable()
    {
        const StartCenterAccountView aView = mapAuthStatus(200, R"({"state":"signed_in","ema)");
        CPPUNIT_ASSERT(aView.eState == StartCenterAccountState::Unreachable);
    }

    // GIVEN an empty body with HTTP 200 WHEN mapped THEN the entry is unreachable.
    void testEmptyBodyIsUnreachable()
    {
        const StartCenterAccountView aView = mapAuthStatus(200, std::string());
        CPPUNIT_ASSERT(aView.eState == StartCenterAccountState::Unreachable);
    }

    // GIVEN valid JSON without a state field WHEN mapped THEN the entry is unreachable.
    void testMissingStateIsUnreachable()
    {
        const StartCenterAccountView aView = mapAuthStatus(200, R"({"email":"a@b.ch"})");
        CPPUNIT_ASSERT(aView.eState == StartCenterAccountState::Unreachable);
    }

    // GIVEN a state this build does not know WHEN mapped THEN the entry is unreachable,
    // never a raw state string.
    void testUnknownStateIsUnreachable()
    {
        const StartCenterAccountView aView = mapAuthStatus(200, R"({"state":"quantum"})");
        CPPUNIT_ASSERT(aView.eState == StartCenterAccountState::Unreachable);
    }

    // GIVEN the agent reports not_configured (no cloud endpoint) WHEN mapped
    // THEN the entry is unreachable: a sign-in button could not work.
    void testNotConfiguredIsUnreachable()
    {
        const StartCenterAccountView aView = mapAuthStatus(200, R"({"state":"not_configured"})");
        CPPUNIT_ASSERT(aView.eState == StartCenterAccountState::Unreachable);
    }

    // GIVEN the agent reports state error with a reason WHEN mapped
    // THEN the entry offers sign-in again and shows no raw reason.
    void testErrorStateOffersSignIn()
    {
        const StartCenterAccountView aView
            = mapAuthStatus(200, R"({"state":"error","reason":"credential_unreadable"})");
        CPPUNIT_ASSERT(aView.eState == StartCenterAccountState::SignedOut);
        CPPUNIT_ASSERT_EQUAL(std::string(), aView.aSublabel);
    }

    // GIVEN a pending grant without a verification URL WHEN mapped
    // THEN the URL is empty so the caller opens nothing.
    void testPendingWithoutUrlHasEmptyUrl()
    {
        const StartCenterAccountView aView
            = mapAuthStatus(200, R"({"state":"pending","user_code":"ABCD-EFGH"})");
        CPPUNIT_ASSERT_EQUAL(std::string(), aView.aVerificationUrl);
    }

    // GIVEN a non-string email (a number) WHEN mapped THEN the e-mail is ignored
    // and the label falls back, so a hostile body cannot inject a value.
    void testNonStringEmailIsIgnored()
    {
        const StartCenterAccountView aView
            = mapAuthStatus(200, R"({"state":"signed_in","email":{"x":1}})");
        CPPUNIT_ASSERT_EQUAL(std::string("Signed in"), aView.aLabel);
    }

    CPPUNIT_TEST_SUITE(StartCenterAccountTest);
    CPPUNIT_TEST(testSignedOutState);
    CPPUNIT_TEST(testSignedOutLabel);
    CPPUNIT_TEST(testSignedOutAction);
    CPPUNIT_TEST(testPendingState);
    CPPUNIT_TEST(testPendingShowsUserCode);
    CPPUNIT_TEST(testPendingActionIsCancel);
    CPPUNIT_TEST(testPendingCarriesVerificationUrl);
    CPPUNIT_TEST(testSignedInState);
    CPPUNIT_TEST(testSignedInShowsEmailAndPlan);
    CPPUNIT_TEST(testSignedInActionIsSignOut);
    CPPUNIT_TEST(testPlanIsShownInDisplayCase);
    CPPUNIT_TEST(testSignedInWithoutPlanHasEmptySublabel);
    CPPUNIT_TEST(testSignedInWithoutEmailFallsBack);
    CPPUNIT_TEST(testOfflineKeepsTheAccountVisible);
    CPPUNIT_TEST(testConnectionFailureIsUnreachable);
    CPPUNIT_TEST(testUnreachableCopy);
    CPPUNIT_TEST(testNon200IsUnreachable);
    CPPUNIT_TEST(testMalformedJsonIsUnreachable);
    CPPUNIT_TEST(testTruncatedJsonIsUnreachable);
    CPPUNIT_TEST(testEmptyBodyIsUnreachable);
    CPPUNIT_TEST(testMissingStateIsUnreachable);
    CPPUNIT_TEST(testUnknownStateIsUnreachable);
    CPPUNIT_TEST(testNotConfiguredIsUnreachable);
    CPPUNIT_TEST(testErrorStateOffersSignIn);
    CPPUNIT_TEST(testPendingWithoutUrlHasEmptyUrl);
    CPPUNIT_TEST(testNonStringEmailIsIgnored);
    CPPUNIT_TEST_SUITE_END();
};

CPPUNIT_TEST_SUITE_REGISTRATION(StartCenterAccountTest);
}

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
