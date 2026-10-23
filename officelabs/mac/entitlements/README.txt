OfficeLabs CEF sidebar — macOS code-signing entitlements + signing script
=======================================================================

This directory holds the hardened-runtime entitlements and a signing
script for the CEF sidebar app bundle built by the LibreOffice CEF CustomTarget.

Files
-----

app.entitlements
    Entitlements applied to the OUTER app (the browser process). Mirrors the
    canonical Chromium/CEF macOS runtime entitlements that cefsimple relies on:
      - com.apple.security.cs.allow-jit
        V8 needs to mark pages executable (W^X write-then-execute) to JIT JS.
      - com.apple.security.cs.allow-unsigned-executable-memory
        Chromium maps executable memory it did not itself sign (e.g. JIT trampolines).
      - com.apple.security.cs.disable-library-validation
        LibreOffice loads the CEF framework (and other LO dylibs) into the
        browser process; they are not all signed by the same team, so library
        validation must be off or the load is rejected at launch.
      - com.apple.security.cs.allow-dyld-environment-variables
        Lets DYLD_* env vars (used by CEF/LO to locate libraries) pass through
        under the hardened runtime.

helper.entitlements
    Entitlements applied to every Helper .app (Renderer, GPU, Plugin, Alerts,
    and the base Helper). Chromium helpers run V8 / Skia / GPU shaders, so they
    need allow-jit and allow-unsigned-executable-memory too; they also load the
    CEF framework, so disable-library-validation is required. cefsimple ships a
    single shared helper Info.plist for all helper types, so one shared
    helper.entitlements is used for all of them.

sign-mac-bundle.sh
    Ad-hoc (-s -) codesigns the bundle in the inside-out order CEF's own build
    uses for cefsimple:
        1. each Helper .app           (helper entitlements)
        2. Chromium Embedded Framework.framework (helper entitlements)
        3. the outer .app             (app entitlements)
        4. codesign --verify --deep --strict
    Usage:  ./sign-mac-bundle.sh <path-to-app.app>
    Make it executable the first time:  chmod +x sign-mac-bundle.sh
    It already has the executable bit set in this tree.

When to re-run
--------------

Re-run sign-mac-bundle.sh AFTER EVERY CEF-ENABLED BUILD.

The LibreOffice CustomTarget that wires in CEF copies UNSIGNED bundles /
frameworks into the instdir (installation directory) as the last step of the
build. Those copied artifacts are not signed, so a freshly built bundle will
fail Gatekeeper / hardened-runtime checks and the CEF framework will be
refused at load time. Signing is therefore a post-build step, not a build step.

Typical flow:
    make                          # produces unsigned bundle in instdir
    ./sign-mac-bundle.sh instdir/<AppName>.app   # sign + verify

For real distribution
---------------------

Replace the ad-hoc identity "-" with a Developer ID Application identity:
    -s "Developer ID Application: <Org Name> (<TeamID>)"
and drop --timestamp=none in favour of a timestamp server (apple's default
timestamp server is used by codesign automatically when you drop that flag).
Notarisation (--notarize-app via xcrun notarytool) is a separate, later step.