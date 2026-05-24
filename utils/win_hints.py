"""
win_hints.py — Native Windows window-style helpers.
Replaces xlib_hints.py from the Debian version.
"""
import ctypes
import ctypes.wintypes

user32 = ctypes.windll.user32

# Window style constants
GWL_EXSTYLE       = -20
WS_EX_TOOLWINDOW  = 0x00000080   # hide from taskbar / Alt-Tab
WS_EX_NOACTIVATE  = 0x08000000   # clicking widget won't steal focus
WS_EX_LAYERED     = 0x00080000   # required for SetLayeredWindowAttributes

HWND_BOTTOM = 1                   # Z-order: behind all normal windows
SWP_NOSIZE  = 0x0001
SWP_NOMOVE  = 0x0002
SWP_NOACTIVATE = 0x0010


def _get_real_hwnd(tk_id: int) -> int:
    """Tkinter gives us the child HWND; we need the top-level parent."""
    hwnd = user32.GetParent(tk_id)
    return hwnd if hwnd else tk_id


def apply_win32_hints(tk_root) -> None:
    """
    Apply Windows-native styles so the widget:
    - Does NOT appear in the taskbar or Alt-Tab switcher
    - Does NOT steal focus when clicked
    """
    hwnd = _get_real_hwnd(tk_root.winfo_id())
    style = user32.GetWindowLongW(hwnd, GWL_EXSTYLE)
    user32.SetWindowLongW(
        hwnd,
        GWL_EXSTYLE,
        style | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
    )


def send_to_desktop(tk_root) -> None:
    """
    Push the widget to the bottom of the Z-order so it stays
    behind all normal application windows (Conky/desktop behaviour).
    Call this after the window is visible.
    """
    hwnd = _get_real_hwnd(tk_root.winfo_id())
    user32.SetWindowPos(
        hwnd,
        HWND_BOTTOM,
        0, 0, 0, 0,
        SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE,
    )
