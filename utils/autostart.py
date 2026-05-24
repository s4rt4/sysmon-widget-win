"""
Windows login autostart helpers.
"""
from __future__ import annotations

import os
import sys
from pathlib import Path


RUN_KEY = r"Software\Microsoft\Windows\CurrentVersion\Run"
VALUE_NAME = "SysmonWidget"
LEGACY_SCRIPT_NAME = "SysmonWidget.vbs"


def startup_command() -> str:
    if getattr(sys, "frozen", False):
        return f'"{sys.executable}"'

    python_exe = Path(sys.executable)
    pythonw = python_exe.with_name("pythonw.exe")
    launcher = pythonw if pythonw.exists() else python_exe
    main_path = Path(__file__).resolve().parents[1] / "sysmon_widget.py"
    return f'"{launcher}" "{main_path}"'


def legacy_startup_script_path() -> Path:
    appdata = os.environ.get("APPDATA")
    if appdata:
        startup_dir = (
            Path(appdata)
            / "Microsoft"
            / "Windows"
            / "Start Menu"
            / "Programs"
            / "Startup"
        )
    else:
        startup_dir = (
            Path.home()
            / "AppData"
            / "Roaming"
            / "Microsoft"
            / "Windows"
            / "Start Menu"
            / "Programs"
            / "Startup"
        )
    return startup_dir / LEGACY_SCRIPT_NAME


def is_enabled() -> bool:
    return _registry_is_enabled()


def set_enabled(enabled: bool) -> None:
    if enabled:
        _remove_legacy_startup_script()
        _registry_set_enabled(True)
        return

    _remove_legacy_startup_script()
    _registry_set_enabled(False)


def _remove_legacy_startup_script() -> None:
    try:
        legacy_startup_script_path().unlink()
    except FileNotFoundError:
        pass
    except OSError:
        pass


def _registry_is_enabled() -> bool:
    if os.name != "nt":
        return False
    try:
        import winreg

        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, RUN_KEY) as key:
            value, _ = winreg.QueryValueEx(key, VALUE_NAME)
            return value == startup_command()
    except OSError:
        return False


def _registry_set_enabled(enabled: bool) -> None:
    if os.name != "nt":
        return
    import winreg

    with winreg.CreateKeyEx(
        winreg.HKEY_CURRENT_USER,
        RUN_KEY,
        0,
        winreg.KEY_SET_VALUE,
    ) as key:
        if enabled:
            winreg.SetValueEx(key, VALUE_NAME, 0, winreg.REG_SZ, startup_command())
            return
        try:
            winreg.DeleteValue(key, VALUE_NAME)
        except FileNotFoundError:
            pass
