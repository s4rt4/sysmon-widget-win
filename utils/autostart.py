"""
Windows login autostart helpers.
"""
from __future__ import annotations

import os
import sys
from pathlib import Path


SCRIPT_NAME = "SysmonWidget.vbs"


def startup_command() -> str:
    if getattr(sys, "frozen", False):
        return f'"{sys.executable}"'

    python_exe = Path(sys.executable)
    pythonw = python_exe.with_name("pythonw.exe")
    launcher = pythonw if pythonw.exists() else python_exe
    main_path = Path(__file__).resolve().parents[1] / "main.py"
    return f'"{launcher}" "{main_path}"'


def startup_dir() -> Path:
    appdata = os.environ.get("APPDATA")
    if appdata:
        return (
            Path(appdata)
            / "Microsoft"
            / "Windows"
            / "Start Menu"
            / "Programs"
            / "Startup"
        )
    return Path.home() / "AppData" / "Roaming" / "Microsoft" / "Windows" / "Start Menu" / "Programs" / "Startup"


def startup_script_path() -> Path:
    return startup_dir() / SCRIPT_NAME


def is_enabled() -> bool:
    path = startup_script_path()
    try:
        return path.exists() and path.read_text(encoding="utf-8") == _script_text()
    except OSError:
        return False


def set_enabled(enabled: bool) -> None:
    path = startup_script_path()
    if enabled:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(_script_text(), encoding="utf-8")
        return
    try:
        path.unlink()
    except FileNotFoundError:
        pass


def _script_text() -> str:
    command = startup_command().replace('"', '""')
    return (
        'Set WshShell = CreateObject("WScript.Shell")\n'
        f'WshShell.Run "{command}", 0, False\n'
    )
