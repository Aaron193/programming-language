# Examples

This directory holds runnable manual examples and demos. These are separate
from the automated shell-test samples under `tests/`.

## Visible `mog:window` demos

The `mog:window` package is only built when SDL2 is available at configure
time. If SDL2 is not installed, `./build.sh` still succeeds but the window
package and these demos are unavailable.

Build the interpreter first:

```bash
./build.sh
```

Run the simple visible-window demo:

```bash
./build/interpreter examples/window_open.expr
```

Run the interactive event demo:

```bash
./build/interpreter examples/window_events.expr
```

`window_open.expr` opens a visible window, presents a frame, and closes after a
short delay unless the user closes it first.

`window_events.expr` opens a visible window, prints observed event kinds, and
exits when the user closes the window or presses Escape (`27`).

Headless automated coverage remains in `tests/sample_mog_window.expr`, which is
designed to run under `SDL_VIDEODRIVER=dummy`.
