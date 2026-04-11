# Examples

This directory holds runnable manual examples and demos. These are separate
from the automated shell-test samples under `tests/`.

## Visible `window` demos

The `window` package is only built when SDL2 is available at configure
time. If SDL2 is not installed, `./build.sh` still succeeds but the window
package and these demos are unavailable.

Build the interpreter first:

```bash
./build.sh
```

Run the simple visible-window demo:

```bash
./build/interpreter examples/window_open.mog
```

Run the interactive event demo:

```bash
./build/interpreter examples/window_events.mog
```

Run the Flappy Bird rectangle-rendered demo:

```bash
./build/interpreter examples/flappy_bird.mog
```

`window_open.mog` opens a visible window, presents a frame, and closes after a
short delay unless the user closes it first.

`window_events.mog` opens a visible window, prints observed event kinds, and
exits when the user closes the window or presses Escape.

`flappy_bird.mog` uses the `clearRgb`, `fillRect`, `drawText`, `delay`, and
key constant APIs to run a simple playable game with rectangle art and an
in-window score HUD.

Headless automated coverage remains in `tests/sample_mog_window.mog`, which is
designed to run under `SDL_VIDEODRIVER=dummy`. Additional render coverage lives
in `tests/sample_mog_window_render.mog`.
