# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-
#
# This file is part of the LibreOffice project.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

$(eval $(call gb_CppunitTest_CppunitTest,sfx2_oltheme))

$(eval $(call gb_CppunitTest_set_include,sfx2_oltheme,\
    $$(INCLUDE) \
    -I$(SRCDIR)/sfx2/inc \
))

$(eval $(call gb_CppunitTest_add_exception_objects,sfx2_oltheme, \
	sfx2/qa/cppunit/test_oltheme \
))

$(eval $(call gb_CppunitTest_use_externals,sfx2_oltheme,boost_headers))

$(eval $(call gb_CppunitTest_use_libraries,sfx2_oltheme, \
	sal \
	tl \
))

$(eval $(call gb_CppunitTest_use_sdk_api,sfx2_oltheme))

# vim: set noet sw=4 ts=4:
