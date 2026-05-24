# Sysmon Widget for Windows

Conky-style desktop system monitor widget built with Python and Tkinter.

The current app targets Windows. It uses Tkinter transparent-color windows,
Win32 window hints, psutil system metrics, OpenWeatherMap weather data, and
Windows SMTC media sessions for the music panel. A system tray icon provides
settings, show/hide, restart, autostart, and exit controls.

## Setup

From PowerShell:

```powershell
python -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install -r requirements.txt
python main.py --managed
```

`--managed` runs the widget as a normal debug window. To run it as a desktop
widget hidden from the taskbar with a tray icon:

```powershell
python main.py
```

## Settings

Right-click the tray icon and choose `Settings`.

Settings are saved to:

```text
%APPDATA%\SysmonWidget\config.json
```

The settings dialog can configure:

- Theme
- Weather API key
- Weather city ID, city name, and country code
- Widget width
- Widget position anchor, X offset, and Y offset
- Enabled panels
- Start with Windows

## Weather API Key

The weather panel reads the API key from environment variables or local config.
Prefer one of these instead of editing source files.

PowerShell environment variable:

```powershell
$env:OPENWEATHERMAP_API_KEY = "YOUR_OPENWEATHERMAP_API_KEY"
python main.py
```

Persistent local config:

```powershell
New-Item -ItemType Directory -Force "$HOME\.config\sysmon-widget"
@'
LOCAL_CONFIG = {
    "weather": {
        "api_key": "YOUR_OPENWEATHERMAP_API_KEY",
    },
}
'@ | Set-Content "$HOME\.config\sysmon-widget\config.py"
```

During local development you can also create `local_config.py` in the project
root. That file is gitignored. Settings saved by the tray dialog are loaded
after local config and take priority.

## Themes

Choose a theme with:

```powershell
python main.py --theme graphite
```

Available themes:

- `amber`
- `forest-amber`
- `graphite`
- `midnight`
- `purple`

You can also set the default theme from local config:

```python
LOCAL_CONFIG = {
    "theme": "midnight",
}
```

## Configuration

Edit [config.py](config.py) for defaults, or put overrides in `local_config.py`
or:

```text
%USERPROFILE%\.config\sysmon-widget\config.py
```

Useful settings include:

- `position`: widget anchor and screen offset
- `width`: widget width
- `panels`: enable or disable individual panels
- `weather`: city, city id, units, refresh interval
- `network`: interface selection and history length
- `sysstat`: gauge size, refresh interval, battery and temperature visibility
- `storage`: auto-detected drives or explicit paths
- `music`: refresh interval, marquee speed, visualizer settings

## Autostart

The tray setting `Start with Windows` writes a user-level startup script:

```text
%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup\SysmonWidget.vbs
```

It does not require administrator access.

## Build EXE

Install dependencies, then run:

```powershell
.\build-exe.ps1
```

The app is built as:

```text
dist\SysmonWidget\SysmonWidget.exe
```

The generated EXE is windowless, starts the desktop widget, and exposes controls
from the system tray.

## Notes

- Weather and temperature polling run in background threads so slow network or
  WMI calls do not freeze the Tkinter UI.
- Music uses Windows System Media Transport Controls through the `winrt`
  packages. Apps such as Spotify, browsers, VLC, and other SMTC-aware players
  can be detected.
- Legacy Debian packaging has been moved to [packaging/linux](packaging/linux).
