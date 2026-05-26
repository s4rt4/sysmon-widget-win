"""
network.py — Network panel: download / upload speed + sparkline + totals.
Windows update: proper loopback filter for Windows interface names.
"""
import time
import tkinter as tk

try:
    import psutil
except ImportError:
    psutil = None

from utils.sparkline import Sparkline
from utils.ui import PanelCard, format_bytes, make_label


class NetworkPanel:
    def __init__(self, parent, config: dict):
        self.config     = config
        self.net_cfg    = config["network"]
        self.widget     = PanelCard(parent, config, width=config["width"])
        self.interface  = None
        self.last       = None
        self.last_time  = None
        self.total_down = 0.0
        self.total_up   = 0.0
        self._iface_checked_at = 0.0

        inner = self.widget.inner

        # Interface name label
        self.iface_label = make_label(
            inner, config, text="Network", size=9,
            color=config["accent"]["text_muted"],
        )
        self.iface_label.pack(fill="x", pady=(0, 4))

        self.down = self._make_row(inner, "↓", config["accent"]["primary"])
        self.up   = self._make_row(inner, "↑", config["accent"]["secondary"])

        self._tick()
        self.widget.after(80, self.widget.update_height)

    def _make_row(self, parent, arrow: str, color: str) -> dict:
        row = tk.Frame(parent, bg=self.config["panel_bg"])
        row.pack(fill="x", pady=2)

        icon  = make_label(row, self.config, text=arrow, size=13, color=color)
        value = make_label(row, self.config, text="0 B/s", size=10)
        spark = Sparkline(
            row, 45, 20,
            self.net_cfg["history_len"],
            color,
            self.config["panel_bg"],
        )
        total = make_label(
            row, self.config, text="0 B", size=9,
            color=self.config["accent"]["text_muted"], anchor="e",
        )

        # Pack order matters: reserve right-edge space for `total` BEFORE
        # the sparkline expands, otherwise the sparkline (expand=True) eats
        # the row and `total` ends up clipped (e.g. "26.13 MiB" → "07 MiB").
        icon.pack(side="left")
        value.pack(side="left", padx=(4, 2))
        total.pack(side="right", padx=(4, 0))
        spark.canvas.pack(side="left", fill="x", expand=True, padx=(4, 4))
        return {"value": value, "total": total, "spark": spark}

    def _tick(self) -> None:
        if psutil is None:
            self.down["value"].configure(text="psutil missing")
            self.widget.after(self.net_cfg["refresh_ms"], self._tick)
            return

        counter = self._counter()
        now     = time.monotonic()

        if counter and self.last:
            elapsed   = max(0.001, now - self.last_time)
            down_spd  = max(0, counter.bytes_recv - self.last.bytes_recv) / elapsed
            up_spd    = max(0, counter.bytes_sent - self.last.bytes_sent) / elapsed
            self.total_down += down_spd * elapsed
            self.total_up   += up_spd   * elapsed

            self.down["value"].configure(text=format_bytes(down_spd) + "/s")
            self.up["value"].configure(  text=format_bytes(up_spd)   + "/s")
            self.down["total"].configure(text=format_bytes(self.total_down))
            self.up["total"].configure(  text=format_bytes(self.total_up))
            self.down["spark"].push(down_spd)
            self.up["spark"].push(up_spd)

        self.last      = counter
        self.last_time = now
        self.widget.after(max(1500, int(self.net_cfg["refresh_ms"])), self._tick)

    def _counter(self):
        counters = psutil.net_io_counters(pernic=True)
        if self.net_cfg["interface"] == "auto":
            now = time.monotonic()
            if self.interface is None or now - self._iface_checked_at > 10:
                self.interface = self._active_interface(counters)
                self._iface_checked_at = now
            iface = self.interface
        else:
            iface = self.net_cfg["interface"]
        self.interface = iface
        if iface:
            self.iface_label.configure(text=f"  {iface}")
        if iface is None:
            return None
        return counters.get(iface)

    def _active_interface(self, counters) -> str | None:
        candidates = [
            (name, c.bytes_recv + c.bytes_sent)
            for name, c in counters.items()
            # Exclude loopback on both Windows and Linux
            if "loopback" not in name.lower() and name.lower() != "lo"
        ]
        if not candidates:
            return None
        return max(candidates, key=lambda item: item[1])[0]
