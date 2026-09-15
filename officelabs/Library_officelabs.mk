# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-

$(eval $(call gb_Library_Library,officelabs))

$(eval $(call gb_Library_add_defs,officelabs,\
    -DOFFICELABS_DLLIMPLEMENTATION \
))

$(eval $(call gb_Library_set_include,officelabs,\
    -I$(SRCDIR)/officelabs/inc \
    $$(INCLUDE) \
))

$(eval $(call gb_Library_use_sdk_api,officelabs))

$(eval $(call gb_Library_use_libraries,officelabs,\
    comphelper \
    cppu \
    cppuhelper \
    sal \
    sfx \
    svl \
    svt \
    svx \
    svxcore \
    tk \
    tl \
    utl \
    vcl \
    i18nlangtag \
))

$(eval $(call gb_Library_use_externals,officelabs,\
    boost_headers \
    curl \
))

$(eval $(call gb_Library_add_exception_objects,officelabs,\
    officelabs/source/AgentIdentity \
    officelabs/source/CefDebugPort \
    officelabs/source/DocumentController \
    officelabs/source/InlineCompletionEligibility \
))

# === CEF WebView support (conditional on --with-cef) ===
ifeq ($(ENABLE_CEF),TRUE)

$(eval $(call gb_Library_add_defs,officelabs,\
    -DHAVE_FEATURE_CEF \
))

$(eval $(call gb_Library_set_include,officelabs,\
    -I$(SRCDIR)/officelabs/inc \
    -I$(CEF_DIR) \
    -I$(CEF_DIR)/include \
    $$(INCLUDE) \
))

# Platform-specific CEF link inputs.
ifeq ($(OS),MACOSX)
# macOS: link the CEF C++ wrapper static lib built from libcef_dll/ (it also
# provides CefScopedLibraryLoader). The framework itself is dlopen'd at runtime
# via the loader, so it is NOT linked here; CEF_LIBS is empty on macOS.
$(eval $(call gb_Library_add_libs,officelabs,\
    $(CEF_DIR)/build/libcef_dll_wrapper/libcef_dll_wrapper.a \
))
$(eval $(call gb_Library_use_system_darwin_frameworks,officelabs,\
    AppKit \
    Cocoa \
    CoreFoundation \
    IOSurface \
))
else
# Windows: import lib (CEF_LIBS = libcef.lib) + the wrapper static lib.
$(eval $(call gb_Library_add_libs,officelabs,\
    $(CEF_LIBS) \
    $(CEF_DIR)/libcef_dll_wrapper/Release/libcef_dll_wrapper.lib \
))
$(eval $(call gb_Library_use_system_win32_libs,officelabs,\
    comctl32 \
))
endif

# Sources shared by all CEF platforms.
$(eval $(call gb_Library_add_exception_objects,officelabs,\
    officelabs/source/CefInit \
    officelabs/source/ConsentBridge \
    officelabs/source/AgentHttp \
    officelabs/source/TrustedUrl \
    officelabs/source/StudioWindow \
    officelabs/source/WebViewPanel \
    officelabs/source/WebViewMessageHandler \
    officelabs/source/OfficelabsBrowserApp \
    officelabs/source/GhostTextWindow \
    officelabs/source/InlineCompletionController \
))

# macOS-only Objective-C++ message-pump shim (dispatch to the AppKit main thread).
ifeq ($(OS),MACOSX)
$(eval $(call gb_Library_add_objcxxobjects,officelabs,\
    officelabs/source/MessagePumpMac \
    officelabs/source/StudioWindowMac \
))
endif

# INativeCefHost platform impls (Decision 5) are created by a later port step.
# Register per-platform once present; warn (do not hard-fail) while still
# missing so the interim gap is obvious in a CEF-enabled build.
ifeq ($(OS),MACOSX)
ifeq ($(wildcard $(SRCDIR)/officelabs/source/WebViewPanelHostMac.mm),)
$(warning officelabs: WebViewPanelHostMac.mm not present yet -- INativeCefHost macOS impl pending (Decision 5))
else
$(eval $(call gb_Library_add_objcxxobjects,officelabs,\
    officelabs/source/WebViewPanelHostMac \
))
endif
else ifeq ($(OS),WNT)
ifeq ($(wildcard $(SRCDIR)/officelabs/source/WebViewPanelHostWin.cxx),)
$(warning officelabs: WebViewPanelHostWin.cxx not present yet -- INativeCefHost Windows impl pending (Decision 5))
else
$(eval $(call gb_Library_add_exception_objects,officelabs,\
    officelabs/source/WebViewPanelHostWin \
))
endif
endif

endif
# === End CEF ===

# vim: set noet sw=4 ts=4:
