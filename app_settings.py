"""
Persistent settings for the Windows widget.
"""
from __future__ import annotations

import json
import os
from copy import deepcopy
from pathlib import Path
from typing import Any


APP_NAME = "SysmonWidget"
CONFIG_FILE_NAME = "config.json"

SAVED_KEYS = (
    "theme",
    "position",
    "width",
    "panels",
    "weather",
    "network",
    "sysstat",
    "storage",
    "music",
    "process",
)


def config_dir() -> Path:
    override = os.environ.get("SYSMON_WIDGET_CONFIG_DIR")
    if override:
        return Path(override)
    appdata = os.environ.get("APPDATA")
    if appdata:
        return Path(appdata) / APP_NAME
    return Path.home() / ".config" / "sysmon-widget"


def config_path() -> Path:
    return config_dir() / CONFIG_FILE_NAME


def load_settings() -> dict[str, Any]:
    path = config_path()
    if not path.exists():
        return {}
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}
    return data if isinstance(data, dict) else {}


def save_settings(settings: dict[str, Any]) -> None:
    path = config_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(settings, indent=2, sort_keys=True),
        encoding="utf-8",
    )


def snapshot_config(config: dict[str, Any]) -> dict[str, Any]:
    return {
        key: deepcopy(config[key])
        for key in SAVED_KEYS
        if key in config
    }
