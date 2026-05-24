"""
System tray integration for Sysmon Widget.
"""
from __future__ import annotations

import threading
from typing import Callable

try:
    import pystray
except ImportError:
    pystray = None

try:
    from PIL import Image, ImageDraw
except ImportError:
    Image = ImageDraw = None

from utils.autostart import is_enabled as autostart_is_enabled
from utils.autostart import set_enabled as set_autostart_enabled


class TrayApp:
    def __init__(
        self,
        root,
        show_settings: Callable[[], None],
        restart_app: Callable[[], None],
        quit_app: Callable[[], None],
    ):
        self.root = root
        self.show_settings = show_settings
        self.restart_app = restart_app
        self.quit_app = quit_app
        self.icon = None
        self.thread = None
        self.visible = True

    @property
    def available(self) -> bool:
        return pystray is not None and Image is not None and ImageDraw is not None

    def start(self) -> None:
        if not self.available:
            print("[tray] pystray/Pillow unavailable; tray icon disabled")
            return
        menu = pystray.Menu(
            pystray.MenuItem("Show Widget", self._show_widget),
            pystray.MenuItem("Hide Widget", self._hide_widget),
            pystray.MenuItem("Settings", self._open_settings, default=True),
            pystray.Menu.SEPARATOR,
            pystray.MenuItem(
                "Start with Windows",
                self._toggle_autostart,
                checked=lambda _item: autostart_is_enabled(),
            ),
            pystray.MenuItem("Restart Widget", self._restart),
            pystray.MenuItem("Exit", self._quit),
        )
        self.icon = pystray.Icon(
            "SysmonWidget",
            self._make_icon(),
            "Sysmon Widget",
            menu,
        )
        self.thread = threading.Thread(target=self.icon.run, daemon=True)
        self.thread.start()

    def stop(self) -> None:
        if self.icon is not None:
            self.icon.stop()

    def _show_widget(self, _icon=None, _item=None) -> None:
        self.root.after(0, self._show_widget_on_main)

    def _hide_widget(self, _icon=None, _item=None) -> None:
        self.root.after(0, self._hide_widget_on_main)

    def _open_settings(self, _icon=None, _item=None) -> None:
        self.root.after(0, self.show_settings)

    def _toggle_autostart(self, _icon=None, _item=None) -> None:
        set_autostart_enabled(not autostart_is_enabled())

    def _restart(self, _icon=None, _item=None) -> None:
        self.root.after(0, self.restart_app)

    def _quit(self, _icon=None, _item=None) -> None:
        self.root.after(0, self.quit_app)

    def _show_widget_on_main(self) -> None:
        self.visible = True
        self.root.deiconify()
        self.root.lift()

    def _hide_widget_on_main(self) -> None:
        self.visible = False
        self.root.withdraw()

    @staticmethod
    def _make_icon():
        img = Image.new("RGBA", (64, 64), (0, 0, 0, 0))
        draw = ImageDraw.Draw(img)
        draw.rounded_rectangle(
            (7, 7, 57, 57),
            radius=14,
            fill=(25, 47, 45, 255),
            outline=(88, 117, 123, 255),
            width=3,
        )
        draw.arc((18, 18, 46, 46), 260, 540, fill=(214, 120, 7, 255), width=6)
        draw.ellipse((27, 27, 37, 37), fill=(247, 235, 237, 255))
        return img
