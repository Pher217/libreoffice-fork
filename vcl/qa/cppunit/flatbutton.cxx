/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <test/bootstrapfixture.hxx>

#include <vcl/wrkwin.hxx>
#include <vcl/toolkit/button.hxx>
#include <vcl/virdev.hxx>

class FlatButtonTest : public test::BootstrapFixture
{
public:
    FlatButtonTest() : BootstrapFixture(true, false) {}

    void testFlatButtonWithControlBackgroundIsNotPaintedWhite();

    CPPUNIT_TEST_SUITE(FlatButtonTest);
    CPPUNIT_TEST(testFlatButtonWithControlBackgroundIsNotPaintedWhite);
    CPPUNIT_TEST_SUITE_END();
};

void FlatButtonTest::testFlatButtonWithControlBackgroundIsNotPaintedWhite()
{
    ScopedVclPtrInstance<WorkWindow> xWin(nullptr, WB_STDWORK);
    xWin->SetSizePixel(Size(200, 100));

    ScopedVclPtrInstance<PushButton> xButton(xWin.get(), WB_FLATBUTTON);
    xButton->SetPosSizePixel(Point(0, 0), Size(80, 24));

    const Color aDark(0x28, 0x2A, 0x36);
    xButton->SetControlBackground(aDark);
    xButton->SetBackground(Wallpaper(aDark));
    xButton->SetPaintTransparent(false);
    xButton->Show();
    xWin->Show();

    ScopedVclPtrInstance<VirtualDevice> xDev;
    xDev->SetOutputSizePixel(Size(80, 24));
    xDev->SetBackground(Wallpaper(aDark));
    xDev->Erase();

    xButton->PaintToDevice(xDev.get(), Point(0, 0));

    Color aCentre = xDev->GetPixel(Point(40, 12));
    CPPUNIT_ASSERT_EQUAL_MESSAGE(
        "flat button with a dark control background must not be filled white",
        aDark, aCentre);
}

CPPUNIT_TEST_SUITE_REGISTRATION(FlatButtonTest);

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
