#!/usr/bin/env bash
# upstream-sync.sh — keep the OfficeLabs fork answerable about upstream LibreOffice.
#
# Three questions, three subcommands. None of them build anything and none of them
# touch the working tree: this script reports, a human decides, the build verifies.
#
#   ./upstream-sync.sh check              how far behind upstream are we?
#   ./upstream-sync.sh report [TAG]       what would conflict if we merged TAG?
#   ./upstream-sync.sh registry [--write] is upstream-patches.list still complete?
#
# Exit codes:  0 clean · 1 attention needed (behind / conflicts) · 2 registry drift
#              3 usage or environment error
set -uo pipefail

UPSTREAM_URL="https://git.libreoffice.org/core"
REFNS="refs/upstream"                 # our fetched upstream tags live here, NOT in refs/tags,
                                      # so `git tag` / `git describe` keep reporting the fork's
                                      # own history and CI tag globs never match upstream.
MANIFEST="upstream-patches.list"
OURS="officelabs"                     # our module: added wholesale, never conflicts
BINARY_RE='\.(png|ico|icns|jpg|jpeg|gif|svg)$'

cd "$(dirname "${BASH_SOURCE[0]}")" || exit 3
[ -d .git ] || [ -f .git ] || { echo "not a git checkout: $PWD" >&2; exit 3; }

die() { echo "error: $*" >&2; exit 3; }
note() { printf '%s\n' "$*" >&2; }

ensure_remote() {
  git remote get-url upstream >/dev/null 2>&1 || {
    note "adding 'upstream' remote -> $UPSTREAM_URL"
    git remote add upstream "$UPSTREAM_URL" || die "could not add upstream remote"
  }
}

# Newest upstream release tag, by version sort. Network, no fetch.
latest_upstream_tag() {
  git ls-remote --tags "$UPSTREAM_URL" 2>/dev/null \
    | sed -n 's#.*refs/tags/\(libreoffice-[0-9][0-9.]*\)$#\1#p' \
    | grep -vE 'alpha|beta|rc' \
    | sort -V | tail -1
}

# Fetch one upstream tag into $REFNS/<tag>. Idempotent; the big one is the first.
fetch_tag() {
  local tag="$1" ref="$REFNS/$1"
  git rev-parse --verify --quiet "$ref" >/dev/null && { echo "$ref"; return 0; }
  ensure_remote
  note "fetching $tag (first fetch pulls months of history; this is slow, not stuck)"
  git fetch --no-tags upstream "refs/tags/$tag:$ref" --progress >&2 \
    || die "fetch of $tag failed"
  echo "$ref"
}

# The upstream commit our master last merged = merge-base with any upstream ref.
base_against() { git merge-base HEAD "$1"; }

manifest_paths() { grep -vE '^\s*(#|$)' "$MANIFEST" | sed 's/[[:space:]]*$//' | sort -u; }

# Every upstream file we modify or delete, excluding our own module and binaries.
actual_paths() {
  local base="$1"
  git diff --name-status "$base" HEAD -- . ":!$OURS" \
    | awk -F'\t' '$1=="M"||$1=="D"{print $2}' \
    | grep -vE "$BINARY_RE" | sort -u
}

cmd_check() {
  local latest base have
  latest="$(latest_upstream_tag)"
  [ -n "$latest" ] || die "could not list upstream tags (offline?)"
  echo "latest upstream release : $latest"
  # Describe our base using whatever upstream refs we already have locally.
  have="$(git rev-parse --verify --quiet "$REFNS/$latest" || true)"
  if [ -z "$have" ]; then
    echo "our base               : not fetched — run './upstream-sync.sh report $latest'"
    echo
    echo "VERDICT: unknown. Fetch $latest to measure."
    return 1
  fi
  base="$(base_against "$REFNS/$latest")"
  local behind ahead
  behind=$(git rev-list --count "$base..$REFNS/$latest")
  ahead=$(git rev-list --count "$base..HEAD")
  echo "our upstream base      : $base ($(git log -1 --format=%ad --date=short "$base"))"
  echo "upstream commits ahead : $behind"
  echo "our commits on top     : $ahead"
  echo
  [ "$behind" -eq 0 ] && { echo "VERDICT: current."; return 0; }
  echo "VERDICT: behind by $behind upstream commits. Run: ./upstream-sync.sh report $latest"
  return 1
}

cmd_report() {
  local tag="${1:-}" ref base tmp rc
  [ -n "$tag" ] || tag="$(latest_upstream_tag)"
  [ -n "$tag" ] || die "no tag given and upstream tag list unavailable"
  ref="$(fetch_tag "$tag")" || exit 3
  base="$(base_against "$ref")"

  echo "# Upstream refresh dry-run: $(git rev-parse --abbrev-ref HEAD) <- $tag"
  echo
  echo "base            : $base ($(git log -1 --format=%ad --date=short "$base"))"
  echo "upstream ahead  : $(git rev-list --count "$base..$ref") commits"
  echo "ours on top     : $(git rev-list --count "$base..HEAD") commits"
  echo

  tmp="$(mktemp)"
  # merge-tree computes the merge in the object store only: no checkout, no index,
  # nothing to clean up if it conflicts. Safe to run on a dirty tree, and in CI.
  git merge-tree --write-tree --name-only HEAD "$ref" > "$tmp" 2>&1
  rc=$?
  if [ $rc -eq 0 ]; then
    echo "RESULT: no conflicts. The merge itself is mechanical."
    echo "        The build is still unverified — see CLAUDE.md, 'Refreshing against upstream'."
    rm -f "$tmp"; return 0
  fi
  [ $rc -eq 1 ] || { echo "merge-tree failed:"; cat "$tmp"; rm -f "$tmp"; exit 3; }

  # Line 1 is the tree oid; the conflict list runs to the first blank line.
  sed -n '2,$p' "$tmp" | sed '/^$/q' | grep -v '^$' | sort -u > "$tmp.c"
  local n undeclared
  n=$(grep -c . "$tmp.c")
  undeclared=$(comm -23 "$tmp.c" <(manifest_paths))

  echo "RESULT: $n conflicting paths."
  echo
  echo "## Declared (expected — each is a patch we own)"
  comm -12 "$tmp.c" <(manifest_paths) | sed 's/^/  /'
  if [ -n "$undeclared" ]; then
    echo
    echo "## UNDECLARED — not in $MANIFEST"
    printf '%s\n' "$undeclared" | sed 's/^/  /'
    echo
    echo "  An undeclared conflict means the fork changed a file nobody recorded."
    echo "  Add it to $MANIFEST with a reason before resolving it."
  fi
  echo
  echo "Next: CLAUDE.md -> 'Refreshing against upstream LibreOffice'."
  rm -f "$tmp" "$tmp.c"
  return 1
}

cmd_registry() {
  local write=0 ref base
  [ "${1:-}" = "--write" ] && write=1
  # Prefer a live merge-base against any upstream ref we already have. Falling back
  # to the "# base:" line in the manifest is what lets this run in CI and offline
  # without the multi-GB upstream fetch that `report` needs.
  ref="$(git for-each-ref --format='%(refname)' --sort=-refname "$REFNS/" | head -1)"
  if [ -n "$ref" ]; then
    base="$(base_against "$ref")"
  else
    base="$(sed -n 's/^# base:[[:space:]]*\([0-9a-f]\{7,\}\).*/\1/p' "$MANIFEST" | head -1)"
    [ -n "$base" ] || die "no upstream ref fetched and no '# base:' in $MANIFEST"
    git rev-parse --verify --quiet "$base^{commit}" >/dev/null \
      || die "'# base: $base' from $MANIFEST is not in this clone (shallow checkout?)"
  fi

  local missing extra
  missing=$(comm -23 <(actual_paths "$base") <(manifest_paths))
  extra=$(comm -13 <(actual_paths "$base") <(manifest_paths))

  if [ -z "$missing" ] && [ -z "$extra" ]; then
    echo "base: $base"
    echo "registry clean: $(manifest_paths | wc -l | tr -d ' ') patched upstream files, all declared."
    return 0
  fi
  [ -n "$missing" ] && { echo "PATCHED BUT NOT DECLARED:"; printf '%s\n' "$missing" | sed 's/^/  + /'; }
  [ -n "$extra" ]   && { echo "DECLARED BUT NO LONGER PATCHED:"; printf '%s\n' "$extra" | sed 's/^/  - /'; }
  if [ $write -eq 1 ]; then
    # Append only. Removals are left to a human: a path that stopped being patched
    # is usually a patch that got LOST in a merge, not one that was retired.
    [ -n "$missing" ] && { printf '\n## --- added by --write, needs a reason ---\n' >> "$MANIFEST"
                           printf '%s\n' "$missing" >> "$MANIFEST"
                           echo; echo "appended $(printf '%s\n' "$missing" | wc -l | tr -d ' ') path(s) to $MANIFEST — annotate them."; }
  else
    echo
    echo "Fix with: ./upstream-sync.sh registry --write   (then write the reason for each)"
  fi
  return 2
}

case "${1:-}" in
  check)    shift; cmd_check "$@" ;;
  report)   shift; cmd_report "$@" ;;
  registry) shift; cmd_registry "$@" ;;
  *) sed -n '2,12p' "$0" | sed -e 's/^# //' -e 's/^#$//'; exit 3 ;;
esac
