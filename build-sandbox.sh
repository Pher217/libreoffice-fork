#!/usr/bin/env bash
# build-sandbox.sh — build a worktree's branch using the ONE build tree.
#
# WHY THIS EXISTS
# `config_host.mk` hardcodes SRCDIR, BUILDDIR, WORKDIR and INSTDIR to one
# absolute path (the main checkout). gbuild derives every path from those, so a
# git worktree cannot share this build tree: two worktrees pointed at one
# workdir would race and leave objects from mixed sources with no way to tell
# which source produced which object.
#
# A worktree CAN have its own build tree -- LibreOffice supports SRCDIR !=
# BUILDDIR -- but that costs ~7.3 GB (workdir 6.3G + instdir 998M) and a full
# build, which is hours. That is worth it for a long-lived branch and absurd for
# a task worktree that lives a day.
#
# So: worktrees hold the commits, and the main checkout is a BUILD SANDBOX whose
# contents are driven from a branch. This script does that driving, instead of
# the hand-copying that lost work on 2026-09-16 and pushed a worker into routing
# around the branch-isolation hook with Bash on 2026-09-17 (project#202).
#
# HOW THE SYNC WORKS
#   git restore --source=<branch> --worktree <paths>
# rewrites ONLY files whose content differs, leaves every identical file's mtime
# untouched, and moves neither HEAD nor the index. Verified 2026-09-18: of two
# files, only the differing one was rewritten (mtime bumped); the identical one
# kept its mtime, HEAD stayed put, and `git status` showed a worktree-only
# modification. That mtime property is the whole point -- gbuild rebuilds
# exactly the files you changed, so a one-module edit stays a ~2 minute
# incremental build rather than a fresh one.
#
# USAGE
#   ./build-sandbox.sh --branch <branch> [--paths <p>...] [--make-args "..."]
#   ./build-sandbox.sh --branch <branch> --dry-run
#   ./build-sandbox.sh --restore                 # put the sandbox back on master
#   ./build-sandbox.sh --sandbox <path> …        # target another checkout (used by the tests)
#
# The sandbox is left carrying the branch content after a build, on purpose --
# you usually want to run what you just built. Run --restore when done.

set -euo pipefail

BRANCH=""; DRY=0; RESTORE=0; MAKE_ARGS=""; SANDBOX_OPT=""; PATHS=()
while [ $# -gt 0 ]; do
  case "$1" in
    --sandbox)   SANDBOX_OPT="$2"; shift 2 ;;
    --branch)    BRANCH="$2"; shift 2 ;;
    --paths)     shift; while [ $# -gt 0 ] && [ "${1#--}" = "$1" ]; do PATHS+=("$1"); shift; done ;;
    --make-args) MAKE_ARGS="$2"; shift 2 ;;
    --dry-run)   DRY=1; shift ;;
    --restore)   RESTORE=1; shift ;;
    -h|--help)   sed -n '2,40p' "${BASH_SOURCE[0]}"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

# Resolve the sandbox. Default: this script lives IN the sandbox (the main
# checkout), so start from its own location -- which stays correct when the
# script is invoked from a worktree copy, because --git-common-dir then points
# back at the main checkout. --sandbox overrides it, which is what makes the
# script testable: without it the test suite silently exercised the real fork
# instead of its fixture, and passed.
if [ -n "$SANDBOX_OPT" ]; then
  SANDBOX=$(cd "$SANDBOX_OPT" && pwd) || { echo "!! no such sandbox: $SANDBOX_OPT" >&2; exit 2; }
else
  SANDBOX="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
fi
cdir=$(git -C "$SANDBOX" rev-parse --git-common-dir)
case "$cdir" in /*) ;; *) cdir="$SANDBOX/$cdir" ;; esac
SANDBOX=$(cd "$(dirname "$cdir")" && pwd)

echo "sandbox: $SANDBOX"

if [ "$RESTORE" = 1 ]; then
  echo "restoring sandbox to origin/master content"
  git -C "$SANDBOX" fetch -q origin
  git -C "$SANDBOX" restore --source=origin/master --worktree .
  git -C "$SANDBOX" status --short | head -20
  exit 0
fi

[ -n "$BRANCH" ] || { echo "!! --branch is required (or --restore)" >&2; exit 2; }
[ ${#PATHS[@]} -gt 0 ] || PATHS=(.)

# --- The guard that matters -------------------------------------------------
# The sandbox is a SHARED working tree. It regularly carries deliberate,
# uncommitted work: a diagnostic probe staged for a headed run is the normal
# case, not the exception (project#192 kept an instrumented salframeview.mm
# there for days). Overwriting that silently is how a session destroys another
# session's staged experiment, so this refuses rather than asks.
dirty=$(git -C "$SANDBOX" status --porcelain --untracked-files=no)
if [ -n "$dirty" ] && [ "$DRY" = 0 ]; then
  echo "!! the sandbox has uncommitted tracked changes:" >&2
  echo "$dirty" | sed 's/^/     /' >&2
  cat >&2 <<'MSG'
!! Refusing to overwrite them. These are someone's deliberate work until proven
!! otherwise -- a staged probe, a half-finished experiment, a bisect step.
!! Commit them to a branch, or stash them with an explicit tag, then re-run.
MSG
  exit 1
fi

git -C "$SANDBOX" rev-parse --verify --quiet "$BRANCH" >/dev/null \
  || { echo "!! no such branch: $BRANCH" >&2; exit 1; }

echo "branch:  $BRANCH"
echo "paths:   ${PATHS[*]}"
echo "--- files this would rewrite ---"
git -C "$SANDBOX" diff --name-only HEAD "$BRANCH" -- "${PATHS[@]}" | sed 's/^/    /'
n=$(git -C "$SANDBOX" diff --name-only HEAD "$BRANCH" -- "${PATHS[@]}" | wc -l | tr -d ' ')
echo "    ($n file(s))"

if [ "$DRY" = 1 ]; then
  if [ -n "$dirty" ]; then
    echo "--- NOTE: the sandbox is dirty; a real run would REFUSE ---"
    echo "$dirty" | sed 's/^/    /'
  fi
  echo "(dry run: nothing written)"
  exit 0
fi

git -C "$SANDBOX" restore --source="$BRANCH" --worktree -- "${PATHS[@]}"
echo "synced. building..."
# BUILD_CMD is a testability seam: the test suite sets it to `true` so the sync
# and guard logic can be exercised without a multi-hour LibreOffice build.
BUILD_CMD="${BUILD_CMD:-gmake}"
# shellcheck disable=SC2086
( cd "$SANDBOX" && $BUILD_CMD $MAKE_ARGS )
echo
echo "built from $BRANCH. The sandbox still carries that content --"
echo "run './build-sandbox.sh --restore' when you are done with it."
