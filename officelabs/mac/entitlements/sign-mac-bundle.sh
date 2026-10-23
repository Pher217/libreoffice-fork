#!/bin/sh
# sign-mac-bundle.sh — ad-hoc codesign the OfficeLabs CEF sidebar app bundle.
#
# Usage:  ./sign-mac-bundle.sh <path-to-app.app>
#
# Mirrors the inside-out signing order CEF's own build uses for cefsimple:
#   1. each Helper .app  (renderer/GPU/plugin/alerts + base helper)
#   2. Chromium Embedded Framework.framework
#   3. the outer .app (browser process)
#   4. codesign --verify --deep --strict
#
# -s -  = ad-hoc signing (no Developer ID). For distribution, replace "-"
#         with a real "Developer ID Application: ..." identity and drop
#         --timestamp=none in favour of a timestamp server.
# --options runtime enables the hardened runtime; the entitlements below
#         relax it just enough for Chromium/CEF to JIT and load the framework.
#
# Make executable:  chmod +x sign-mac-bundle.sh

set -eu

APP="${1:?usage: $0 <path-to-app.app>}"
[ -d "$APP" ] || { echo "error: not a dir: $APP" >&2; exit 1; }

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
APP_ENT="$SCRIPT_DIR/app.entitlements"
HELPER_ENT="$SCRIPT_DIR/helper.entitlements"

for f in "$APP_ENT" "$HELPER_ENT"; do
    [ -f "$f" ] || { echo "error: missing entitlements: $f" >&2; exit 1; }
done

FRAMEWORKS="$APP/Contents/Frameworks"

sign_app() {
    # $1 = bundle path, $2 = entitlements plist
    codesign -f -s - --timestamp=none --options runtime --entitlements "$2" "$1"
}

# 1. Helpers (inside-out: sign leaf helpers first).
for helper in \
    "$FRAMEWORKS/Chromium Embedded Framework.framework/Helpers/"*.app \
    "$FRAMEWORKS/"*Helper*.app; do
    [ -d "$helper" ] || continue
    case "$helper" in
        *"Chromium Embedded Framework.framework"*) ;;
        *) sign_app "$helper" "$HELPER_ENT" ;;
    esac
done

# 2. The CEF framework bundle itself.
sign_app "$FRAMEWORKS/Chromium Embedded Framework.framework" "$HELPER_ENT"

# 3. The outer app (browser process).
sign_app "$APP" "$APP_ENT"

# 4. Verify the whole nested bundle.
codesign --verify --deep --strict "$APP"

echo "Signed OK: $APP"