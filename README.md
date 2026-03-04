# Katalyst Terminal

A KDE Plasma terminal emulator written in C++23, with its own PTY, VT/ANSI parser, scrollback buffer, tab and split-pane support, and an optional Vulkan renderer backed by a glyph atlas.

## Features

- **Own PTY** — uses `forkpty(3)` with async reads via `QSocketNotifier`
- **VT/ANSI parser** — state machine handling CSI, OSC, SGR (256-colour, true-colour), alternate screen, scroll regions, bracketed paste, and more
- **Scrollback buffer** — configurable line limit, mouse-wheel scrolling
- **Tabs** — multiple terminal tabs in a single window
- **Split panes** — horizontal and vertical splits, arbitrarily nested
- **Two renderers** — a QPainter raster renderer (default) and an optional Vulkan renderer with an instanced glyph atlas
- **Find** — incremental forward/backward search with highlight (raster renderer)
- **Configurable** — font, colours, scrollback limit, shell, environment, and keyboard shortcuts persisted via KConfig
- **D-Bus** — open a new window or tab from the command line or scripts
- **RPM packaging** — spec file and build script included

## Dependencies

Package names vary by distro:

| Dependency | Notes |
|---|---|
| Qt 6 (Widgets, Gui, DBus) | Required |
| KDE Frameworks 6 — KConfig | Required |
| Vulkan headers + loader | Required at build time; renderer is optional at runtime |
| `glslangValidator` | Required for SPIR-V shader compilation |
| CMake 3.24+ | Required |
| GCC 14+ | Required — `import std;` and C++23 named modules are used |

## Build

```bash
cmake -S . -B build
cmake --build build
```

> **Note:** The first configure run pre-compiles the C++23 `std` module from your system GCC headers into `gcm.cache/`. This is a one-time step. If you upgrade GCC, delete `gcm.cache/` and reconfigure.

## Run

```bash
./build/katalyst-terminal
```

## RPM Package (Fedora / RHEL)

```bash
./build-rpm.sh
# Output: build/rpmbuild/RPMS/
```

## Configuration

Settings are read from `~/.config/katalyst-terminalrc` (KConfig format).

### Renderer

```ini
[General]
Renderer=Raster   # or: Vulkan
```

If `Renderer=Vulkan` is set but Vulkan initialisation fails at runtime, the raster renderer is used automatically.

### Profile

```ini
[Profile Default]
Font=                        # falls back to system fixed-width font
Background=14,16,20
Foreground=220,220,220
Selection=60,120,200,120
SearchHighlight=200,160,60,160
Cursor=200,200,200
ScrollbackLines=4000
Program=                     # defaults to $SHELL, then /bin/bash
Arguments=
Env=
Term=xterm-256color
```

### Keyboard Shortcuts

Shortcuts can be changed at runtime via **Settings → Keyboard Shortcuts…** and are persisted under `[Shortcuts]` in `katalyst-terminalrc`.

Default bindings:

| Action | Default shortcut |
|---|---|
| New Tab | `Ctrl+Shift+T` |
| Close Tab | `Ctrl+Shift+W` |
| Split Horizontally | `Ctrl+Shift+H` |
| Split Vertically | `Ctrl+Alt+V` |
| Close Split | `Ctrl+Shift+Q` |
| Copy | `Ctrl+Shift+C` |
| Paste | `Ctrl+Shift+V` |
| Find | `Ctrl+Shift+F` |
| Find Next | `F3` |
| Find Previous | `Shift+F3` |

## D-Bus

Service name: `org.katalyst.Terminal`  
Object path: `/org.katalyst.Terminal`

```bash
# Open a new window
qdbus org.katalyst.Terminal /org.katalyst.Terminal NewWindow

# Open a new tab in the current window
qdbus org.katalyst.Terminal /org.katalyst.Terminal OpenTab
```

## Architecture

```
MainWindow
 └── TerminalTab              (split-pane manager — QSplitter tree)
      ├── TerminalView         (QPainter raster renderer)
      └── VulkanTerminalView   (Vulkan glyph-atlas renderer)
           └── TerminalSession
                ├── PtyProcess       (forkpty + QSocketNotifier)
                ├── TerminalBuffer   (cell grid, scrollback, alternate screen)
                └── VtParser → VtParserCore   (VT/ANSI state machine)
                              imports terminal.ansi.color  (C++23 module)
                              imports terminal.sgr.param   (C++23 module)
```

## License

[Apache License 2.0](LICENSE)
