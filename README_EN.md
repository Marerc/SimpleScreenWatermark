# SimpleScreenMark

A lightweight Windows screen watermark tool. Displays semi-transparent watermark text (hostname, IP address, date/time, etc.) on screen, commonly used for screen recording, presentations, or security auditing.

[中文](README.md)

## Features

- Customizable semi-transparent watermark overlay
- Template variables: `{hostname}`, `{ip}`, `{time}`
- Multi-monitor support with independent overlays per display
- System tray icon with right-click context menu
- Global hotkey to toggle visibility (default: `Ctrl+W`)
- INI config file with automatic hot-reload on save
- Optional auto-start on Windows login
- Single-instance enforcement, no external dependencies
- Single portable exe, runs out of the box

## Download

Download the latest `SimpleScreenMark.exe` from the [GitHub Releases](../../releases/latest) page and run it directly.

## Build from Source

**Requirements:** CMake 3.15+, Visual Studio 2019+ (MSVC)

```bash
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The output binary is at `build/Release/SimpleScreenMark.exe`.

## Configuration

On first launch, a default config file is created at `%APPDATA%\SimpleScreenMark\config.ini`. Right-click the tray icon and select "Edit Settings" to open it. Changes are applied automatically on save.

### [Watermark]

| Key | Default | Description |
|---|---|---|
| Template | `{hostname} {ip} {time}` | Watermark text template |
| FontName | `宋体` | Font name |
| FontSize | `20` | Font size |
| FontColor | `808080` | Font color (hex RGB) |
| Opacity | `50` | Opacity (0-255) |
| SpacingX | `700` | Horizontal spacing (pixels) |
| SpacingY | `400` | Vertical spacing (pixels) |
| Angle | `-42` | Rotation angle (degrees) |
| Aliased | `1` | Aliased rendering (1=on, 0=anti-aliased) |

### [Time]

| Key | Default | Description |
|---|---|---|
| Format | `%Y-%m-%d` | Time format (strftime syntax) |
| RefreshInterval | `1` | Refresh interval in seconds (0=disabled) |
| RandomRefreshRange | `0` | Random extra seconds added to interval (actual = RefreshInterval + random[0, RandomRefreshRange]) |

### [Dynamic]

| Key | Default | Description |
|---|---|---|
| RandomOffsetX | `0` | Random X-axis pixel offset range per refresh (0=disabled) |
| RandomOffsetY | `0` | Random Y-axis pixel offset range per refresh (0=disabled) |

### [Network]

| Key | Default | Description |
|---|---|---|
| NIC | `auto` | Network adapter name (`auto`=auto-detect) |

### [Hotkey]

| Key | Default | Description |
|---|---|---|
| Hotkey | `Ctrl+W` | Toggle visibility hotkey |

Supported modifiers: `Ctrl`, `Alt`, `Shift`, `Win`. Supported keys: `A-Z`, `F1-F24`, `Space`, `Tab`, `Escape`.

### [General]

| Key | Default | Description |
|---|---|---|
| AutoStart | `0` | Start on Windows login (1=on, 0=off) |

### Template Variables

| Variable | Description |
|---|---|
| `{hostname}` | Computer hostname |
| `{ip}` | Local IP address |
| `{time}` | Current time (formatted per Format setting) |

## License

[MIT License](LICENSE)
