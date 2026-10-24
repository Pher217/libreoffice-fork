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

#include <sidebar/SidebarRestoredWidth.hxx>

using sfx2::sidebar::ExpandCollapsedSidebarWidth;

namespace
{
// Tab bar 35 px, so the deck opens at 75 px; the expanded default is 330 px.
constexpr sal_Int32 OPEN_WIDTH = 75;
constexpr sal_Int32 EXPANDED_WIDTH = 330;

// A sidebar collapsed to its tab-bar strip persisted that width, and every
// later launch opened with the AI deck closed (officelabs-project#352).
class SidebarRestoredWidthTest : public CppUnit::TestFixture
{
public:
    // GIVEN the profile string a collapsed sidebar left on quit (mac, 2026-10-16)
    // WHEN a document window restores the sidebar
    // THEN both persisted widths are the expanded width.
    void testCollapsedWidthIsExpanded()
    {
        CPPUNIT_ASSERT_EQUAL(u"AL:(5,16,0/0/330/450,330;691)"_ustr,
                             ExpandCollapsedSidebarWidth(u"AL:(5,16,0/0/35/450,35;691)"_ustr,
                                                         OPEN_WIDTH, EXPANDED_WIDTH));
    }

    // GIVEN a sidebar left wider than the tab bar but too narrow to show the deck
    // WHEN it is restored
    // THEN it is expanded too.
    void testWidthBelowOpenThresholdIsExpanded()
    {
        CPPUNIT_ASSERT_EQUAL(u"AL:(5,16,0/0/330/450,330;691)"_ustr,
                             ExpandCollapsedSidebarWidth(u"AL:(5,16,0/0/74/450,74;691)"_ustr,
                                                         OPEN_WIDTH, EXPANDED_WIDTH));
    }

    // GIVEN a sidebar the user left open at a width of their choosing
    // WHEN it is restored
    // THEN the width is kept.
    void testOpenWidthIsKept()
    {
        CPPUNIT_ASSERT_EQUAL(u"AL:(5,16,0/0/520/450,520;691)"_ustr,
                             ExpandCollapsedSidebarWidth(u"AL:(5,16,0/0/520/450,520;691)"_ustr,
                                                         OPEN_WIDTH, EXPANDED_WIDTH));
    }

    // GIVEN a sidebar exactly as wide as the deck needs to open
    // WHEN it is restored
    // THEN the width is kept.
    void testWidthAtOpenThresholdIsKept()
    {
        CPPUNIT_ASSERT_EQUAL(u"AL:(5,16,0/0/75/450,75;691)"_ustr,
                             ExpandCollapsedSidebarWidth(u"AL:(5,16,0/0/75/450,75;691)"_ustr,
                                                         OPEN_WIDTH, EXPANDED_WIDTH));
    }

    // GIVEN a collapsed sidebar whose extra string carries text around the alignment block
    // WHEN it is restored
    // THEN only the widths inside the block change.
    void testSurroundingTextIsKept()
    {
        CPPUNIT_ASSERT_EQUAL(u"X,AL:(5,16,0/0/330/450,330;691)Y"_ustr,
                             ExpandCollapsedSidebarWidth(u"X,AL:(5,16,0/0/35/450,35;691)Y"_ustr,
                                                         OPEN_WIDTH, EXPANDED_WIDTH));
    }

    // GIVEN a first start: the profile has no extra string yet
    // WHEN the sidebar is created
    // THEN nothing is invented.
    void testEmptyStringIsKept()
    {
        CPPUNIT_ASSERT_EQUAL(OUString(),
                             ExpandCollapsedSidebarWidth(OUString(), OPEN_WIDTH, EXPANDED_WIDTH));
    }

    // GIVEN an alignment block without the split size
    // WHEN it is restored
    // THEN it is returned unchanged for SfxDockingWindow to handle as before.
    void testMalformedStringIsKept()
    {
        CPPUNIT_ASSERT_EQUAL(u"AL:(5,16,0/0/35/450)"_ustr,
                             ExpandCollapsedSidebarWidth(u"AL:(5,16,0/0/35/450)"_ustr, OPEN_WIDTH,
                                                         EXPANDED_WIDTH));
    }

    // GIVEN a collapsed split width whose horizontal size is already open
    // WHEN it is restored
    // THEN only the split width changes.
    void testOnlyCollapsedSplitWidthChanges()
    {
        CPPUNIT_ASSERT_EQUAL(u"AL:(5,16,0/0/520/450,330;691)"_ustr,
                             ExpandCollapsedSidebarWidth(u"AL:(5,16,0/0/520/450,35;691)"_ustr,
                                                         OPEN_WIDTH, EXPANDED_WIDTH));
    }

    // GIVEN a split size without its ';' separator
    // WHEN it is restored
    // THEN it is returned unchanged.
    void testSplitWithoutSemicolonIsKept()
    {
        CPPUNIT_ASSERT_EQUAL(u"AL:(5,16,0/0/35/450,35)"_ustr,
                             ExpandCollapsedSidebarWidth(u"AL:(5,16,0/0/35/450,35)"_ustr,
                                                         OPEN_WIDTH, EXPANDED_WIDTH));
    }

    // GIVEN a negative split width, which SfxDockingWindow rejects
    // WHEN it is restored
    // THEN it is returned unchanged.
    void testNegativeWidthIsKept()
    {
        CPPUNIT_ASSERT_EQUAL(u"AL:(5,16,0/0/35/450,-1;691)"_ustr,
                             ExpandCollapsedSidebarWidth(u"AL:(5,16,0/0/35/450,-1;691)"_ustr,
                                                         OPEN_WIDTH, EXPANDED_WIDTH));
    }

    CPPUNIT_TEST_SUITE(SidebarRestoredWidthTest);
    CPPUNIT_TEST(testOnlyCollapsedSplitWidthChanges);
    CPPUNIT_TEST(testSplitWithoutSemicolonIsKept);
    CPPUNIT_TEST(testNegativeWidthIsKept);
    CPPUNIT_TEST(testCollapsedWidthIsExpanded);
    CPPUNIT_TEST(testWidthBelowOpenThresholdIsExpanded);
    CPPUNIT_TEST(testOpenWidthIsKept);
    CPPUNIT_TEST(testWidthAtOpenThresholdIsKept);
    CPPUNIT_TEST(testSurroundingTextIsKept);
    CPPUNIT_TEST(testEmptyStringIsKept);
    CPPUNIT_TEST(testMalformedStringIsKept);
    CPPUNIT_TEST_SUITE_END();
};

CPPUNIT_TEST_SUITE_REGISTRATION(SidebarRestoredWidthTest);
}

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
