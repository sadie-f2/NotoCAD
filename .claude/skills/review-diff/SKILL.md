---
name: review-diff
description: Review the working diff against NotoCAD's own standards — three parallel agents covering CLAUDE.md adherence, obvious bugs, and whether the code still matches the comments around it. Use when the user asks to review uncommitted changes, a commit, or a range, or says "review this" about work in progress. For a GitHub pull request use /review instead.
user-invocable: true
---

# /review-diff — review the working diff

A three-agent review shaped for this repository, where the contributors are Sadie
and Claude, work goes straight to main, and there are no pull request comments to
mine. The stock five-agent flow spends two of its agents on team artefacts that do
not exist here; this spends all three on what actually finds things.

## What to review

Default to **uncommitted work**: `git diff HEAD`. If that is empty, review the last
commit, `git show HEAD`.

The user may name something else — a commit, `main..HEAD`, a range, a single file.
Take them at their word. Confirm what you are reviewing in one line before
starting, so a misread scope is caught before three agents work on it.

If the diff is empty, say so and stop. Do not invent something to review.

## Before the agents

Get the diff once and note which files it touches. Read the **root `CLAUDE.md`**
yourself — it is the standard the first agent judges against, it is long, and it
is cheaper to pass its relevant parts down than to have the agent rediscover them.

## The three agents

Launch all three **in parallel**, each with the diff and the list of touched
files. Each returns a list of findings, and for each finding: the file, the line,
what is wrong, and a concrete failure case.

### Agent 1 — does this hold to the project's own decisions

Judge against `CLAUDE.md`, and against `SF_todo.md` where the change touches
something that file has already reasoned about. This is the agent that earns its
keep here, because the standards are unusual and specific:

- **The restraint rules**: single-level inheritance, no RTTI or `dynamic_cast`, no
  template metaprogramming, exceptions never as control flow in the geometry
  kernel or the AutoLISP hot paths. Geometry types stay trivially copyable.
- **A divergence from R12 must be a decision with a recorded reason**, written
  where the thing lives. An undocumented divergence is a finding even when the
  code is good.
- **A divergence must not break DXF R12 interchange.** Richer geometry in the
  database is fine; it has to degrade honestly on the way out.
- **Where R12's behaviour was a considered choice, it is kept** — escape
  preserving committed work, counterclockwise arcs, negative radius meaning the
  major arc.
- **Licence boundaries are structural**: no Qt outside `src/gui/`, no LibreDWG in
  core headers, nothing GPL in the default build.
- **The command count** in `project(VERSION)` and `tests/test_registry.cpp` must
  agree, and adding a command means raising both.

Flag it when the code contradicts a decision the project has already taken. Do
**not** flag a decision you merely disagree with — that is Sadie's call and it has
usually already been argued out in writing.

### Agent 2 — obvious bugs, from the change alone

Read only the changes. Do not go spelunking for context: this agent is
deliberately diff-local, and its value is speed and a fresh eye. Look for real
defects — wrong sign, off-by-one, an unhandled error return, a resource not
released on every path, a condition inverted. Ignore style and nitpicks. Ignore
anything you cannot state a concrete failure case for.

Given what this codebase is, weight these especially:

- Arithmetic on `std::size_t` that could wrap, and unguarded indices.
- A path that returns early leaving a partial write, a half-built entity, or an
  acquired resource behind.
- Anything that could put the screen and the file out of step — an entity whose
  `draw()` and `dxf_write()` would disagree.

### Agent 3 — do the comments still tell the truth

The distinguishing check for this repository. The reasoning here lives in the
comments, at unusual density, and they are load-bearing documentation rather than
narration. So a change that silently invalidates the comment above it is a real
defect: the next reader trusts the comment.

For every changed hunk, read the surrounding comments — including the file's
header block — and ask whether they are still accurate. Report:

- A comment that now describes behaviour the code no longer has.
- A comment that says "the reason this is not X" where the change made it X.
- A measured or verified claim ("43 entities", "agrees to four decimals",
  "no closing `?`") that the change contradicts.
- A new divergence, workaround or non-obvious choice with **no** comment
  explaining why — in this codebase that is a gap, not a matter of taste.

Do not ask for more comments on self-evident code. The bar is whether a reader
would be misled.

## Verifying before reporting

Findings from three agents will overlap and some will be wrong. Before reporting:

1. **Merge duplicates.** One finding per defect, attributed to whichever agent
   made the clearest case.
2. **Check each one against the actual file**, not against the diff. A hunk can
   look wrong and be correct in context — that is the commonest false positive.
3. **Drop anything you cannot give a concrete failure case for.** "This could be
   fragile" is not a finding. "Passing an empty selection reaches `front()` on an
   empty vector" is.
4. Rank by severity: something that loses a drawing or corrupts a DXF outranks
   anything else, and a documented-decision violation outranks a nitpick.

Then report with `ReportFindings`, most severe first, empty if nothing survives —
and do not also print the findings as prose. Finding nothing is a real outcome;
say so plainly rather than padding the list.

## Do not fix anything

Report only, unless the user asks for fixes afterwards. A review that edits while
it reviews leaves the user unable to tell which changes were theirs.
