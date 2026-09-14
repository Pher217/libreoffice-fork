# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-
#
# This file is part of the LibreOffice project.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

$(eval $(call gb_CppunitTest_CppunitTest,officelabs_cursor))

$(eval $(call gb_CppunitTest_set_include,officelabs_cursor,\
	$$(INCLUDE) \
	-I$(SRCDIR)/officelabs/inc \
))

$(eval $(call gb_CppunitTest_add_exception_objects,officelabs_cursor, \
	officelabs/qa/cppunit/test_documentcontroller_cursor \
))

$(eval $(call gb_CppunitTest_use_libraries,officelabs_cursor, \
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

$(eval $(call gb_CppunitTest_use_externals,officelabs_cursor,\
	boost_headers \
	libxml2 \
))

$(eval $(call gb_CppunitTest_use_sdk_api,officelabs_cursor))

$(eval $(call gb_CppunitTest_use_ure,officelabs_cursor))
$(eval $(call gb_CppunitTest_use_vcl,officelabs_cursor))

$(eval $(call gb_CppunitTest_use_rdb,officelabs_cursor,services))

$(eval $(call gb_CppunitTest_use_configuration,officelabs_cursor))

# vim: set noet sw=4 ts=4:
