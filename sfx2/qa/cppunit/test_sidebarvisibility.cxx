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

#include <sidebar/SidebarInitialVisibility.hxx>

using sfx2::sidebar::ResolveInitialChildWindowVisibility;

namespace
{
// The sidebar's visibility is persisted per module in the user profile. A
// profile that once hid it opened every later document with no sidebar at all
// (officelabs-project#350); the AI deck lives in the sidebar, so it must open
// with each new document window.
class SidebarInitialVisibilityTest : public CppUnit::TestFixture
{
public:
    // GIVEN a profile that persisted the sidebar hidden
    // WHEN a desktop document window initialises it
    // THEN it opens visible.
    void testPersistedHiddenSidebarOpens()
    {
        CPPUNIT_ASSERT(ResolveInitialChildWindowVisibility(SID_SIDEBAR, false, false));
    }

    // GIVEN a profile that persisted the sidebar visible
    // WHEN a desktop document window initialises it
    // THEN it stays visible.
    void testPersistedVisibleSidebarOpens()
    {
        CPPUNIT_ASSERT(ResolveInitialChildWindowVisibility(SID_SIDEBAR, true, false));
    }

    // GIVEN another child window (the navigator) persisted hidden
    // WHEN a document window initialises it
    // THEN the persisted state is kept: only the sidebar is forced open.
    void testOtherChildWindowKeepsPersistedHidden()
    {
        CPPUNIT_ASSERT(!ResolveInitialChildWindowVisibility(SID_NAVIGATOR, false, false));
    }

    // GIVEN another child window persisted visible
    // WHEN a document window initialises it
    // THEN the persisted state is kept.
    void testOtherChildWindowKeepsPersistedVisible()
    {
        CPPUNIT_ASSERT(ResolveInitialChildWindowVisibility(SID_NAVIGATOR, true, false));
    }

    // GIVEN LibreOfficeKit and a sidebar persisted hidden
    // WHEN a view initialises it
    // THEN it stays hidden: LOK clients open the sidebar on demand.
    void testLibreOfficeKitKeepsPersistedHidden()
    {
        CPPUNIT_ASSERT(!ResolveInitialChildWindowVisibility(SID_SIDEBAR, false, true));
    }

    CPPUNIT_TEST_SUITE(SidebarInitialVisibilityTest);
    CPPUNIT_TEST(testPersistedHiddenSidebarOpens);
    CPPUNIT_TEST(testPersistedVisibleSidebarOpens);
    CPPUNIT_TEST(testOtherChildWindowKeepsPersistedHidden);
    CPPUNIT_TEST(testOtherChildWindowKeepsPersistedVisible);
    CPPUNIT_TEST(testLibreOfficeKitKeepsPersistedHidden);
    CPPUNIT_TEST_SUITE_END();
};

CPPUNIT_TEST_SUITE_REGISTRATION(SidebarInitialVisibilityTest);
}

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
