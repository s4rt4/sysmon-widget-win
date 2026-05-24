"""
Windows login autostart helpers.
"""
from __future__ import annotations

import os
import sys
from pathlib import Path


RUN_KEY = r"Software\Microsoft\Windows\CurrentVersion\Run"
VALUE_NAME = "SysmonWidget"


def _winreg():
    import winreg

    return winreg


def startup_command() -> str:
    if getattr(sys, "frozen", False):
        return f'"{sys.executable}"'

    python_exe = Path(sys.executable)
    pythonw = python_exe.with_name("pythonw.exe")
    launcher = pythonw if pythonw.exists() else python_exe
    main_path = Path(__file__).resolve().parents[1] / "main.py"
    return f'"{launcher}" "{main_path}"'


def is_enabled() -> bool:
    if os.name != "nt":
        return False
    winreg = _winreg()
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, RUN_KEY) as key:
            value, _ = winreg.QueryValueEx(key, VALUE_NAME)
            return value == startup_command()
    except FileNotFoundError:
        return False
    except OSError:
        return False


def set_enabled(enabled: bool) -> None:
    if os.name != "nt":
        return
    winreg = _winreg()
    with winreg.OpenKey(
        winreg.HKEY_CURRENT_USER,
        RUN_KEY,
        0,
        winreg.KEY_SET_VALUE,
    ) as key:
        if enabled:
            winreg.SetValueEx(key, VALUE_NAME, 0, winreg.REG_SZ, startup_command())
        else:
            try:
                winreg.DeleteValue(key, VALUE_NAME)
            except FileNotFoundError:
                pass
