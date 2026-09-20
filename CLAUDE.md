# libreoffice-fork — Claude Instructions

The OfficeLabs LibreOffice fork: the C++ host, the CEF sidebar integration, and the `officelabs/`
module. **Stack-specific rules only** — the repo table, ports, topology, env contract, vault layout
and the shared coding rules live in `officelabs-master/CLAUDE.md` and are not repeated here.

Two things about this repo are not true of any sibling, and both have cost real work:

1. **It is on the personal `Pher217` account, not the org.** Pass `-R Pher217/libreoffice-fork` to
   every `gh` call. Its default branch is `master`, not `main`.
2. **Only one checkout can build.** See below — this is the rule that gets broken.

## Building: worktrees hold commits, the main checkout is the build sandbox

### Why a worktree cannot build

`config_host.mk` hardcodes all four path variables to one absolute location:

```
export SRCDIR=/…/libreoffice-fork
export BUILDDIR=/…/libreoffice-fork
export WORKDIR=/…/libreoffice-fork/workdir
export INSTDIR=/…/libreoffice-fork/instdir
```

gbuild derives every path from those, so a worktree has no build tree and cannot acquire one by
sharing: two worktrees pointed at one `workdir` would race and leave objects built from mixed
sources with no way to tell which source produced which object.

A worktree *can* have its own build tree — LibreOffice supports `SRCDIR != BUILDDIR` — but that
costs **~7.3 GB** (`workdir` 6.3 G + `instdir` 998 M) and a full build, which is hours. Disk is not
the constraint (6.6 TiB free here); time is. That trade is worth it for a long-lived branch and
absurd for a task worktree that lives a day.

### The convention

- **Edit and commit in a worktree.** `git worktree add .claude/worktrees/<task> -b claude/<date>-<desc> origin/master`.
  This is what the `branch-isolation-check.cjs` hook wants, and it is where your work must live.
- **The main checkout is a build sandbox.** Its branch is not meaningful; its *contents* are driven
  from whichever branch you are building. Never leave work there — work in the sandbox is one
  `restore` away from gone.
- **Drive it with `./build-sandbox.sh`, not by hand.** Hand-copying is what lost the inline-UX work
  on 2026-09-16 and what pushed a delegated worker into routing around the hook with `Bash` on
  2026-09-17 ([project#202](https://github.com/Creative-Pandas-Sarl/officelabs-project/issues/202)).

```bash
./build-sandbox.sh --branch <your-branch> --paths officelabs --dry-run   # see what it would rewrite
./build-sandbox.sh --branch <your-branch> --paths officelabs             # sync + gmake
./build-sandbox.sh --restore                                             # put the sandbox back
```

`--restore` refuses on a dirty sandbox exactly as a sync does, and restores from
**HEAD** rather than `origin/master` — restoring from origin while local master is
behind would leave the sandbox dirty against its own HEAD and get every later run
refused. `--force` overrides either refusal and prints what it is about to destroy.

Two things the guard cannot see, so the script checks them separately: an
**untracked** file at a path the target branch tracks (it is in no git object, so
overwriting it is unrecoverable), and a second concurrent run (one build tree, one
builder — `mkdir`-based lock). Note also that `git restore` is **no-overlay**: a
tracked file present at HEAD but absent on the branch is *deleted* by a sync with
`--paths .`. `--dry-run` lists everything it would rewrite.

It syncs with `git restore --source=<branch> --worktree`, which **rewrites only files whose content
differs** and leaves every identical file's mtime untouched, moving neither HEAD nor the index. That
mtime property is the whole point: gbuild then rebuilds exactly what you changed, so a one-module
edit stays a ~2 minute incremental build.

Its own tests are `./test-build-sandbox.sh` — 15 assertions against throwaway git repos, no
LibreOffice build required (`BUILD_CMD=true` stands in for `gmake`). Run them after touching the
script. They exist because the first version of this suite passed while silently exercising the
**real** checkout instead of its fixture: the script resolves its sandbox from its own location, so
targeting another tree needs `--sandbox <path>`. The first assertion now proves which tree was
resolved.

**It refuses to run against a dirty sandbox**, listing what it found. That is deliberate. The
sandbox regularly carries a deliberate uncommitted probe staged for a headed run — project#192 kept
an instrumented `salframeview.mm` there for days — and silently overwriting that destroys another
session's experiment. Commit or tag-stash it first.

### Build traps

- **`gmake vcl` compiles but does not relink.** A bare `gmake` is required to get a change into
  `instdir`. This has produced "my change isn't in the binary" more than once.
- **Verify a probe in the right artifact.** This is a `MERGELIBS=TRUE` build, so
  `instdir/…/Frameworks/libvcllo.dylib` is a **21-byte stub reading `invalid - merged lib`**.
  `vcl/osx/salframeview.mm` links into **`libvclplug_osxlo.dylib`**. Grepping the stub reports
  "probe absent" for a correctly instrumented build.
- **`SAL_INFO`/`SAL_WARN` are live in `officelabs/` but only there.** `ENABLE_SAL_LOG` is empty in
  this tree, so they compile to nothing everywhere else. `Library_officelabs.mk` adds
  `-DSAL_LOG_INFO -DSAL_LOG_WARN` for this module, and `officelabs`, `officelabs.cef` and
  `officelabs.inline` are registered in `include/sal/log-areas.dox`. Select them at runtime with
  e.g. `SAL_LOG="+INFO.officelabs.inline"`.
- **`CppunitTest_officelabs_controller` builds only with `ENABLE_CEF=TRUE`**
  (`officelabs/Module_officelabs.mk:16-19`), so "I ran the tests locally" means a CEF-enabled tree.
- **`git fetch origin`, never `git fetch --all`** — `--all` drags upstream LibreOffice and hangs.
  There is now an `upstream` remote (`git.libreoffice.org/core`), which makes `--all` worse, not
  better. Reach upstream only through `./upstream-sync.sh`; it fetches one tag at a time into
  `refs/upstream/`, deliberately outside `refs/tags/` so `git describe` and CI tag globs keep
  seeing only the fork's own history.

### Stale scripts — do not use

`build_helper.sh`, `run_build.sh`, `resume_build.sh` and `restore_files.sh` are tracked but dead:
each hardcodes `/cygdrive/c/Users/philh/dev/officelabs-suite/officelabs-master/libreoffice-fork`, a
Cygwin path that no longer exists (note the obsolete `officelabs-master/` nesting). They predate the
current layout. Left in place rather than deleted here so their removal is its own reviewable
change.

## Refreshing against upstream LibreOffice

The 2026-02 refresh took weeks. Almost none of that was the merge. It was that nobody could
answer *what is ours* — this fork is a full LibreOffice tree with our commits merged into upstream
history, so every refresh started by re-deriving the patch set from scratch, and the answer was
never written down. Two files fix that:

- **`upstream-patches.list`** — every upstream file this fork modifies or deletes (95 of them),
  grouped by why, with the load-bearing ones called out. Our own `officelabs/` module, added
  files and branding binaries are deliberately excluded: none of them can conflict.
- **`upstream-sync.sh`** — measures, never changes. `check` (how far behind), `report [TAG]`
  (what would conflict), `registry` (is the list still complete).

```bash
./upstream-sync.sh check                          # behind by N commits?
./upstream-sync.sh report libreoffice-26.8.1.1    # dry-run merge, no checkout touched
./upstream-sync.sh registry                       # fails if a patch is undeclared
```

`report` uses `git merge-tree --write-tree`, which computes the merge in the object store alone:
no checkout, no index, nothing to abort. It is safe on a dirty tree and safe in CI, which is why
the monthly `.github/workflows/upstream-watch.yml` can run it unattended.

**An undeclared conflict is the finding, not the conflict count.** A conflict in a declared file
is a patch we own and can reason about. A conflict in a file nobody listed is a patch that was
never recorded — exactly the class that made the last refresh archaeology. Add it to the list
with a reason *before* resolving it.

### The procedure

Driven end to end by **`/upstream-refresh`** (`.claude/skills/upstream-refresh/SKILL.md`), which
owns the halt conditions, the two-machine handoff and the headed-verification checklist. The steps
below are what it follows.

1. `./upstream-sync.sh report <tag>` — get the conflict set, and resolve any undeclared entries
   into `upstream-patches.list` first.
2. Worktree off `master`, `git merge refs/upstream/<tag>`.
3. Resolve. Three rules, in order of how much they have cost:
   - **`sfx2/source/sidebar/*`, `*PanelFactory.cxx`, `Sidebar.xcu`, `ThemePanel.cxx` are the
     product.** Upstream refactors this area most releases. Resolving one of these by taking
     upstream removes the OfficeLabs deck while leaving a tree that builds and tests green.
     Re-apply our side by hand, every time.
   - **`external/**` — take upstream wholesale, then re-test.** Our edits there are build
     workarounds; upstream has often fixed the same thing properly. Hand-merging a stale
     workaround is how these accumulate.
   - **`dictionaries`, `helpcontent2`, `translations`, `g`, `.gitmodules` — keep ours (deleted).**
     They resurface as modify/delete on every refresh. We do not ship them.
4. `./upstream-sync.sh registry` must be clean, and the `# base:` line in the list updated to the
   merged upstream commit.
5. **Build both platforms before the PR.** A green dry-run and green unit tests prove nothing
   about the sidebar: `CppunitTest_officelabs_*` needs `ENABLE_CEF=TRUE`, and the deck itself is
   only observable in a headed run. Budget a full rebuild (hours), not an incremental one — the
   external tarballs move on a major bump.
6. The verification that actually matters is the one automation cannot do: launch it, open the
   AI deck in Writer/Calc/Impress, and exercise inline ghost text. **That last refresh shipped a
   tree where the AI window was all that survived** — the checks all passed.

### Do it more often than once a year

A refresh is roughly linear in upstream commits and superlinear in months elapsed, because the
patch set drifts out of anyone's head. Against 26.8.1.1 — 3659 upstream commits, seven months —
the dry-run finds **13 conflicting paths, all declared**, and the entire fork patch set is ~930
inserted lines. That is a day of merging plus a build, not weeks. Refresh at each upstream
`.0.x` release and it stays that way; skip two and the archaeology comes back.

## Where changes land

Branch → worktree → PR against `master`, reviewed by Pher217. Never commit to `master` directly, and
never leave a change only in the build sandbox.
