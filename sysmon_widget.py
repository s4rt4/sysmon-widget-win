"""
Entry point for the Windows native sysmon widget.
"""
from __future__ import annotations

import argparse
import ctypes
import subprocess
import sys
import tkinter as tk
from ctypes import wintypes
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

# ---------------------------------------------------------------------------
# Single-instance guard (Windows named mutex)
# ---------------------------------------------------------------------------
_SINGLE_INSTANCE_MUTEX_NAME = "SysmonWidget_SingleInstance_v1"
_SINGLE_INSTANCE_HANDLE: int | None = None
_ERROR_ALREADY_EXISTS = 183


def _acquire_single_instance() -> bool:
    """Return True if this is the only running instance.

    Creates a Windows named mutex. While any process holds an open handle to
    it, subsequent CreateMutexW calls with the same name return
    ERROR_ALREADY_EXISTS — which we use to detect a duplicate launch.

    The handle is kept in a module global so it lives for the process
    lifetime; Windows releases the mutex automatically when the process
    exits (including crashes). Use _release_single_instance() to release
    early when spawning a replacement (e.g. tray-menu Restart).
    """
    global _SINGLE_INSTANCE_HANDLE
    if sys.platform != "win32":
        return True
    try:
        kernel32 = ctypes.windll.kernel32
        kernel32.CreateMutexW.restype = wintypes.HANDLE
        kernel32.CreateMutexW.argtypes = [
            wintypes.LPVOID, wintypes.BOOL, wintypes.LPCWSTR,
        ]
        kernel32.GetLastError.restype = wintypes.DWORD
        kernel32.CloseHandle.restype = wintypes.BOOL
        kernel32.CloseHandle.argtypes = [wintypes.HANDLE]

        handle = kernel32.CreateMutexW(None, False, _SINGLE_INSTANCE_MUTEX_NAME)
        if not handle:
            # Couldn't create the mutex; rather than block startup, allow run.
            return True
        last_err = kernel32.GetLastError()
        if last_err == _ERROR_ALREADY_EXISTS:
            kernel32.CloseHandle(handle)
            return False
        _SINGLE_INSTANCE_HANDLE = handle
        return True
    except Exception:
        # Never block startup on guard errors.
        return True


def _release_single_instance() -> None:
    """Release the mutex so a replacement instance can acquire it."""
    global _SINGLE_INSTANCE_HANDLE
    if _SINGLE_INSTANCE_HANDLE is None:
        return
    try:
        ctypes.windll.kernel32.CloseHandle(_SINGLE_INSTANCE_HANDLE)
    except Exception:
        pass
    _SINGLE_INSTANCE_HANDLE = None


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

    if not _acquire_single_instance():
        # Another instance is already running. Exit quietly so accidental
        # double-launches (autostart + manual run, double-click, etc.) don't
        # result in invisible duplicate widgets at the same screen position.
        if args.managed:
            print("[sysmon-widget] Another instance is already running. Exiting.")
        sys.exit(0)

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
    # Release the single-instance mutex BEFORE spawning the replacement so
    # the new process can acquire it cleanly. (Without this the new process
    # would see ERROR_ALREADY_EXISTS and exit immediately.)
    _release_single_instance()
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
