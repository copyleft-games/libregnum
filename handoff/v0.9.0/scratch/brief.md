# Shared brief: libregnum 0.9.0 (close the gap analysis)

Read first: the approved plan `/var/home/zach/.claude/plans/take-a-look-at-hidden-iverson.md` (context,
decisions, your milestone, and the "Documentation and skill" section — docs and the skill are a gate),
and `docs/editor/gap-analysis.org` rows for your area. For UI work also read
`/tmp/claude-1000/-var-home-zach-source-projects-libregnum/c474e1e8-b6e5-4b4b-bd42-fe50c0433c3f/scratchpad/v081/brief-w4.md` (how editor panels/tools register actions/IPC
from their own files; visual pass under isolated Xvfb).

Worktree: `/var/home/zach/source/projects/libregnum/trees/v0.9.0` (branch feat/v0.9.0). Several agents
work IN PARALLEL here. Work only on the files your task names.

## Hard rules
- No state-changing git (no commit/add/stash/checkout/reset). The lead commits.
- Do not edit `Makefile`, `src/libregnum.h`, `tests/Makefile`, `examples/Makefile`, `src/lrg-enums.{h,c}`,
  `src/lrg-types.h`, or files outside your list, unless your task explicitly grants it. Stub files for
  your new files exist and are registered; replace them wholesale (change parent type, add types).
  Need another new file or a change to a file you don't own? STOP and report it (exact patch).
- Enums: in YOUR header with a hand-written `*_get_type()`.
- Style: gnu89, gcc, tabs, GNU `func (args)`, `/* */` only, snake_case, g_autoptr/g_steal_pointer,
  explicit casts, zero warnings (-Werror), never silence a warning, detailed comments. Every public
  symbol: `LRG_AVAILABLE_IN_ALL` + GIR doc comment in the header (once); properties/signals documented;
  `G_PARAM_EXPLICIT_NOTIFY`. GIR scanner warnings are FATAL in `make all`.
- No `raymath.h` (C99-only); no memcpy into raylib Matrix; raylib calls rely on `-include stdbool.h`.
- Third-party code: only MIT/BSD/zlib/Apache-2.0 sources may be ported; keep their copyright notice in
  the file header and report it so the lead adds it to the third-party notices.

## Docs and skill (gate — your work is not done without them)
- Full org pages for everything you add (`:PROPERTIES: :CUSTOM_ID:` headings, no hard-wrapped prose):
  overview, concepts, format schema with an annotated example (formats), complete API reference,
  worked C examples, performance notes, pitfalls, cross-links, "using it from the editor".
- Every public symbol mentioned in docs (`make docs-check`).
- Skill: if your feature changes how someone uses libregnum or the editor, update
  `skills/skill-libregnum/` (SKILL.md and/or references/*.md) in your report's file list ONLY if your
  task grants it; otherwise put the exact skill text you want added in your report.

## Build & test (builds are shared: ALWAYS take the lock)
- `L=/tmp/claude-1000/-var-home-zach-source-projects-libregnum/c474e1e8-b6e5-4b4b-bd42-fe50c0433c3f/scratchpad/v090/build.lock`
- Library: `flock $L make -j"$(nproc)" lib`; before finishing `flock $L make -j"$(nproc)" all` (GIR).
- A test: `cd tests && flock $L make build/release/tests/<name>`, run from the worktree root with `G_TEST_SRCDIR=$PWD/tests` (golden/data files resolve through it; without it e.g. test-replay golden fails as NOT_FOUND) and
  `LD_LIBRARY_PATH=$PWD/build/release/lib:$PWD/deps/graylib/build/lib:$PWD/deps/yaml-glib/build tests/build/release/tests/<name>`
- GPU/display: `/tmp/claude-1000/-var-home-zach-source-projects-libregnum/c474e1e8-b6e5-4b4b-bd42-fe50c0433c3f/scratchpad/xrun.sh env LD_LIBRARY_PATH=... <cmd>` (isolated Xvfb only).
- Examples link the STATIC lib: `flock $L make -j"$(nproc)" examples` after lib changes.
- Headless text measures 0.55*size per glyph: take positions from widget rects in UI tests.
- If a build error is in a file you don't own, wait and retry. Stale .o after struct growth: delete it.
- Before finishing: lib + all (GIR) warning-free for your files; your suites pass; existing suites you
  could affect still pass (name them); `make docs-check` 0 undocumented for your headers.

## Report
Files changed; public API (one line each); formats (schema summary); tests + results; docs pages;
skill text; anything you could not do; patches needed in files you don't own.

## Screenshots (Zach, 2026-11-07)
xrun.sh now runs a 1920x1200 Xvfb screen. Take every editor/example screenshot at **1920x1080** (editor: `lrgeditor --width 1920 --height 1080 ...`; examples: open the window at 1920x1080). No more 1280x800 / 1600x1000 shots: they look squished.
