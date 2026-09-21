## Agent efficiency and stopping rules

Work efficiently and keep the scope narrow.

* Make the smallest coherent change necessary to complete the user's request.
* Do not perform unrelated refactoring, cleanup, redesign, optimization, or bug fixes unless required.
* Do not repeatedly inspect the same files, repeat searches, or rerun commands without new evidence.
* Do not rerun a successful build or test unless relevant code has changed.
* Prefer targeted tests during implementation and run the required final verification once.
* If the same approach fails twice, reassess instead of repeating the same cycle.
* Preserve unrelated existing changes.

When the requested work is implemented and the relevant build/tests pass, the task is complete.

**STOP immediately and give the final response.**

Do not perform additional review passes, speculative improvements, cleanup, refactoring, or repeated verification after the task is complete.

If a previous session was interrupted, inspect the current repository state (`git status` and `git diff`) and continue only with work that is actually missing. Do not restart completed work from scratch.

If progress is genuinely blocked, report the blocker instead of continuing indefinitely.
