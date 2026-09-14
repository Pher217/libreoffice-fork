# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-

$(eval $(call gb_Module_Module,officelabs))

$(eval $(call gb_Module_add_targets,officelabs,\
    Library_officelabs \
    UIConfig_officelabs \
))

$(eval $(call gb_Module_add_check_targets,officelabs,\
	CppunitTest_officelabs_inline \
	CppunitTest_officelabs_cursor \
))

# CEF WebView support (conditional on --with-cef)
ifeq ($(ENABLE_CEF),TRUE)
$(eval $(call gb_Module_add_targets,officelabs,\
    Executable_officelabs_cef_subprocess \
    Package_cef \
))
# macOS: assemble + install the CEF framework and the five Helper .app bundles
# into Contents/Frameworks. Their paths contain spaces, which gbuild's Package
# list macros cannot express, so a CustomTarget does the space-safe shell copy.
ifeq ($(OS),MACOSX)
$(eval $(call gb_Module_add_targets,officelabs,\
    CustomTarget_cef_mac_bundle \
))
endif
endif

# vim: set noet sw=4 ts=4:
