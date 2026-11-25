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

#include <officelabs/PanelHostVisibility.hxx>

using officelabs::HostAction;
using officelabs::hostActionForDocumentState;

namespace
{
class PanelHostVisibilityTest : public CppUnit::TestFixture
{
public:
    // GIVEN the Start Center (no document) and a host that is still shown
    // WHEN the host action is decided THEN the host is hidden.
    void testNoDocumentHidesShownHost()
    {
        CPPUNIT_ASSERT(HostAction::Hide == hostActionForDocumentState(false, false));
    }

    // GIVEN the Start Center and a host already hidden for lack of a document
    // (a re-attach may have shown it again) WHEN decided THEN it is hidden again.
    void testNoDocumentKeepsHostHidden()
    {
        CPPUNIT_ASSERT(HostAction::Hide == hostActionForDocumentState(false, true));
    }

    // GIVEN a document opened after the host was hidden on the Start Center
    // WHEN the host action is decided THEN the host is shown again.
    void testDocumentAppearsShowsHiddenHost()
    {
        CPPUNIT_ASSERT(HostAction::Show == hostActionForDocumentState(true, true));
    }

    // GIVEN a document is open and the host was never hidden for lack of one
    // WHEN the host action is decided THEN nothing is done and the native
    // host keeps its own minimize/fullscreen gating.
    void testDocumentOpenLeavesHostAlone()
    {
        CPPUNIT_ASSERT(HostAction::None == hostActionForDocumentState(true, false));
    }

    CPPUNIT_TEST_SUITE(PanelHostVisibilityTest);
    CPPUNIT_TEST(testNoDocumentHidesShownHost);
    CPPUNIT_TEST(testNoDocumentKeepsHostHidden);
    CPPUNIT_TEST(testDocumentAppearsShowsHiddenHost);
    CPPUNIT_TEST(testDocumentOpenLeavesHostAlone);
    CPPUNIT_TEST_SUITE_END();
};

CPPUNIT_TEST_SUITE_REGISTRATION(PanelHostVisibilityTest);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
