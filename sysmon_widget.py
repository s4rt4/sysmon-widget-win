"""
Entry point for the Windows native sysmon widget.
"""
from __future__ import annotations

import argparse
import subprocess
import sys
import tkinter as tk
from pathlib import Path

try:
    import psutil
except ImportError:
    psutil = None

from config import CONFIG, THEMES, TRANSPARENT_KEY, apply_theme
from settings_dialog import show_settings_dialog
from tray_app import TrayApp
from utils.win_hints import apply_win32_hints, send_to_desktop
from widget import WidgetLayout


WINDOW_TITLE = "sysmon-widget"
_LAYOUT: WidgetLayout | None = None


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Native Windows desktop system-monitor widget."
    )
    parser.add_argument(
        "--theme",
        choices=sorted(THEMES),
        help="Colour theme to use. Overrides config.",
    )
    parser.add_argument(
        "--managed",
        action="store_true",
        help="Run as a normal managed window for debugging.",
    )
    return parser.parse_args()


def calculate_position(root: tk.Tk, width: int, height: int) -> tuple[int, int]:
    pos = CONFIG["position"]
    y = max(0, pos.get("y", 16))
    if pos.get("anchor") == "right":
        x = max(0, root.winfo_screenwidth() - width - pos.get("x", 16))
    else:
        x = max(0, pos.get("x", 16))
    return x, y


def main() -> None:
    args = parse_args()

    if args.theme:
        apply_theme(args.theme)

    if psutil is not None:
        psutil.cpu_percent(interval=None)

    root = tk.Tk()
    root.title(WINDOW_TITLE)
    root.resizable(False, False)
    root.overrideredirect(True)
    root.configure(bg=TRANSPARENT_KEY)
    root.wm_attributes("-transparentcolor", TRANSPARENT_KEY)

    rebuild_widget(root, apply_hints=not args.managed)

    if not args.managed:
        root.after(200, lambda: _apply_hints(root))

    tray = TrayApp(
        root,
        show_settings=lambda: show_settings_dialog(
            root,
            CONFIG,
            lambda: rebuild_widget(root, apply_hints=not args.managed),
            lambda: _restart_app(root, tray),
        ),
        restart_app=lambda: _restart_app(root, tray),
        quit_app=lambda: _quit_app(root, tray),
    )
    tray.start()

    root.mainloop()
    tray.stop()


def _apply_hints(root: tk.Tk) -> None:
    try:
        apply_win32_hints(root)
        send_to_desktop(root)
    except Exception as exc:
        print(f"[win_hints] {exc}")


def rebuild_widget(root: tk.Tk, apply_hints: bool = True) -> None:
    global _LAYOUT
    apply_theme(CONFIG.get("theme"))
    for child in root.winfo_children():
        child.destroy()

    _LAYOUT = WidgetLayout(root, CONFIG)
    _LAYOUT.build()
    root.update_idletasks()

    height = max(1, root.winfo_reqheight())
    x, y = calculate_position(root, CONFIG["width"], height)
    root.geometry(f"{CONFIG['width']}x{height}+{x}+{y}")

    if apply_hints:
        root.after(200, lambda: _apply_hints(root))


def _restart_app(root: tk.Tk, tray: TrayApp) -> None:
    tray.stop()
    if getattr(sys, "frozen", False):
        args = [sys.executable]
        cwd = Path(sys.executable).resolve().parent
    else:
        args = [sys.executable, str(Path(__file__).resolve())]
        cwd = Path(__file__).resolve().parent
    subprocess.Popen(args, cwd=str(cwd))
    root.destroy()


def _quit_app(root: tk.Tk, tray: TrayApp) -> None:
    tray.stop()
    root.destroy()


if __name__ == "__main__":
    main()
