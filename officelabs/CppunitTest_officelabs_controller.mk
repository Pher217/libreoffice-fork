# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-
#
# This file is part of the LibreOffice project.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

$(eval $(call gb_CppunitTest_CppunitTest,officelabs_controller))

$(eval $(call gb_CppunitTest_set_include,officelabs_controller,\
    $$(INCLUDE) \
    -I$(SRCDIR)/officelabs/inc \
))

$(eval $(call gb_CppunitTest_add_exception_objects,officelabs_controller, \
    officelabs/qa/cppunit/test_inline_controller \
    officelabs/qa/cppunit/test_webview_message_handler \
))

$(eval $(call gb_CppunitTest_use_libraries,officelabs_controller, \
    officelabs \
    comphelper \
    cppu \
    cppuhelper \
    test \
    unotest \
    vcl \
    sal \
    subsequenttest \
    sfx \
    utl \
    tl \
))

$(eval $(call gb_CppunitTest_use_externals,officelabs_controller,\
    boost_headers \
    libxml2 \
))

$(eval $(call gb_CppunitTest_use_sdk_api,officelabs_controller))

$(eval $(call gb_CppunitTest_use_ure,officelabs_controller))
$(eval $(call gb_CppunitTest_use_vcl,officelabs_controller))

$(eval $(call gb_CppunitTest_use_rdb,officelabs_controller,services))

$(eval $(call gb_CppunitTest_use_configuration,officelabs_controller))

# vim: set noet sw=4 ts=4:
