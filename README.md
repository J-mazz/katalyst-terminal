# Katalyst Terminal

A KDE Plasma terminal emulator with its own PTY, and scrollback buffer.

## Build

Dependencies (package names vary by distro):
- Qt 6 Widgets, Gui, DBus
- KDE Frameworks 6: KConfig
- glslangValidator (for Vulkan shader compilation)
- CMake 3.24+
- A C++23 compiler

Build steps:

```bash
cmake -S . -B build
cmake --build build
```

Run:

```bash
./build/katalyst-terminal
```

Notes:
- The desktop entry uses the system "utilities-terminal" icon as a placeholder.
- Settings are read from `katalyst-terminalrc` (KConfig).
- Default profile lives under `[Profile Default]`.
- Renderer can be set in `[General]` as `Renderer=Vulkan` or `Renderer=Raster`.
- Tabs: Ctrl+Shift+T, Close Tab: Ctrl+Shift+W.
- Split: Ctrl+Shift+H / Ctrl+Shift+V, Close Split: Ctrl+Shift+Q.
- Find: Ctrl+Shift+F, Next: F3, Previous: Shift+F3.
- D-Bus service name: `org.katalyst.Terminal`.
