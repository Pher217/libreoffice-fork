# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-
#
# This file is part of the LibreOffice project.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

$(eval $(call gb_CppunitTest_CppunitTest,officelabs_inline))

$(eval $(call gb_CppunitTest_set_include,officelabs_inline,\
    $$(INCLUDE) \
    -I$(SRCDIR)/officelabs/inc \
))

$(eval $(call gb_CppunitTest_add_exception_objects,officelabs_inline, \
	officelabs/qa/cppunit/test_inline_eligibility \
))

$(eval $(call gb_CppunitTest_use_externals,officelabs_inline,boost_headers))

$(eval $(call gb_CppunitTest_use_libraries,officelabs_inline, \
	officelabs \
	sal \
))

$(eval $(call gb_CppunitTest_use_sdk_api,officelabs_inline))

# vim: set noet sw=4 ts=4:
