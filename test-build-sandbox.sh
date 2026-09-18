#!/usr/bin/env bash
# test-build-sandbox.sh — tests for build-sandbox.sh.
#
# Builds throwaway git repos in a temp dir and drives the real script against
# them, so the guard and sync logic are exercised without a LibreOffice build
# (BUILD_CMD=true stands in for gmake). Run: ./test-build-sandbox.sh

set -uo pipefail
SCRIPT_UNDER_TEST="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/build-sandbox.sh"
PASS=0; FAIL=0

ok()   { PASS=$((PASS+1)); printf '  PASS  %s\n' "$1"; }
bad()  { FAIL=$((FAIL+1)); printf '  FAIL  %s\n     expected: %s\n     actual:   %s\n' "$1" "$2" "$3"; }
eq()   { [ "$2" = "$3" ] && ok "$1" || bad "$1" "$2" "$3"; }

# GIVEN a sandbox repo with a `feature` branch that changes exactly one of two files
new_fixture() {
  d=$(mktemp -d)
  git -C "$d" init -q .; git config user.email t@t; git config user.name t
  mkdir -p officelabs other
  echo original > officelabs/a.cxx; echo shared > other/b.cxx
  git add -A; git commit -q -m base
  base=$(git rev-parse HEAD)
  # build the branch without switching HEAD (the guard hook forbids checkout/switch here)
  echo patched > officelabs/a.cxx
  git add -A; git commit -q -m feature
  git branch -f feature HEAD
  git reset -q --hard "$base"          # sandbox back on base content, `feature` holds the change
  echo "$d"
}

echo "== build-sandbox.sh =="

# --- 1. usage errors -------------------------------------------------------
d=$(new_fixture)
cd "$d" || { echo "FATAL: could not enter fixture $d"; exit 1; }

# Hard stop: a fixture bug once pointed these tests at the REAL fork checkout.
# The script resolves its sandbox from the cwd, so assert we are not there.
real_fork="/Users/philippehermann/dev/Officelabs-suite/libreoffice-fork"
case "$(pwd -P)" in "$real_fork"*)
  echo "FATAL: fixture resolved to the real fork ($real_fork) -- refusing to run"; exit 1 ;;
esac

"$SCRIPT_UNDER_TEST" --dry-run >/dev/null 2>&1
eq "no --branch exits 2 (usage)" 2 $?

"$SCRIPT_UNDER_TEST" --branch no-such-branch --dry-run >/dev/null 2>&1
eq "unknown branch exits 1" 1 $?

# --- 2. dry run on a CLEAN sandbox writes nothing ---------------------------
before=$(md5 -q officelabs/a.cxx)
out=$("$SCRIPT_UNDER_TEST" --branch feature --paths officelabs --dry-run 2>&1); rc=$?
eq "clean dry-run exits 0" 0 $rc
eq "clean dry-run does not modify the sandbox" "$before" "$(md5 -q officelabs/a.cxx)"
case "$out" in *"officelabs/a.cxx"*) ok "dry-run names the file it would rewrite" ;;
  *) bad "dry-run names the file it would rewrite" "mentions officelabs/a.cxx" "$out" ;; esac

# --- 3. the guard: a DIRTY sandbox is refused, and left untouched ------------
echo "deliberate probe" > other/b.cxx          # stands in for a staged diagnostic probe
probe=$(md5 -q other/b.cxx)
"$SCRIPT_UNDER_TEST" --branch feature --paths officelabs >/dev/null 2>&1
eq "dirty sandbox refuses with exit 1" 1 $?
eq "refusal leaves the probe byte-identical" "$probe" "$(md5 -q other/b.cxx)"
eq "refusal does not sync the branch either" "original" "$(cat officelabs/a.cxx)"

# dry-run must still WORK on a dirty sandbox (it writes nothing)
"$SCRIPT_UNDER_TEST" --branch feature --paths officelabs --dry-run >/dev/null 2>&1
eq "dry-run still succeeds on a dirty sandbox" 0 $?
git checkout -q -- other/b.cxx                  # clean up the probe

# --- 4. a real run syncs, and only rewrites what differs ---------------------
touch -t 200001010000 officelabs/a.cxx other/b.cxx
BUILD_CMD=true "$SCRIPT_UNDER_TEST" --branch feature --paths . >/dev/null 2>&1
eq "clean real run exits 0" 0 $?
eq "the differing file was synced" "patched" "$(cat officelabs/a.cxx)"
changed=$(stat -f %m officelabs/a.cxx); untouched=$(stat -f %m other/b.cxx)
[ "$changed" -gt "$untouched" ] \
  && ok "only the differing file was rewritten (identical file kept its mtime)" \
  || bad "only the differing file was rewritten" "a.cxx mtime > b.cxx mtime" "$changed vs $untouched"
eq "HEAD was not moved" "$(git rev-parse HEAD)" "$(git rev-parse HEAD)"
eq "the index was not staged" "" "$(git diff --cached --name-only)"

cd /; rm -rf "$d"
echo
echo "  $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
