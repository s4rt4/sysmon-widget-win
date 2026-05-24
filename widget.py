"""
widget.py — Layout orchestrator.
Panels are stacked vertically with transparent gap separators.
"""
import tkinter as tk

from config import TRANSPARENT_KEY
from utils.ui import make_separator
from panels.clock   import ClockPanel
from panels.music   import MusicPanel
from panels.network import NetworkPanel
from panels.storage import StoragePanel
from panels.sysstat import SysStatPanel
from panels.weather import WeatherPanel


PANEL_ORDER = ["clock", "weather", "network", "sysstat", "storage", "music"]

PANEL_CLASSES = {
    "clock":   ClockPanel,
    "weather": WeatherPanel,
    "network": NetworkPanel,
    "sysstat": SysStatPanel,
    "storage": StoragePanel,
    "music":   MusicPanel,
}


class WidgetLayout:
    def __init__(self, root: tk.Tk, config: dict):
        self.root   = root
        self.config = config
        self.panels = []

    def build(self) -> None:
        # Outer container fills the window; background = transparent key
        frame = tk.Frame(
            self.root,
            bg=self.config.get("transparent_key", TRANSPARENT_KEY),
            width=self.config["width"],
        )
        frame.pack(fill="both", expand=True)

        enabled = self.config["panels"]
        gap     = self.config.get("panel_gap", 10)
        half_w  = (self.config["width"] - gap) // 2

        # Helper to add a transparent gap
        def _add_gap():
            tk.Frame(frame, bg=self.config.get("transparent_key"), height=gap).pack(fill="x")

        # ── Row 1: Clock ──
        if enabled.get("clock"):
            p = ClockPanel(frame, self.config)
            p.widget.pack(fill="x")
            self.panels.append(p)
            _add_gap()

        # ── Row 2: Weather & Network (Side-by-side) ──
        has_weather = enabled.get("weather")
        has_network = enabled.get("network")
        if has_weather or has_network:
            row2 = tk.Frame(frame, bg=self.config.get("transparent_key"))
            row2.pack(fill="x")
            
            if has_weather:
                w_cfg = self.config.copy()
                w_cfg["width"] = half_w if has_network else self.config["width"]
                pw = WeatherPanel(row2, w_cfg)
                pw.widget.pack(side="left", fill="both", expand=True)
                self.panels.append(pw)
            
            if has_weather and has_network:
                tk.Frame(row2, bg=self.config.get("transparent_key"), width=gap).pack(side="left")
                
            if has_network:
                n_cfg = self.config.copy()
                n_cfg["width"] = half_w if has_weather else self.config["width"]
                pn = NetworkPanel(row2, n_cfg)
                pn.widget.pack(side="left", fill="both", expand=True)
                self.panels.append(pn)
                
            _add_gap()

        # ── Row 3: SysStat ──
        if enabled.get("sysstat"):
            p = SysStatPanel(frame, self.config)
            p.widget.pack(fill="x")
            self.panels.append(p)
            _add_gap()

        # ── Row 4: Storage & Music (Side-by-side) ──
        has_storage = enabled.get("storage")
        has_music   = enabled.get("music")
        if has_storage or has_music:
            row4 = tk.Frame(frame, bg=self.config.get("transparent_key"))
            row4.pack(fill="x")
            
            if has_storage:
                s_cfg = self.config.copy()
                s_cfg["width"] = half_w if has_music else self.config["width"]
                ps = StoragePanel(row4, s_cfg)
                ps.widget.pack(side="left", fill="both", expand=True)
                self.panels.append(ps)
            
            if has_storage and has_music:
                tk.Frame(row4, bg=self.config.get("transparent_key"), width=gap).pack(side="left")
                
            if has_music:
                m_cfg = self.config.copy()
                m_cfg["width"] = half_w if has_storage else self.config["width"]
                pm = MusicPanel(row4, m_cfg)
                pm.widget.pack(side="left", fill="both", expand=True)
                self.panels.append(pm)

