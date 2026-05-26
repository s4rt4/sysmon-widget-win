"""
sysstat.py — System stats panel: CPU, RAM, Battery, Temperature (ring gauges).
Windows version: temperature via WMI (PowerShell subprocess), battery via psutil.
"""
import queue
import subprocess
import sys
import threading
import tkinter as tk

try:
    import psutil
except ImportError:
    psutil = None

from utils.ring_gauge import RingGauge
from utils.ui import PanelCard, make_label
from config import TRANSPARENT_KEY


# ---------------------------------------------------------------------------
# WMI temperature helper (Windows only)
# ---------------------------------------------------------------------------

_TEMP_PS_CMD = (
    "try {"
    "  $t = (Get-WmiObject Win32_PerfFormattedData_Counters_ThermalZoneInformation "
    "    -ErrorAction Stop | Select-Object -First 1).Temperature;"
    "  if ($t -gt 0) { Write-Output ($t - 273) } else { Write-Output 'N/A' }"
    "} catch { Write-Output 'N/A' }"
)


def _wmi_temperature() -> float | None:
    """Return CPU package temperature in °C via WMI, or None if unavailable."""
    try:
        startupinfo = None
        creationflags = 0
        if sys.platform == "win32":
            startupinfo = subprocess.STARTUPINFO()
            startupinfo.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startupinfo.wShowWindow = subprocess.SW_HIDE
            creationflags = getattr(subprocess, "CREATE_NO_WINDOW", 0)

        # Triple-layered hide:
        #   1. -WindowStyle Hidden  → PowerShell's own first instruction
        #   2. STARTF_USESHOWWINDOW + SW_HIDE  → startup info passed by Win32
        #   3. CREATE_NO_WINDOW  → no console at all
        # stdin=DEVNULL prevents the child from attaching to the parent's
        # console (which is what causes the brief flash when the parent
        # itself owns a console, e.g. `python.exe --managed`).
        result = subprocess.run(
            ["powershell.exe", "-WindowStyle", "Hidden",
             "-NoLogo", "-NoProfile", "-NonInteractive",
             "-Command", _TEMP_PS_CMD],
            capture_output=True,
            text=True,
            timeout=3,
            startupinfo=startupinfo,
            creationflags=creationflags,
            stdin=subprocess.DEVNULL,
        )
        out = result.stdout.strip()
        if out and out != "N/A":
            return float(out)
    except Exception:
        pass
    return None


# ---------------------------------------------------------------------------
# SysStatPanel
# ---------------------------------------------------------------------------

class SysStatPanel:
    def __init__(self, parent, config: dict):
        self.config   = config
        self.sys_cfg  = config["sysstat"]
        self.widget   = tk.Frame(
            parent,
            bg=config.get("transparent_key", TRANSPARENT_KEY),
            width=config["width"],
        )
        self.gauges: dict[str, RingGauge] = {}
        self.cards: list[PanelCard] = []
        self._cached_temp: float | None = None
        self._last_good_temp: float | None = None
        self._last_temp_poll_ms = 0
        self._temp_queue: queue.Queue = queue.Queue()
        self._temp_in_flight = False

        self._build()
        self._tick()
        self.widget.after(80, self._update_card_heights)

    # ------------------------------------------------------------------

    def _build(self) -> None:
        font = self.config["clock"]["font"]
        gap = self.config.get("panel_gap", 10)
        card_width = max(1, (self.config["width"] - gap * 3) // 4)

        for key, label, ring_color, text_color in self._gauge_defs():
            card_cfg = self.config.copy()
            card_cfg["width"] = card_width
            card = PanelCard(self.widget, card_cfg, width=card_width)
            card.pack(side="left", fill="y")

            if len(self.cards) < 3:
                tk.Frame(
                    self.widget,
                    bg=self.config.get("transparent_key", TRANSPARENT_KEY),
                    width=gap,
                ).pack(side="left", fill="y")

            cell = tk.Frame(card.inner, bg=self.config["panel_bg"])
            cell.pack(fill="both", expand=True)

            gauge = RingGauge(
                cell,
                self.sys_cfg["ring_size"],
                self.sys_cfg["ring_width"],
                ring_color or self.config["accent"]["primary"],
                self.config["accent"]["track_bg"],
                text_color or self.config["accent"]["text_main"],
                self.config["panel_bg"],
                font=font,
                font_size=10,
            )
            gauge.canvas.pack(anchor="center")

            make_label(
                cell, self.config,
                text=label, size=9,
                color=self.config["accent"]["text_muted"],
                anchor="center",
            ).pack(fill="x")

            self.gauges[key] = gauge
            self.cards.append(card)

    def _update_card_heights(self) -> None:
        for card in self.cards:
            card.update_height()

    def _gauge_defs(self) -> list[tuple]:
        s = self.sys_cfg
        primary = self.config["accent"]["primary"]
        secondary = self.config["accent"]["secondary"]
        text_main = self.config["accent"]["text_main"]
        return [
            ("cpu",     "CPU",  s.get("cpu_color")     or primary, text_main),
            ("ram",     "RAM",  s.get("ram_color")      or secondary, primary),
            ("battery", "BAT",  s.get("battery_color")  or primary, text_main),
            ("temp",    "TEMP", s.get("temp_color")     or secondary, primary),
        ]

    # ------------------------------------------------------------------

    def _tick(self) -> None:
        self._drain_temp_queue()

        now_ms = int(self.widget.winfo_toplevel().tk.call("clock", "milliseconds"))
        temp_refresh_ms = int(self.sys_cfg.get("temp_refresh_sec", 60) * 1000)
        if (
            self.sys_cfg.get("show_temp")
            and not self._temp_in_flight
            and (
                self._cached_temp is None
                or now_ms - self._last_temp_poll_ms >= temp_refresh_ms
            )
        ):
            self._last_temp_poll_ms = now_ms
            self._temp_in_flight = True
            threading.Thread(target=self._poll_temperature, daemon=True).start()

        values = self._values()
        for key, gauge in self.gauges.items():
            pct, label = values.get(key, (0, "--"))
            gauge.animate_to(pct, label)

        self.widget.after(self.sys_cfg["refresh_ms"], self._tick)

    def _poll_temperature(self) -> None:
        self._temp_queue.put(_wmi_temperature())

    def _drain_temp_queue(self) -> None:
        try:
            while True:
                value = self._temp_queue.get_nowait()
                self._cached_temp = value
                if value is not None:
                    self._last_good_temp = value
                self._temp_in_flight = False
        except queue.Empty:
            pass

    def _values(self) -> dict[str, tuple[float, str]]:
        if psutil is None:
            return {k: (0, "--") for k in self.gauges}

        cpu = psutil.cpu_percent(interval=None)
        ram = psutil.virtual_memory().percent

        # Battery
        battery = None
        if self.sys_cfg.get("show_battery"):
            try:
                battery = psutil.sensors_battery()
            except Exception:
                battery = None

        # Temperature (cached from WMI slow poll)
        temp = self._last_good_temp if self.sys_cfg.get("show_temp") else None

        return {
            "cpu":  (cpu, f"{cpu:.0f}%"),
            "ram":  (ram, f"{ram:.0f}%"),
            "battery": (
                battery.percent if battery else 0,
                f"{battery.percent:.0f}%" if battery else "N/A",
            ),
            "temp": (
                min(100, temp) if temp is not None else 0,
                f"{temp:.0f}°" if temp is not None else "N/A",
            ),
        }
