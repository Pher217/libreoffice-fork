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

### Stale scripts — do not use

`build_helper.sh`, `run_build.sh`, `resume_build.sh` and `restore_files.sh` are tracked but dead:
each hardcodes `/cygdrive/c/Users/philh/dev/officelabs-suite/officelabs-master/libreoffice-fork`, a
Cygwin path that no longer exists (note the obsolete `officelabs-master/` nesting). They predate the
current layout. Left in place rather than deleted here so their removal is its own reviewable
change.

## Where changes land

Branch → worktree → PR against `master`, reviewed by Pher217. Never commit to `master` directly, and
never leave a change only in the build sandbox.
