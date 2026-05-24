"""
config.py — Widget configuration and themes.
Windows version: Forest Amber theme, Windows paths, transparent-color key.
"""
import importlib.util
import os
from pathlib import Path

from app_settings import load_settings


# ---------------------------------------------------------------------------
# Transparent colour key — must match root.configure(bg=...) in sysmon_widget.py
# Avoid pure black (#000000) as it clashes with some icon shadows.
# ---------------------------------------------------------------------------
TRANSPARENT_KEY = "#010101"

# ---------------------------------------------------------------------------
# Theme definitions
# ---------------------------------------------------------------------------
THEMES = {
    # ── Custom theme built from user's wallpaper palette ──────────────────
    "forest-amber": {
        "bg_color":        "#192F2D",   # panel card fill
        "panel_bg":        "#192F2D",
        "panel_dark":      "#0B1A17",   # darker accent areas
        "separator_color": "#58757B",
        "primary":         "#D67807",   # warm amber — gauges / sparklines
        "secondary":       "#F7EBED",   # light cream — secondary accents
        "track_bg":        "#0B1A17",
        "text_main":       "#F7EBED",
        "text_muted":      "#58757B",
    },
    # ── Additional bundled themes ──────────────────────────────────────────
    "purple": {
        "bg_color":        "#2A1A3E",
        "panel_bg":        "#2A1A3E",
        "panel_dark":      "#1A0F29",
        "separator_color": "#3A2A50",
        "primary":         "#CC44FF",
        "secondary":       "#FF44AA",
        "track_bg":        "#1A0F29",
        "text_main":       "#FFFFFF",
        "text_muted":      "#A99BB8",
    },
    "graphite": {
        "bg_color":        "#202124",
        "panel_bg":        "#202124",
        "panel_dark":      "#111315",
        "separator_color": "#34363A",
        "primary":         "#4DD0E1",
        "secondary":       "#80CBC4",
        "track_bg":        "#111315",
        "text_main":       "#F5F7FA",
        "text_muted":      "#A8ADB5",
    },
    "midnight": {
        "bg_color":        "#101A2E",
        "panel_bg":        "#101A2E",
        "panel_dark":      "#08101F",
        "separator_color": "#22304A",
        "primary":         "#5BA7FF",
        "secondary":       "#8B7CFF",
        "track_bg":        "#08101F",
        "text_main":       "#F3F7FF",
        "text_muted":      "#94A3B8",
    },
    "amber": {
        "bg_color":        "#241B12",
        "panel_bg":        "#241B12",
        "panel_dark":      "#140E08",
        "separator_color": "#3F3121",
        "primary":         "#FBBF24",
        "secondary":       "#F97316",
        "track_bg":        "#140E08",
        "text_main":       "#FFF8EA",
        "text_muted":      "#C7B99E",
    },
}

# ---------------------------------------------------------------------------
# Main CONFIG
# ---------------------------------------------------------------------------
CONFIG = {
    # ── Theme ───────────────────────────────────────────────────────────────
    "theme":            os.environ.get("SYSMON_WIDGET_THEME", "forest-amber"),

    # ── Window geometry ─────────────────────────────────────────────────────
    "position":         {"anchor": "right", "x": 16, "y": 16},
    "width":            380,

    # ── Transparency (Windows colour-key approach) ───────────────────────────
    "transparent_key":  TRANSPARENT_KEY,
    "bg_color":         "#192F2D",  # panel card fill (set by apply_theme)
    "bg_alpha":         1.0,        # unused on Windows, kept for compat

    # ── Panel visual tokens ─────────────────────────────────────────────────
    "panel_bg":         "#192F2D",
    "panel_dark":       "#0B1A17",
    "separator_color":  "#58757B",
    "corner_radius":    14,
    "panel_gap":        10,
    "panel_padding":    14,

    # ── Accent colours ──────────────────────────────────────────────────────
    "accent": {
        "primary":    "#D67807",
        "secondary":  "#F7EBED",
        "text_main":  "#F7EBED",
        "text_muted": "#58757B",
        "track_bg":   "#0B1A17",
    },

    # ── Panel on/off ────────────────────────────────────────────────────────
    "panels": {
        "clock":   True,
        "weather": True,
        "network": True,
        "sysstat": True,
        "process": True,
        "storage": True,
        "music":   True,
    },

    # ── Clock ────────────────────────────────────────────────────────────────
    "clock": {
        "time_font_size": 52,
        "date_font_size": 14,
        "font":           "Segoe UI Variable",
        "show_seconds":   False,
    },

    # ── Weather ─────────────────────────────────────────────────────────────
    "weather": {
        "api_key":     os.environ.get("OPENWEATHERMAP_API_KEY")
                       or os.environ.get("SYSMON_WIDGET_WEATHER_API_KEY", ""),
        "city_id":     0,                # 0 = use city,country query
        "city":        "Cileungsi",
        "country_code":"ID",
        "units":       "metric",
        "refresh_sec": 600,
        "show_humidity": True,
        "show_wind":     True,
    },

    # ── Network ─────────────────────────────────────────────────────────────
    "network": {
        "interface":   "auto",
        "history_len": 40,
        "refresh_ms":  1000,
        "unit":        "KiB",
    },

    # ── System stats ────────────────────────────────────────────────────────
    "sysstat": {
        "refresh_ms":     1500,
        "show_battery":   True,
        "show_temp":      True,
        "ring_size":      58,
        "ring_width":     6,
        "cpu_color":      None,          # None → use accent.primary
        "ram_color":      None,
        "battery_color":  None,
        "temp_color":     "#F7EBED",     # cream for temp gauge
    },

    # Process / uptime
    "process": {
        "refresh_ms":     3000,
    },

    # ── Storage ─────────────────────────────────────────────────────────────
    "storage": {
        "paths":      "auto",            # "auto" = detect all drives
        "bar_height": 5,
        "refresh_sec": 30,
    },

    # ── Music ────────────────────────────────────────────────────────────────
    "music": {
        "refresh_ms":      2000,
        "marquee_speed":   30,
        "show_visualizer": True,
        "vis_bars":        20,
        "vis_height":      18,
    },
}

# ---------------------------------------------------------------------------
# Theme application
# ---------------------------------------------------------------------------

def _merge_dict(base: dict, override: dict) -> None:
    for key, value in override.items():
        if isinstance(value, dict) and isinstance(base.get(key), dict):
            _merge_dict(base[key], value)
        else:
            base[key] = value


def _load_config_file(path: Path, module_name: str) -> None:
    if not path.exists():
        return
    spec = importlib.util.spec_from_file_location(module_name, path)
    if spec is None or spec.loader is None:
        return
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    local_cfg = getattr(module, "LOCAL_CONFIG", None)
    if isinstance(local_cfg, dict):
        _merge_dict(CONFIG, local_cfg)


def _load_local_config() -> None:
    # Project-local config is gitignored and handy during development.
    _load_config_file(Path(__file__).with_name("local_config.py"),
                      "sysmon_widget_project_config")
    # User-local config can override project-local settings.
    _load_config_file(Path.home() / ".config" / "sysmon-widget" / "config.py",
                      "sysmon_widget_user_config")
    # App settings are written by the Windows settings dialog.
    local_settings = load_settings()
    if isinstance(local_settings, dict):
        _merge_dict(CONFIG, local_settings)


_load_local_config()


def apply_theme(name: str | None = None) -> None:
    theme_name = name or CONFIG.get("theme", "forest-amber")
    theme = THEMES.get(theme_name, THEMES["forest-amber"])
    CONFIG["theme"]           = theme_name if theme_name in THEMES else "forest-amber"
    CONFIG["bg_color"]        = theme["bg_color"]
    CONFIG["panel_bg"]        = theme["panel_bg"]
    CONFIG["panel_dark"]      = theme.get("panel_dark", theme["bg_color"])
    CONFIG["separator_color"] = theme["separator_color"]
    CONFIG["accent"]["primary"]    = theme["primary"]
    CONFIG["accent"]["secondary"]  = theme["secondary"]
    CONFIG["accent"]["track_bg"]   = theme["track_bg"]
    CONFIG["accent"]["text_main"]  = theme["text_main"]
    CONFIG["accent"]["text_muted"] = theme["text_muted"]


apply_theme()
