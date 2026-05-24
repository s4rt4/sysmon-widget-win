"""
Tkinter settings dialog for Sysmon Widget.
"""
from __future__ import annotations

import tkinter as tk
from tkinter import messagebox, ttk
from typing import Callable

from app_settings import config_path, save_settings, snapshot_config
from config import CONFIG, THEMES
from utils.autostart import is_enabled as autostart_is_enabled
from utils.autostart import set_enabled as set_autostart_enabled


class SettingsDialog(tk.Toplevel):
    def __init__(
        self,
        parent: tk.Tk,
        config: dict,
        on_restart: Callable[[], None],
    ):
        super().__init__(parent)
        self.config_data = config
        self.on_restart = on_restart
        self.title("Sysmon Widget Settings")
        self.resizable(False, False)
        self.transient(parent)
        self.protocol("WM_DELETE_WINDOW", self.destroy)

        self.vars: dict[str, tk.Variable] = {}
        self._build()
        self._load_values()
        self.grab_set()
        self.focus_force()

    def _build(self) -> None:
        outer = ttk.Frame(self, padding=14)
        outer.grid(row=0, column=0, sticky="nsew")

        row = 0
        row = self._section(outer, row, "Appearance")
        self.vars["theme"] = tk.StringVar()
        self._combo(outer, row, "Theme", "theme", sorted(THEMES))
        row += 1
        self.vars["width"] = tk.IntVar()
        self._spin(outer, row, "Width", "width", 260, 600)
        row += 1

        row = self._section(outer, row, "Weather")
        self.vars["api_key"] = tk.StringVar()
        self._entry(outer, row, "API key", "api_key", show="*")
        row += 1
        self.vars["city_id"] = tk.IntVar()
        self._spin(outer, row, "City ID", "city_id", 0, 9999999)
        row += 1
        self.vars["city"] = tk.StringVar()
        self._entry(outer, row, "City", "city")
        row += 1
        self.vars["country_code"] = tk.StringVar()
        self._entry(outer, row, "Country", "country_code", width=8)
        row += 1

        row = self._section(outer, row, "Position")
        self.vars["anchor"] = tk.StringVar()
        self._combo(outer, row, "Anchor", "anchor", ("right", "left"))
        row += 1
        self.vars["pos_x"] = tk.IntVar()
        self._spin(outer, row, "X offset", "pos_x", 0, 2000)
        row += 1
        self.vars["pos_y"] = tk.IntVar()
        self._spin(outer, row, "Y offset", "pos_y", 0, 2000)
        row += 1

        row = self._section(outer, row, "Panels")
        panel_frame = ttk.Frame(outer)
        panel_frame.grid(row=row, column=0, columnspan=2, sticky="w", pady=(2, 8))
        for idx, name in enumerate(self.config_data["panels"]):
            self.vars[f"panel_{name}"] = tk.BooleanVar()
            ttk.Checkbutton(
                panel_frame,
                text=name.title(),
                variable=self.vars[f"panel_{name}"],
            ).grid(row=idx // 3, column=idx % 3, sticky="w", padx=(0, 14), pady=2)
        row += 1

        self.vars["autostart"] = tk.BooleanVar()
        ttk.Checkbutton(
            outer,
            text="Start with Windows",
            variable=self.vars["autostart"],
        ).grid(row=row, column=0, columnspan=2, sticky="w", pady=(4, 12))
        row += 1

        buttons = ttk.Frame(outer)
        buttons.grid(row=row, column=0, columnspan=2, sticky="e")
        ttk.Button(buttons, text="Cancel", command=self.destroy).pack(
            side="right", padx=(8, 0)
        )
        ttk.Button(buttons, text="Save", command=self._save).pack(
            side="right", padx=(8, 0)
        )
        ttk.Button(
            buttons,
            text="Save & Restart",
            command=self._save_and_restart,
        ).pack(side="right")

    def _load_values(self) -> None:
        weather = self.config_data["weather"]
        position = self.config_data["position"]
        self.vars["theme"].set(self.config_data["theme"])
        self.vars["width"].set(self.config_data["width"])
        self.vars["api_key"].set(weather.get("api_key", ""))
        self.vars["city_id"].set(int(weather.get("city_id") or 0))
        self.vars["city"].set(weather.get("city", ""))
        self.vars["country_code"].set(weather.get("country_code", ""))
        self.vars["anchor"].set(position.get("anchor", "right"))
        self.vars["pos_x"].set(int(position.get("x", 16)))
        self.vars["pos_y"].set(int(position.get("y", 16)))
        for name, enabled in self.config_data["panels"].items():
            self.vars[f"panel_{name}"].set(bool(enabled))
        self.vars["autostart"].set(autostart_is_enabled())

    def _save(self) -> bool:
        try:
            self._apply_to_config()
            set_autostart_enabled(bool(self.vars["autostart"].get()))
            save_settings(snapshot_config(self.config_data))
        except Exception as exc:
            messagebox.showerror("Settings", str(exc), parent=self)
            return False
        messagebox.showinfo(
            "Settings",
            f"Saved to:\n{config_path()}\n\nRestart the widget to apply layout changes.",
            parent=self,
        )
        return True

    def _save_and_restart(self) -> None:
        if self._save():
            self.destroy()
            self.on_restart()

    def _apply_to_config(self) -> None:
        city_id = int(self.vars["city_id"].get())
        if city_id < 0:
            raise ValueError("City ID must be 0 or greater.")
        width = int(self.vars["width"].get())
        if width < 260:
            raise ValueError("Width is too small.")

        self.config_data["theme"] = self.vars["theme"].get()
        self.config_data["width"] = width
        self.config_data["weather"]["api_key"] = self.vars["api_key"].get().strip()
        self.config_data["weather"]["city_id"] = city_id
        self.config_data["weather"]["city"] = self.vars["city"].get().strip()
        self.config_data["weather"]["country_code"] = (
            self.vars["country_code"].get().strip().upper()
        )
        self.config_data["position"] = {
            "anchor": self.vars["anchor"].get(),
            "x": int(self.vars["pos_x"].get()),
            "y": int(self.vars["pos_y"].get()),
        }
        for name in self.config_data["panels"]:
            self.config_data["panels"][name] = bool(
                self.vars[f"panel_{name}"].get()
            )

    @staticmethod
    def _section(parent, row: int, title: str) -> int:
        ttk.Label(parent, text=title, font=("", 10, "bold")).grid(
            row=row, column=0, columnspan=2, sticky="w", pady=(10, 4)
        )
        return row + 1

    def _entry(self, parent, row: int, label: str, key: str, **kwargs) -> None:
        ttk.Label(parent, text=label).grid(row=row, column=0, sticky="w", pady=2)
        ttk.Entry(parent, textvariable=self.vars[key], width=32, **kwargs).grid(
            row=row, column=1, sticky="ew", pady=2
        )

    def _combo(self, parent, row: int, label: str, key: str, values) -> None:
        ttk.Label(parent, text=label).grid(row=row, column=0, sticky="w", pady=2)
        ttk.Combobox(
            parent,
            textvariable=self.vars[key],
            values=tuple(values),
            state="readonly",
            width=30,
        ).grid(row=row, column=1, sticky="ew", pady=2)

    def _spin(
        self,
        parent,
        row: int,
        label: str,
        key: str,
        from_: int,
        to: int,
    ) -> None:
        ttk.Label(parent, text=label).grid(row=row, column=0, sticky="w", pady=2)
        ttk.Spinbox(
            parent,
            from_=from_,
            to=to,
            textvariable=self.vars[key],
            width=10,
        ).grid(row=row, column=1, sticky="w", pady=2)


def show_settings_dialog(
    parent: tk.Tk,
    config: dict,
    on_restart: Callable[[], None],
) -> None:
    for child in parent.winfo_children():
        if isinstance(child, SettingsDialog):
            child.lift()
            child.focus_force()
            return
    SettingsDialog(parent, config, on_restart)
