# Visible Window Manual-Test Plan

## Goal

Add a manual example path for `mog:window` that can open a real, visible OS
window on a developer machine. This should live alongside future runnable
example scripts rather than inside the automated shell-test suite.

## Status

Implemented:

- `window.show(win) -> void`
- `window.hide(win) -> void`
- `examples/window_open.expr`
- `examples/window_events.expr`
- `examples/README.md`
- main `README.md` updates that distinguish headless tests from manual demos

Verified in this environment:

- `./build.sh`
- `bash tests/test_import.sh`
- `env SDL_VIDEODRIVER=dummy ./build/interpreter examples/window_open.expr`

Still pending:

- manual desktop validation that `examples/window_open.expr` opens a visible OS
  window
- manual desktop validation that `examples/window_events.expr` exits cleanly on
  window close and Escape in a real interactive session

## Current State

- `tests/sample_mog_window.expr` exercises package loading and basic calls.
- The native package currently creates windows with `SDL_WINDOW_HIDDEN`.
- The existing SDL smoke path is correct for CI/headless testing, but it does
  not verify that a developer can actually see and interact with a real window.

## Recommended Direction

Keep automated smoke tests headless, and add a separate manual-example path for
visible windows.

Recommended API direction:

- keep `window.create(...)` usable for automated tests
- add `window.show(win) -> void`
- optionally add `window.hide(win) -> void` at the same time for symmetry

This is safer than changing `window.create(...)` to visible-by-default because
it preserves the current headless test behavior and makes the visible path
explicit.

## Example Layout

Use `examples/` for runnable scripts that are meant for developer validation and
demos.

Recommended first files:

- `examples/window_open.expr`
- `examples/window_events.expr`
- `examples/README.md`

Example responsibilities:

- `window_open.expr`: create, show, clear, present, wait briefly via
  `clock()`, close
- `window_events.expr`: create, show, poll events in a loop, print event kinds,
  exit on `"quit"` or Escape
- `examples/README.md`: explain how to run visible-window examples on a machine
  with SDL2 installed

## Implementation Steps

1. Extend `packages/mog/window/package.cpp` with `show` and `hide`
   functions using SDL window visibility APIs.
2. Add the new exported signatures to the package registration metadata.
3. Keep `window.create(...)` hidden by default so `tests/sample_mog_window.expr`
   and CI remain stable.
4. Create `examples/window_open.expr` as the first visible manual validation
   script.
5. Create `examples/window_events.expr` as the first interactive event-loop
   demo.
6. Add `examples/README.md` with run instructions:
   `./build/interpreter examples/window_open.expr`
7. Update the main `README.md` to distinguish:
   automated headless tests in `tests/`
   manual visible demos in `examples/`
8. Run the two visible examples on a developer desktop session and confirm the
   remaining acceptance criteria manually.

## Acceptance Criteria

- `./build.sh` still works with SDL2 present.
- `bash tests/test_import.sh` still passes in headless mode.
- Running `./build/interpreter examples/window_open.expr` opens a visible
  window on a developer machine.
- Running `./build/interpreter examples/window_events.expr` shows a visible
  window and exits cleanly when the user closes it.
- No visible-window requirement is added to CI or headless automated tests.

## Non-Goals

- do not replace the current headless smoke test
- do not add callback-based event listeners
- do not redesign the rest of the `mog:window` API in the same change
