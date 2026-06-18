<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

<!-- PR title MUST be exactly: NNN: <task title>  (so it threads to the squash subject) -->

## What & why
<the problem, the root cause if it's a fix, and the approach — in prose>

## TDD
<the exact test(s) added, by name; failed before, clean after. Or: test added alongside.>

## Verification (all green)
```
<real commands + real pass counts — paste actual output, do not assert>
cmake --preset default && cmake --build --preset default
ctest --preset default -R <tag> --no-tests=error    # e.g. 512/512 passed
```

## Notes
<pre-existing / orthogonal issues explicitly disclaimed, so the reviewer doesn't re-flag them>

🤖 Generated with [Claude Code](https://claude.com/claude-code)
