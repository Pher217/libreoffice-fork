# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-
#
# This file is part of the LibreOffice project.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

# Not CEF-gated: StartCenterAccount.cxx is a pure function, so this runs in
# every tree (project#418).
$(eval $(call gb_CppunitTest_CppunitTest,officelabs_startcenter))

$(eval $(call gb_CppunitTest_set_include,officelabs_startcenter,\
	$$(INCLUDE) \
	-I$(SRCDIR)/officelabs/inc \
))

$(eval $(call gb_CppunitTest_add_exception_objects,officelabs_startcenter, \
	officelabs/qa/cppunit/test_start_center_account \
))

$(eval $(call gb_CppunitTest_use_externals,officelabs_startcenter,boost_headers))

$(eval $(call gb_CppunitTest_use_libraries,officelabs_startcenter, \
	officelabs \
	sal \
))

$(eval $(call gb_CppunitTest_use_sdk_api,officelabs_startcenter))

# vim: set noet sw=4 ts=4:
