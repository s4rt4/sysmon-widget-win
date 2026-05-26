# Sysmon Widget for Windows

Conky-style desktop system monitor widget for Windows. Two builds available:

| Build | Size | Memory | Best for |
|---|---|---|---|
| **Native C++** | 514 KB EXE / 249 KB zip | ~50 MB | daily use, autostart, no install |
| **Python**     | 4.4 MB EXE / 28 MB zip | ~150 MB | hacking / panel customisation |

Both share the same `%APPDATA%\SysmonWidget\config.json`, so you can switch
between them without re-entering settings.

## Download

Grab the latest from [GitHub Releases](https://github.com/s4rt4/sysmon-widget-win/releases):

- `SysmonWidget-native-vX.Y.Z-win-x64.zip` — single portable EXE, no installer, no runtime DLLs
- `SysmonWidget-python-vX.Y.Z-win-x64.zip` — PyInstaller bundle (folder)

Extract anywhere and run the EXE. Right-click the tray icon for Settings.

## Panels

Both builds render the same set of cards:

- **Clock** — time + Indonesian weekday + date + ticking seconds
- **Weather** — OpenWeatherMap-backed, with icon glyph, humidity, wind
- **Network** — Download / Upload throughput + persistent "Today" total
- **Sysstat** — CPU / RAM / Battery / Temperature ring gauges
- **Music** — SMTC-driven, scrolling title marquee, real-audio visualiser (WASAPI loopback)
- **Storage** — drive usage with bar
- **Top Processes** — 2 highest by CPU%, with RAM
- **Uptime / Boot / Volume / Brightness** — bottom strip card

## Quick start (Native build)

1. Download and unzip the native release somewhere persistent (e.g. `%LOCALAPPDATA%\SysmonWidget`).
2. Double-click `SysmonWidgetNative.exe` — widget appears in the top-right of the work area.
3. Right-click the tray icon → **Settings** to set the OpenWeatherMap API key, city, position anchor, and autostart.

That's it. The EXE has a single-instance guard so double-click + autostart launch can't stack duplicate widgets.

## Quick start (Python build)

From PowerShell:

```powershell
python -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install -r requirements.txt
python sysmon_widget.py --managed
```

`--managed` runs as a normal debug window. For the desktop widget mode with tray icon:

```powershell
python sysmon_widget.py
```

## Settings

Right-click the tray icon → **Settings**.

Both builds read/write `%APPDATA%\SysmonWidget\config.json`. The native build only touches the `weather`, `position`, and `network` keys — Python-specific keys (theme, panels, width) are preserved.

The settings dialog exposes:

- **Weather** — API key, City, City ID, Country
- **Position** — anchor (right/left), X offset, Y offset
- **Startup** — Start with Windows toggle (HKCU Run entry)

## Weather API Key

Get a free key from [OpenWeatherMap](https://openweathermap.org/api). Then either:

- Paste into the Settings dialog → Save
- Or set environment variable `OPENWEATHERMAP_API_KEY` before launch
- Or edit `%APPDATA%\SysmonWidget\config.json` directly

## Autostart

Settings → **Start with Windows** writes a per-user registry value:

```text
HKCU\Software\Microsoft\Windows\CurrentVersion\Run\SysmonWidget
```

No administrator access required.

## Building from source

### Native (C++)

Requires CMake 3.24+ and Visual Studio 2022 Build Tools (or full VS). From a developer prompt:

```powershell
.\build-native.ps1 -Run
```

Output: `build-native\Release\SysmonWidgetNative.exe` (~500 KB, static MSVC runtime).

If the icon design changes, regenerate `src/native/app.ico` from PowerShell:

```powershell
.\tools\make_icon.ps1
```

### Python (PyInstaller)

```powershell
python -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install -r requirements.txt
.\build-exe.ps1
```

Output: `dist\SysmonWidget\SysmonWidget.exe`. Uses `--onedir --windowed --noupx` with version-info metadata for AV-friendliness.

## Configuration (Python build)

Edit [config.py](config.py) for defaults, or override in `local_config.py` (gitignored) or `%USERPROFILE%\.config\sysmon-widget\config.py`. Useful keys:

- `position`, `width`, `panels`, `weather`, `network`, `sysstat`, `storage`, `music`

### Themes (Python only)

```powershell
python sysmon_widget.py --theme graphite
```

Available themes: `amber`, `forest-amber`, `graphite`, `midnight`, `purple`. The native build uses a single fixed palette (dark cards + per-card neon accents).

## Architecture notes

### Native build

- **Direct2D + DirectWrite** rendering on a per-pixel-alpha layered window
- **Per-Monitor DPI v2** with grayscale antialiasing (crisp at 100/125/150% scaling)
- **Background worker threads** for SMTC music, process scan, WMI temperature, WMI brightness, WinHTTP weather — UI never blocks on slow I/O
- **Smart invalidate** + adaptive audio frame rate (~1–2% CPU idle, ~0% hidden)
- **Single-instance guard** via named mutex
- **Atomic config writes** via `MoveFileExW(REPLACE_EXISTING|WRITE_THROUGH)`
- **Static MSVC CRT** — portable single EXE, no vcruntime dependency

### Python build

- Tkinter with transparent-color windows + Win32 hints
- Background threads for weather and temperature polling
- Music via the `winrt` Python projection of Windows SMTC
- Pillow for weather icons, psutil for system metrics

## Compatibility

- Windows 10 1703 or later (native build needs Per-Monitor DPI v2)
- x64 architecture
- Internet for weather, audio playback via SMTC-aware apps (Spotify, browsers, Dopamine, VLC, etc.) for the music panel
- Temperature reading depends on hardware exposing `Win32_PerfFormattedData_Counters_ThermalZoneInformation` or `MSAcpi_ThermalZoneTemperature` — works on most laptops, varies on desktops
- Brightness reading via `WmiMonitorBrightness` — laptops typically work, desktop monitors without DDC/CI show `--`

## Project layout

```
src/native/       C++ source for the native build
tools/            One-off helper scripts (e.g. make_icon.ps1)
panels/           Python panel implementations
utils/            Shared Python helpers
packaging/linux/  Legacy Debian packaging (unmaintained)
```

## Notes

- Both builds run continuously to track Network "Today" totals accurately. The native build persists baseline counters to `config.json` every 60 s + on graceful exit, so brief restarts don't reset the day's tally.
- The native EXE is unsigned. Some AV / SmartScreen heuristics flag unsigned binaries — code-signing is recommended for redistribution.
