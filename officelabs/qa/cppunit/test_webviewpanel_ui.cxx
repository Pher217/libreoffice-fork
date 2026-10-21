/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <test/bootstrapfixture.hxx>
#include <cppunit/TestAssert.h>
#include <cppunit/extensions/HelperMacros.h>

#include <vcl/svapp.hxx>
#include <vcl/weld/Builder.hxx>
#include <vcl/weld/weld.hxx>
#include <vcl/wrkwin.hxx>

namespace
{
// WebViewPanel welds these ids by name when CEF fails to start (project#368);
// a renamed id would turn the failure path into a null dereference.
class WebViewPanelUiTest : public test::BootstrapFixture
{
    VclPtr<WorkWindow> m_xWindow;
    std::unique_ptr<weld::Builder> m_xBuilder;

public:
    void setUp() override
    {
        test::BootstrapFixture::setUp();
        m_xWindow = VclPtr<WorkWindow>::Create(nullptr, WB_APP | WB_STDWORK);
        m_xBuilder = Application::CreateInterimBuilder(
            m_xWindow, u"officelabs/ui/webviewpanel.ui"_ustr, false);
    }

    void tearDown() override
    {
        m_xBuilder.reset();
        m_xWindow.disposeAndClear();
        test::BootstrapFixture::tearDown();
    }

    void testErrorLabelExists()
    {
        // GIVEN the panel .ui WHEN the error label is welded THEN it exists
        CPPUNIT_ASSERT(m_xBuilder->weld_label(u"cef_error"_ustr));
    }

    void testErrorLabelHiddenByDefault()
    {
        // GIVEN the panel .ui WHEN nothing has failed THEN the error label is hidden
        CPPUNIT_ASSERT(!m_xBuilder->weld_label(u"cef_error"_ustr)->get_visible());
    }

    void testErrorLabelText()
    {
        // GIVEN the panel .ui WHEN the error label is read THEN it carries the user message
        CPPUNIT_ASSERT_EQUAL(
            u"The assistant could not start. Restart OfficeLabs; if it persists, contact support."_ustr,
            m_xBuilder->weld_label(u"cef_error"_ustr)->get_label());
    }

    void testBrowserContainerExists()
    {
        // GIVEN the panel .ui WHEN the browser container is welded THEN it exists
        CPPUNIT_ASSERT(m_xBuilder->weld_widget(u"cef_container"_ustr));
    }

    CPPUNIT_TEST_SUITE(WebViewPanelUiTest);
    CPPUNIT_TEST(testErrorLabelExists);
    CPPUNIT_TEST(testErrorLabelHiddenByDefault);
    CPPUNIT_TEST(testErrorLabelText);
    CPPUNIT_TEST(testBrowserContainerExists);
    CPPUNIT_TEST_SUITE_END();
};

CPPUNIT_TEST_SUITE_REGISTRATION(WebViewPanelUiTest);
}

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
