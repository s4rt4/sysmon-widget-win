"""
main.py — Entry point for the Windows native sysmon widget.

Changes from Debian version:
  - Uses wm_attributes("-transparentcolor") for true per-colour-key transparency
  - Applies Win32 WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE to hide from taskbar
  - Sends widget to Z-order bottom (desktop-level) via SetWindowPos
  - Drops Xlib / splash-type / desktop-type arguments (Linux-only)
  - Position is fixed right-side; no dragging
"""
import argparse
import tkinter as tk

try:
    import psutil
except ImportError:
    psutil = None

from config import CONFIG, THEMES, TRANSPARENT_KEY, apply_theme
from utils.win_hints import apply_win32_hints, send_to_desktop
from widget import WidgetLayout


WINDOW_TITLE = "sysmon-widget"


# ---------------------------------------------------------------------------
# CLI args
# ---------------------------------------------------------------------------

def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Native Windows desktop system-monitor widget."
    )
    parser.add_argument(
        "--theme",
        choices=sorted(THEMES),
        help="Colour theme to use (overrides config).",
    )
    parser.add_argument(
        "--managed",
        action="store_true",
        help="Run as a normal managed window (debug mode — shows in taskbar).",
    )
    return parser.parse_args()


# ---------------------------------------------------------------------------
# Position helper
# ---------------------------------------------------------------------------

def calculate_position(root: tk.Tk, width: int, height: int) -> tuple[int, int]:
    pos = CONFIG["position"]
    y   = max(0, pos.get("y", 16))
    if pos.get("anchor") == "right":
        x = max(0, root.winfo_screenwidth() - width - pos.get("x", 16))
    else:
        x = max(0, pos.get("x", 16))
    return x, y


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    args = parse_args()

    if args.theme:
        apply_theme(args.theme)

    # Warm up CPU percent so first reading isn't 0 %
    if psutil is not None:
        psutil.cpu_percent(interval=None)

    # ── Create root window ─────────────────────────────────────────────────
    root = tk.Tk()
    root.title(WINDOW_TITLE)
    root.resizable(False, False)

    # Remove title bar / window decorations
    root.overrideredirect(True)

    # The transparent colour-key: every pixel of this colour becomes invisible
    root.configure(bg=TRANSPARENT_KEY)
    root.wm_attributes("-transparentcolor", TRANSPARENT_KEY)

    # ── Build layout ───────────────────────────────────────────────────────
    layout = WidgetLayout(root, CONFIG)
    layout.build()
    root.update_idletasks()

    # ── Size & position ────────────────────────────────────────────────────
    height = max(1, root.winfo_reqheight())
    x, y   = calculate_position(root, CONFIG["width"], height)
    root.geometry(f"{CONFIG['width']}x{height}+{x}+{y}")

    # ── Win32 window hints (after window is realised) ──────────────────────
    if not args.managed:
        root.after(200, lambda: _apply_hints(root))

    root.mainloop()


def _apply_hints(root: tk.Tk) -> None:
    try:
        apply_win32_hints(root)   # hide from taskbar, no focus steal
        send_to_desktop(root)     # push behind all normal windows
    except Exception as exc:
        print(f"[win_hints] {exc}")


if __name__ == "__main__":
    main()
