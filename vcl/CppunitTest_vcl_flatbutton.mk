# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-
#
# This file is part of the LibreOffice project.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

$(eval $(call gb_CppunitTest_CppunitTest,vcl_flatbutton))

$(eval $(call gb_CppunitTest_set_include,vcl_flatbutton,\
    $$(INCLUDE) \
    -I$(SRCDIR)/vcl/inc \
))

$(eval $(call gb_CppunitTest_add_exception_objects,vcl_flatbutton, \
	vcl/qa/cppunit/flatbutton \
))

$(eval $(call gb_CppunitTest_use_externals,vcl_flatbutton,boost_headers))

$(eval $(call gb_CppunitTest_use_libraries,vcl_flatbutton, \
	comphelper \
	cppu \
	cppuhelper \
	sal \
	svt \
	test \
	tk \
	tl \
	unotest \
	vcl \
))

$(eval $(call gb_CppunitTest_add_defs,vcl_flatbutton,\
    -DVCL_INTERNALS \
))

$(eval $(call gb_CppunitTest_use_sdk_api,vcl_flatbutton))

$(eval $(call gb_CppunitTest_use_ure,vcl_flatbutton))
$(eval $(call gb_CppunitTest_use_vcl,vcl_flatbutton))

$(eval $(call gb_CppunitTest_use_components,vcl_flatbutton,\
	configmgr/source/configmgr \
	i18npool/util/i18npool \
	ucb/source/core/ucb1 \
	ucb/source/ucp/file/ucpfile1 \
	framework/util/fwk \
	sfx2/util/sfx \
))

$(eval $(call gb_CppunitTest_use_configuration,vcl_flatbutton))

$(eval $(call gb_CppunitTest_use_uiconfigs,vcl_flatbutton, \
    vcl \
))

# vim: set noet sw=4 ts=4:
