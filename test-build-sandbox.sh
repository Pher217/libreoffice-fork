#!/usr/bin/env bash
# test-build-sandbox.sh — tests for build-sandbox.sh.
#
# Drives the real script against throwaway git repos, so the guard and sync
# logic are exercised without a LibreOffice build (BUILD_CMD=true replaces
# gmake). Run: ./test-build-sandbox.sh

set -uo pipefail
SUT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/build-sandbox.sh"
REAL_FORK="/Users/philippehermann/dev/Officelabs-suite/libreoffice-fork"
PASS=0; FAIL=0; FIXTURE=""

ok()  { PASS=$((PASS+1)); printf '  PASS  %s\n' "$1"; }
bad() { FAIL=$((FAIL+1)); printf '  FAIL  %s\n          expected: [%s]\n          actual:   [%s]\n' "$1" "$2" "$3"; }
eq()  { [ "$2" = "$3" ] && ok "$1" || bad "$1" "$2" "$3"; }

# Sets FIXTURE. Never prints to stdout: a previous version captured the path via
# $(...) and a git hook's warning landed in the variable, pointing the whole
# suite at the real fork. Hence also the hard stop below.
new_fixture() {
  FIXTURE=$(mktemp -d)
  {
    git -C "$FIXTURE" init -q .
    git -C "$FIXTURE" config user.email t@t
    git -C "$FIXTURE" config user.name t
    git -C "$FIXTURE" config core.hooksPath /dev/null   # fixture must not run the fork's hooks
    mkdir -p "$FIXTURE/officelabs" "$FIXTURE/other"
    echo original > "$FIXTURE/officelabs/a.cxx"
    echo shared   > "$FIXTURE/other/b.cxx"
    git -C "$FIXTURE" add -A
    git -C "$FIXTURE" commit -q -m base
    base=$(git -C "$FIXTURE" rev-parse HEAD)
    echo patched > "$FIXTURE/officelabs/a.cxx"
    git -C "$FIXTURE" add -A
    git -C "$FIXTURE" commit -q -m feature
    git -C "$FIXTURE" branch -f feature HEAD
    git -C "$FIXTURE" reset -q --hard "$base"
  } >/dev/null 2>&1
  cd "$FIXTURE" || exit 1
  case "$(pwd -P)" in
    "$REAL_FORK"*) echo "FATAL: fixture resolved into the real fork — refusing"; exit 1 ;;
  esac
}

echo "== build-sandbox.sh =="
new_fixture

# The check whose absence let an earlier version of this suite pass while
# exercising the real fork: prove the script resolved OUR fixture.
resolved=$("$SUT" --sandbox "$FIXTURE" --branch feature --paths officelabs --dry-run 2>&1 | sed -n 's/^sandbox: //p')
eq "the script resolves the fixture, not the real checkout" "$(cd "$FIXTURE" && pwd -P)" "$(cd "$resolved" && pwd -P)"

# --- usage ------------------------------------------------------------------
"$SUT" --sandbox "$FIXTURE" --dry-run >/dev/null 2>&1;                        eq "no --branch exits 2 (usage)" 2 $?
"$SUT" --sandbox "$FIXTURE" --branch nope --dry-run >/dev/null 2>&1;          eq "unknown branch exits 1" 1 $?

# --- dry run on a clean sandbox writes nothing -------------------------------
before=$(md5 -q officelabs/a.cxx)
out=$("$SUT" --sandbox "$FIXTURE" --branch feature --paths officelabs --dry-run 2>&1); rc=$?
eq "clean dry-run exits 0" 0 "$rc"
eq "clean dry-run leaves the sandbox untouched" "$before" "$(md5 -q officelabs/a.cxx)"
case "$out" in *officelabs/a.cxx*) ok "dry-run names the file it would rewrite" ;;
  *) bad "dry-run names the file it would rewrite" "mentions officelabs/a.cxx" "$out" ;; esac

# --- the guard: a dirty sandbox is refused and left alone --------------------
echo "deliberate probe" > other/b.cxx
probe=$(md5 -q other/b.cxx)
"$SUT" --sandbox "$FIXTURE" --branch feature --paths officelabs >/dev/null 2>&1
eq "dirty sandbox refuses with exit 1" 1 $?
eq "refusal leaves the probe byte-identical" "$probe" "$(md5 -q other/b.cxx)"
eq "refusal does not sync the branch either" "original" "$(cat officelabs/a.cxx)"
"$SUT" --sandbox "$FIXTURE" --branch feature --paths officelabs --dry-run >/dev/null 2>&1
eq "dry-run still succeeds on a dirty sandbox" 0 $?
git -C "$FIXTURE" checkout -q -- other/b.cxx

# --- a real run syncs, rewriting only what differs ---------------------------
head_before=$(git -C "$FIXTURE" rev-parse HEAD)
touch -t 200001010000 officelabs/a.cxx other/b.cxx
BUILD_CMD=true "$SUT" --sandbox "$FIXTURE" --branch feature --paths . >/dev/null 2>&1
eq "clean real run exits 0" 0 $?
eq "the differing file was synced" "patched" "$(cat officelabs/a.cxx)"
a=$(stat -f %m officelabs/a.cxx); b=$(stat -f %m other/b.cxx)
[ "$a" -gt "$b" ] && ok "only the differing file was rewritten (identical kept its mtime)" \
                  || bad "only the differing file was rewritten" "a.cxx mtime > b.cxx mtime" "$a vs $b"
eq "HEAD was not moved" "$head_before" "$(git -C "$FIXTURE" rev-parse HEAD)"
eq "the index was not staged" "" "$(git -C "$FIXTURE" diff --cached --name-only)"

cd /; rm -rf "$FIXTURE"
echo; echo "  $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
