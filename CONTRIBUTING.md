# Contributing to Katalyst Terminal

Thank you for your interest in contributing! Katalyst Terminal is a KDE Plasma terminal emulator written in **C++23**, using Qt 6, KDE Frameworks 6 (KConfig), a custom PTY layer, a VT/ANSI parser, a software raster renderer, and an optional **Vulkan** renderer backed by a glyph atlas. Contributions of all kinds are welcome — bug fixes, new features, test coverage, and documentation improvements.

Please read this document before opening a pull request.

---

## Table of Contents

- [Code of Conduct](#code-of-conduct)
- [Getting Started](#getting-started)
  - [Dependencies](#dependencies)
  - [Building](#building)
  - [Running Tests](#running-tests)
  - [Coverage Report](#coverage-report)
- [Architecture Overview](#architecture-overview)
- [How to Contribute](#how-to-contribute)
  - [Reporting Bugs](#reporting-bugs)
  - [Suggesting Features](#suggesting-features)
  - [Submitting Pull Requests](#submitting-pull-requests)
- [Coding Standards](#coding-standards)
  - [Language and Modules](#language-and-modules)
  - [Qt Conventions](#qt-conventions)
  - [Naming](#naming)
  - [Error Handling](#error-handling)
  - [Shaders](#shaders)
- [Project Structure](#project-structure)
- [Subsystem Notes](#subsystem-notes)
  - [PTY (PtyProcess)](#pty-ptyprocess)
  - [Terminal Buffer (TerminalBuffer)](#terminal-buffer-terminalbuffer)
  - [VT/ANSI Parser (VtParserCore / VtParser)](#vtansi-parser-vtparsercore--vtparser)
  - [C++23 Modules (AnsiColorCore, SgrParamCore)](#c23-modules-ansicolorcore-sgrparamcore)
  - [Raster Renderer (TerminalView)](#raster-renderer-terminalview)
  - [Vulkan Renderer (VulkanRenderer)](#vulkan-renderer-vulkanrenderer)
  - [Configuration (TerminalConfig)](#configuration-terminalconfig)
  - [D-Bus (TerminalDBus)](#d-bus-terminaldbus)
  - [Qt Shim (QtShim.h)](#qt-shim-qtshimh)
- [Tests](#tests)
- [License](#license)

---

## Code of Conduct

Be respectful, constructive, and assume good faith. This is a small project — keep discussion focused on the code and the problem at hand.

---

## Getting Started

### Dependencies

| Dependency | Notes |
|---|---|
| Qt 6 (Widgets, Gui, DBus, Test) | Required |
| KDE Frameworks 6 — KConfig | Required |
| Vulkan headers + loader | Required (Vulkan renderer is optional at runtime, not at build time) |
| `glslangValidator` | Required for compiling GLSL shaders to SPIR-V |
| CMake 3.24+ | Required |
| GCC 13+ (C++23, `-fmodules`) | Required — `import std;` and C++23 named modules are used. Clang support is untested. |
| `gcovr` | Optional — only needed for the `coverage` CMake target |

> **Note on the std module cache:** The build system pre-compiles `bits/std.cc` from the system GCC headers into `gcm.cache/std.gcm` the first time you configure. This directory is in `.gitignore`. If you switch GCC versions, delete `gcm.cache/` and reconfigure.

### Building

```bash
cmake -S . -B build
cmake --build build
```

Run:

```bash
./build/katalyst-terminal
```

RPM packages (Fedora/RHEL):

```bash
./build-rpm.sh
```

### Running Tests

Tests use Qt Test and exercise `TerminalBuffer`, `VtParser`, `TerminalSession`, `PtyProcess`, and `TerminalConfig` directly (no GUI required):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
cd build && ctest --output-on-failure
```

Or run the binary directly for verbose output:

```bash
./build/test_terminal -v2
```

### Coverage Report

Requires `gcovr` and a `Debug` build (coverage flags are added automatically):

```bash
cmake --build build --target coverage
# Output: coverage/coverage.html
```

---

## Architecture Overview

```
main()
 └── MainWindow                  ← QMainWindow, tab bar, keyboard shortcuts, KConfig
      └── TerminalTab             ← QWidget, manages split panes (QSplitter tree)
           └── TerminalView       ← QPainter-based raster renderer  ┐ both inherit
                VulkanTerminalView ← Vulkan glyph-atlas renderer     ┘ TerminalViewCommon
                     │
                     ├── TerminalSession   ← owns PtyProcess + TerminalBuffer + VtParser
                     │        ├── PtyProcess      ← forkpty / POSIX PTY, QSocketNotifier
                     │        ├── TerminalBuffer  ← screen + scrollback, cell grid
                     │        └── VtParser        ← thin Qt wrapper around VtParserCore
                     │                 └── VtParserCore  ← state machine: Normal/Escape/CSI/OSC
                     │                        imports terminal.ansi.color (AnsiColorCore)
                     │                        imports terminal.sgr.param  (SgrParamCore)
                     └── VulkanRenderer    ← Vulkan device, swapchain, glyph atlas, instanced draw
                          VulkanTerminalWindow ← QWindow (VulkanSurface), forwards input signals
```

All class declarations live in **`src/QtShim.h`** (see [Qt Shim](#qt-shim-qtshimh)).  
C++23 named module interfaces (`export module …`) are in `src/modules/` and compiled first as the `katalyst_modules` CMake OBJECT library.

---

## How to Contribute

### Reporting Bugs

[Open an issue](https://github.com/J-mazz/katalyst-terminal/issues/new) and include:

- A clear description of the problem and steps to reproduce it
- Your OS/distro, KDE Plasma version, Qt 6 version, and GCC version
- Whether you are using `Renderer=Vulkan` or `Renderer=Raster` (check `~/.config/katalyst-terminalrc`, `[General]` group)
- Whether the bug reproduces in both renderers or only one
- Any relevant terminal output or crash backtrace

### Suggesting Features

[Open an issue](https://github.com/J-mazz/katalyst-terminal/issues/new) with:

- A description of the feature and the problem it solves
- Which subsystem it touches (PTY, buffer, parser, raster renderer, Vulkan renderer, tabs/splits, D-Bus, config, etc.)
- Any prior art from other terminal emulators that's worth referencing

### Submitting Pull Requests

1. Fork the repo and create a focused branch:
   ```bash
   git checkout -b fix/pty-sigwinch-race
   ```
2. Keep each PR to **one logical change**. Split unrelated fixes into separate PRs.
3. Make sure the project builds cleanly: no new compiler warnings.
4. Make sure `ctest` passes. If your change touches `TerminalBuffer`, `VtParserCore`, `PtyProcess`, or `TerminalSession`, add or update a test in `tests/test_terminal.cpp`.
5. If your change touches GLSL shaders, verify they compile with `glslangValidator` before committing the `.spv` output.
6. Write a clear PR description: what changed, why, and — for non-trivial changes — how you tested it.
7. Reference related issues in the description (`Closes #N`, `Fixes #N`).

---

## Coding Standards

### Language and Modules

- **C++23** throughout. Use modern idioms: `std::unique_ptr`, structured bindings, `if constexpr`, ranges, etc.
- All `.cpp` files use the pattern:
  ```cpp
  #include "QtShim.h"   // Qt forward-declares + all class definitions (PCH)
  import std;            // C++23 std module — must come after the shim
  ```
- C++23 named module interfaces (`export module …`) go in `src/modules/` only. They must be listed in the `katalyst_modules` CMake OBJECT library and compiled before any TU that imports them.
- Do **not** add `#include <…>` for standard library headers in files that `import std;` — the module covers the entire standard library.

### Qt Conventions

- Use `QStringLiteral(…)` for all string literals passed to Qt APIs.
- Use `QLatin1Char(…)` / `QLatin1String(…)` for single-character and ASCII comparisons.
- Use `Qt::QueuedConnection` explicitly when crossing thread boundaries (currently there are none, but keep it in mind if you add any).
- Signals/slots: prefer the new pointer-to-member syntax over `SIGNAL`/`SLOT` macros.
- Do not use `qDebug()` in production code paths; remove all debug output before opening a PR.

### Naming

| Thing | Convention |
|---|---|
| Classes | `PascalCase` |
| Methods / functions | `camelCase` |
| Member variables | `m_camelCase` |
| Local variables | `camelCase` |
| Constants / `constexpr` | `kPascalCase` (e.g. `kBaseColors`) or `ALL_CAPS` for C-style macros |
| Enum values | `PascalCase` inside a scoped `enum class` |
| Module names | `terminal.subsystem.component` (e.g. `terminal.ansi.color`) |

### Error Handling

- In `PtyProcess`: check return values from `forkpty`, `ioctl`, `write`, `read`; handle `EINTR` in write loops; treat `EIO` on read as child exit.
- In `VulkanRenderer`: every Vulkan API call that can fail must check its return value and call `cleanup()` / return `false` on failure. Do not panic or `abort()` — the renderer must degrade gracefully so `TerminalTab::createView()` can fall back to the raster renderer.
- Do not use exceptions. The codebase does not enable them and Qt does not use them.

### Shaders

- GLSL source lives in `shaders/`. The vertex shader is `terminal_quad.vert`, the fragment shader is `terminal_quad.frag`.
- The fragment shader samples the glyph atlas (a greyscale `R` channel texture) and blends foreground/background per-instance.
- The vertex shader uses a `PushConstants` block for the screen size; instanced per-cell data is passed via vertex attributes (`inInstancePos`, `inInstanceSize`, `inInstanceUV`, `inFg`, `inBg`).
- Compile with `glslangValidator -V <file> -o <file>.spv` and verify there are no warnings before committing.
- SPV binaries are **not** committed to the repository; they are built by CMake at build time.

---

## Project Structure

```
katalyst-terminal/
├── src/
│   ├── QtShim.h                     # All class/struct declarations + Qt includes (used as PCH)
│   ├── main.cpp                     # Entry point: QApplication, D-Bus registration, MainWindow
│   ├── MainWindow.cpp               # Tab bar, keyboard shortcuts, KConfig shortcut persistence
│   ├── dbus/
│   │   └── TerminalDBus.cpp         # D-Bus adaptor: NewWindow(), OpenTab() → signals
│   ├── modules/
│   │   ├── AnsiColorCore.cpp        # `export module terminal.ansi.color` — xterm-256 palette
│   │   └── SgrParamCore.cpp         # `export module terminal.sgr.param`  — SGR param classifier
│   ├── terminal/
│   │   ├── PtyProcess.cpp           # forkpty, QSocketNotifier, SIGWINCH via TIOCSWINSZ
│   │   ├── TerminalBuffer.cpp       # Cell grid, scrollback, alternate screen, cursor, find
│   │   ├── TerminalConfig.cpp       # KConfig reader: profiles, renderer selection, colors/font
│   │   ├── TerminalSession.cpp      # Owns PtyProcess + TerminalBuffer + VtParser; emits screenUpdated
│   │   ├── VtParser.cpp             # Qt QObject wrapper around VtParserCore
│   │   └── VtParserCore.cpp         # VT/ANSI state machine: Normal/Escape/CSI/OSC/OscEscape
│   ├── ui/
│   │   ├── TerminalViewBase         # Abstract base (in QtShim.h): setSearchTerm, findNext, copy/paste
│   │   ├── TerminalViewCommon.cpp   # Shared input (keyboard, mouse, wheel, IME), selection logic
│   │   ├── TerminalView.cpp         # QPainter raster renderer: backgrounds, text, cursor, highlights
│   │   ├── TerminalTab.cpp          # QSplitter tree management, view lifecycle, tab title
│   │   └── VulkanTerminalView.cpp   # Vulkan view: delegates input to Common, drives VulkanRenderer
│   └── vulkan/
│       ├── VulkanRenderer.cpp       # Vulkan device/swapchain/pipeline, glyph atlas, instanced draw
│       └── VulkanTerminalWindow.cpp # QWindow (VulkanSurface), forwards Qt events as signals
├── shaders/
│   ├── terminal_quad.vert           # Instanced quad vertex shader (NDC transform, UV mapping)
│   └── terminal_quad.frag           # Alpha-blend glyph atlas R channel over per-cell bg/fg
├── tests/
│   └── test_terminal.cpp            # Qt Test: buffer, parser, config, session, PTY lifecycle
├── data/
│   └── org.katalyst.Terminal.desktop
├── .github/
│   └── workflows/                   # CI
├── CMakeLists.txt
├── build-rpm.sh
├── katalyst-terminal.spec
└── LICENSE                          # Apache 2.0
```

---

## Subsystem Notes

### PTY (`PtyProcess`)

- Uses `forkpty(3)` + `QSocketNotifier` for async reads. The child calls `execvp` via the internal `execChild` helper (never returns).
- `send()` loops on `EINTR` and breaks on any other `write` error — do not assume all bytes are delivered in a single call.
- `setWindowSize()` uses `TIOCSWINSZ` via `ioctl`. It is called by `TerminalSession::resize()`, which is itself called from `TerminalView::resizeEvent()`.
- `stop()` sends `SIGHUP` to the child PID. A second call to `start()` while already running is a no-op (returns `false`).

### Terminal Buffer (`TerminalBuffer`)

- Stores cells as `QVector<Cell>` rows. The scrollback and both screens (normal + alternate) are held in a single flat `QVector` of rows, with `m_normalScreenStart`, `m_alternateScreenStart`, and `m_useAlternateScreen` tracking which region is active.
- Minimum size is 1×1 (enforced in `resize()`).
- `cellAtVisible(row, col, scrollOffset)` is the primary accessor for renderers — it maps the visible viewport (accounting for scroll offset) into the flat buffer.
- `snapshot(scrollOffset)` returns the visible rows as `QStringList` — used for selection text extraction in both renderers.
- `findNext` dispatches to `findForward` / `findBackward` based on the `forward` flag.

### VT/ANSI Parser (`VtParserCore` / `VtParser`)

- `VtParserCore` is a plain C++ struct (not a QObject) with a 6-state machine: `Normal`, `Escape`, `CharsetDesignation`, `Csi`, `Osc`, `OscEscape`.
- `CharsetDesignation` silently consumes the designator byte following `ESC (` — it does not apply character set switching.
- SGR (`ESC [ … m`) is handled via `applySgrParams`, which imports `SgrParamCore` (`classifySgrParam`) and `AnsiColorCore` (`ansiColorFromXtermIndex`).
- OSC title (`ESC ] 0;` or `2; … BEL/ST`) sets the window title via `VtParser::titleChanged`.
- `VtParser` is a thin `QObject` wrapper that owns the `VtParserCore*` and emits `titleChanged`.
- `feedVtParserCore` returns `true` if the title changed; `VtParser::feed` emits the signal accordingly.

### C++23 Modules (`AnsiColorCore`, `SgrParamCore`)

- `terminal.ansi.color` exports `AnsiRgb` and `constexpr ansiColorFromXtermIndex(int)` — a fully `constexpr` xterm-256 palette lookup (base 16 + 6×6×6 cube + 24 greys).
- `terminal.sgr.param` exports `SgrAction` and `constexpr classifySgrParam(int)` — maps SGR integer parameters to a typed enum.
- Both modules are compiled first via the `katalyst_modules` CMake OBJECT library. All TUs that `import` them must depend on `katalyst_modules`.
- Keep these modules **pure** (no Qt, no I/O, no global mutable state). They are `constexpr`-friendly by design.

### Raster Renderer (`TerminalView`)

- Inherits `TerminalViewCommon` (input handling, selection, scroll) → `TerminalViewBase` → `QWidget`.
- `paintEvent` renders in three passes per row: backgrounds (`paintRowBackgrounds`), selection highlight (`paintRowSelection`), then text glyphs + decorations (`paintRowText`).
- Search highlights (`drawSearchHighlights`) are drawn between the selection and text passes.
- `isCellVisuallyEmpty` skips `drawText` for space cells without decorations — a cheap optimization.
- Cell font is set lazily in `setCellFont` per cell (bold/italic). Consider batching by font run if you are optimizing paint performance.
- `scrollToLine` adjusts `m_scrollOffset` to center the target line in the viewport.

### Vulkan Renderer (`VulkanRenderer`)

- Initializes a `QVulkanInstance` (API 1.2), creates a `VulkanTerminalWindow` (a `QWindow` with `VulkanSurface`), and wraps it in `QWidget::createWindowContainer`.
- The glyph atlas is a device-local `VK_FORMAT_R8_UNORM` image. Glyphs are rasterized using `QPainter` into a CPU-side `QImage`, then uploaded via a staging buffer with `copyBufferToImage`.
- Rendering is fully instanced: each terminal cell becomes one `TerminalQuadInstance` (position, size, UV rect into atlas, fg/fg RGBA). The instance buffer grows on demand (`growInstanceBuffer`).
- Screen size is pushed via a `PushConstants` block (`vec2 screenSize`) in the vertex shader.
- `isInitialized()` must return `true` before `TerminalTab` uses the Vulkan view; if Vulkan setup fails at any point, `TerminalTab::createView()` deletes the `VulkanTerminalView` and falls back to `TerminalView`.
- Search and find-next are **not yet implemented** in the Vulkan renderer (`setSearchTerm` and `findNext` are stubs). Contributions welcome.

### Configuration (`TerminalConfig`)

- Reads from `katalyst-terminalrc` via `KConfig`. The `[General]` group holds `DefaultProfile` and `Renderer`. The `[Profile <name>]` group holds font, colors, scrollback, program, arguments, env, and `TERM`.
- Keyboard shortcuts are persisted separately under `[Shortcuts]` in the same file, written by `MainWindow::configureShortcuts()`.
- `TerminalConfig` is constructed once in `MainWindow::setupUi()` and passed by pointer to all child widgets.

### D-Bus (`TerminalDBus`)

- Service name: `org.katalyst.Terminal`, object path: `/org.katalyst.Terminal`.
- Exposes two slots: `NewWindow()` → emits `newWindowRequested()` and `OpenTab()` → emits `newTabRequested()`.
- Connected in `main()` to `createWindow` (lambda) and `MainWindow::openTab()` respectively.

### Qt Shim (`QtShim.h`)

- **All** class and struct declarations for the project live here. It is compiled as a precompiled header (PCH) via `target_precompile_headers`.
- Every `.cpp` file that is not a C++23 module interface starts with `#include "QtShim.h"` followed by `import std;`.
- If you add a new class, declare it here. Keep the file organized by subsystem in the same order as the directory structure.

---

## Tests

Tests live in `tests/test_terminal.cpp` and use `QTest` + `QSignalSpy`. There is no GUI dependency (`QTEST_GUILESS_MAIN`).

Current coverage includes:
- `TerminalBuffer`: resize, cursor clamping, put/newline/CR/BS/tab, scrollback, find forward/backward, attributes, clear modes, snapshot, visible cells, alternate screen
- `VtParser` / `VtParserCore`: basic feed, CSI cursor movement, SGR attributes, color params (256-color, true-color), OSC title (BEL and ST terminator), charset designation consumption, private modes (`?25`, `?47`, `?1049`), scroll region
- `TerminalConfig`: default profile accessors, renderer string, colors
- `TerminalSession`: buffer ownership, resize propagation, shell start + PTY read, OSC title via real shell subprocess
- `PtyProcess`: start, double-start guard, data received, exit signal, `setWindowSize`, `send` after exit

When adding tests:
- Use the `waitUntil` helper (already in the test file) for anything involving real PTY I/O.
- Do not use fixed `QTest::qWait` sleeps — use signal spies or predicate polling.
- Keep subprocess-based tests (those that call `startShell`) isolated so they don't leave zombie processes.

---

## License

By contributing to Katalyst Terminal, you agree that your contributions will be licensed under the [Apache License 2.0](LICENSE).
