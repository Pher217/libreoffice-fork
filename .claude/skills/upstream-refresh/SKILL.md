---
name: upstream-refresh
description: "Drive a LibreOffice upstream refresh of this fork end to end — measure, merge, resolve, build on both platforms, verify headed, land. Spans two machines and several sessions; the tracking issue is the state, not the chat. Invoke for 'refresh the fork', 'bump LibreOffice', 'merge upstream 26.8'."
---

# /upstream-refresh — bump the fork to a new LibreOffice release

**This skill exists because the 2026-02 refresh shipped a tree where the AI window was all that
survived, and every automated check passed.** Its job is not speed. Its job is to make the ways
that happens impossible to reach by accident.

Three properties, in order of how much each has cost:

1. **The session may never self-certify.** A green build and green cppunit say nothing about the
   sidebar. Only a headed run does, and only a human has eyes.
2. **The work spans two machines.** macOS and Windows. Neither session sees the other's build.
3. **The tracking issue is the state.** Not `hot.md`, not this chat. A phase is closed by a comment
   carrying evidence, so the Windows session resumes from GitHub, not from a handoff prose blob.

## Before anything

Read `CLAUDE.md` → *Refreshing against upstream LibreOffice* and *Building*. Two rules from there
that this skill assumes and does not repeat: **worktrees hold commits, the main checkout is the
build sandbox** (drive it with `./build-sandbox.sh`), and **`git fetch --all` is forbidden** — reach
upstream only through `./upstream-sync.sh`.

Find or file the tracking issue on `Creative-Pandas-Sarl/officelabs-project` with `area:fork`.
**Never file it here** — this repo inherits upstream's `repo-lockdown` workflow and auto-closes its
own issues. Open refresh at time of writing:
[project#245](https://github.com/Creative-Pandas-Sarl/officelabs-project/issues/245).

## Phase 0 — measure (mac or Windows, cheap, no build)

```bash
./upstream-sync.sh check
./upstream-sync.sh report <tag>
./upstream-sync.sh registry
```

**Gate:** `report` must show **zero UNDECLARED paths** before a merge starts. An undeclared conflict
is a patch nobody recorded — the exact class that turned the last refresh into archaeology. Add it
to `upstream-patches.list` with a reason *first*, in its own commit, so the registry change is
reviewable separately from the merge.

Post the report as a comment on the tracking issue. That is Phase 0's evidence.

## Phase 1 — merge and resolve (mac)

Worktree off `master`, then `git merge refs/upstream/<tag>`. Resolve in this order:

| Class | Rule |
|---|---|
| `dictionaries`, `helpcontent2`, `translations`, `g`, `.gitmodules` | **Keep ours** — stay deleted. We do not ship them. |
| `external/**` | **Take upstream wholesale**, then re-test. Ours are build workarounds; upstream has often fixed the same thing properly. Hand-merging a stale workaround is how they accumulate. |
| `sfx2/source/sidebar/*`, `*PanelFactory.cxx`, `Sidebar.xcu`, `ThemePanel.cxx`, `backingwindow.cxx` | **Re-apply our side by hand, every time.** This set *is* the product. Taking upstream here removes the OfficeLabs deck and leaves a tree that builds and tests green. |
| `vcl/win/app/salinst.cxx` | Hand-resolve, and treat it as Windows-only risk. Our CEF message pump lives in a loop upstream reworks freely. |

Then update the `# base:` line in `upstream-patches.list` to the merged upstream commit, and
`./upstream-sync.sh registry` must be clean.

**HALT and ask** — do not resolve, do not guess:
- A conflict in a file outside `upstream-patches.list`.
- Upstream deleted or renamed a symbol our `officelabs/` module calls.
- A resolution where you cannot state, in one sentence, what our side was for.

## Phase 2 — build macOS

`./build-sandbox.sh --branch <branch> --paths .` then a bare `gmake`. Budget a **full** rebuild:
external tarballs move on a major bump, so an incremental is not the honest test.

Run all three `CppunitTest_officelabs_*` suites in an `ENABLE_CEF=TRUE` tree.

**Gate:** paste the real command output — suite count and failure count — into the tracking issue.
Not "tests pass". The verification-gate rule in `~/.claude/rules/code-quality.md` applies in full:
no status claim without fresh evidence in the same message.

## Phase 3 — build Windows

Different machine. Label the issue `machine:windows` and **stop**; a mac session cannot do this and
must not claim it did. The Windows session resumes from the issue comments.

Note [master#18](https://github.com/Creative-Pandas-Sarl/officelabs-master/issues/18): **no CI job
runs on Windows**, so `vcl/win/app/salinst.cxx` — the riskiest file in the refresh — has zero
automated coverage. Everything about it is proven by hand or not at all.

## Phase 4 — headed verification (human, both platforms)

The only phase that proves the product survived. **The session presents this checklist and waits.
It does not tick a box on the user's behalf, and it does not infer a tick from a passing test.**

- [ ] App launches, branded as OfficeLabs (not LibreOffice), correct bundle identity
- [ ] AI deck opens in **Writer**, **Calc** and **Impress**
- [ ] Deck themes correctly in light and dark
- [ ] Sidebar answers a prompt end to end
- [ ] Inline ghost text: suggests, accepts, and types through
- [ ] Start Center and window title are OfficeLabs

Any unticked box is a blocker, not a caveat. Record who verified, on which OS build, in the issue.

## Phase 5 — land

PR against `master`, `--reviewer Pher217`. Body states the upstream tag, the conflict count, which
files were hand-resolved and why, and **the platforms actually verified** — naming any that were
not. Then bump the submodule pointer in `officelabs-project`.

Write-back before finishing: `04 Lessons/gotchas.md` for anything upstream changed under us,
`01 Architecture/Repo - libreoffice-fork.md` if the patch set changed shape, `06 Decisions/` if a
resolution set a precedent. Cite the PR.

## Stop the whole run for

- Any Phase 1 HALT condition.
- A build failure whose root cause is not understood. **No blind retry, no widened assertion, no
  "take upstream and see".** Root cause first — `~/.claude/rules/code-quality.md`.
- A Phase 4 box that cannot be ticked.
- Being asked to run Phase 3 or 4 on a machine that cannot do it.

## What this skill will not do

Claim the refresh is done from a green build. Resolve a sidebar conflict in upstream's favour.
Tick a Phase 4 box. Merge the PR — that is Philippe's, always.
