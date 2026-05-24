"""
process.py - Uptime and top process panel.
"""
from datetime import datetime
import time
import tkinter as tk

try:
    import psutil
except ImportError:
    psutil = None

from utils.ui import PanelCard, format_bytes, make_label


class ProcessPanel:
    def __init__(self, parent, config: dict):
        self.config = config
        self.proc_cfg = config["process"]
        self.widget = PanelCard(parent, config, width=config["width"])

        inner = self.widget.inner
        gap = max(8, config.get("panel_gap", 10))

        self.left = tk.Frame(inner, bg=config["panel_bg"])
        self.right = tk.Frame(inner, bg=config["panel_bg"])
        self.left.pack(side="left", fill="both", expand=True)
        tk.Frame(inner, bg=config["separator_color"], width=1).pack(
            side="left", fill="y", padx=gap
        )
        self.right.pack(side="left", fill="both", expand=True)

        self.uptime_title = make_label(
            self.left, config, text="UPTIME", size=9,
            color=config["accent"]["text_muted"],
        )
        self.uptime_value = make_label(
            self.left, config, text="--", size=13, weight="bold",
            color=config["accent"]["primary"],
        )
        self.boot_label = make_label(
            self.left, config, text="Boot --", size=9,
            color=config["accent"]["text_muted"],
        )

        self.proc_title = make_label(
            self.right, config, text="TOP PROCESS", size=9,
            color=config["accent"]["text_muted"],
        )
        self.proc_name = make_label(
            self.right, config, text="--", size=11, weight="bold",
            color=config["accent"]["text_main"],
        )
        self.proc_detail = make_label(
            self.right, config, text="CPU --  RAM --", size=9,
            color=config["accent"]["text_muted"],
        )

        for label in (
            self.uptime_title,
            self.uptime_value,
            self.boot_label,
            self.proc_title,
            self.proc_name,
            self.proc_detail,
        ):
            label.pack(fill="x")

        self._prime_cpu_percent()
        self._tick()
        self.widget.after(80, self.widget.update_height)

    def _tick(self) -> None:
        self._render_uptime()
        self._render_top_process()
        self.widget.after(self.proc_cfg["refresh_ms"], self._tick)

    def _render_uptime(self) -> None:
        if psutil is None:
            self.uptime_value.configure(text="psutil missing")
            self.boot_label.configure(text="")
            return

        boot = psutil.boot_time()
        elapsed = max(0, int(time.time() - boot))
        self.uptime_value.configure(text=self._format_duration(elapsed))
        self.boot_label.configure(
            text=f"Boot {datetime.fromtimestamp(boot).strftime('%H:%M')}"
        )

    def _render_top_process(self) -> None:
        if psutil is None:
            self.proc_name.configure(text="psutil missing")
            self.proc_detail.configure(text="")
            return

        top = None
        top_score = (-1.0, -1.0)
        attrs = ["name", "pid", "cpu_percent", "memory_info"]
        cpu_count = max(1, psutil.cpu_count() or 1)

        for proc in psutil.process_iter(attrs=attrs):
            try:
                info = proc.info
                if self._is_system_idle_process(info):
                    continue
                cpu = float(info.get("cpu_percent") or 0.0)
                mem = info.get("memory_info")
                rss = float(mem.rss) if mem else 0.0
            except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.ZombieProcess):
                continue

            score = (cpu, rss)
            if score > top_score:
                top_score = score
                top = {
                    "name": info.get("name") or f"PID {info.get('pid', '--')}",
                    "cpu": min(100.0, cpu / cpu_count),
                    "rss": rss,
                }

        if top is None:
            self.proc_name.configure(text="N/A")
            self.proc_detail.configure(text="CPU --  RAM --")
            return

        self.proc_name.configure(text=self._shorten(top["name"], 18))
        self.proc_detail.configure(
            text=f"CPU {top['cpu']:.0f}%  RAM {format_bytes(top['rss'])}"
        )

    def _prime_cpu_percent(self) -> None:
        if psutil is None:
            return
        for proc in psutil.process_iter():
            try:
                proc.cpu_percent(interval=None)
            except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.ZombieProcess):
                continue

    @staticmethod
    def _format_duration(seconds: int) -> str:
        days, rem = divmod(seconds, 86400)
        hours, rem = divmod(rem, 3600)
        minutes = rem // 60
        if days:
            return f"{days}d {hours}j"
        return f"{hours}j {minutes}m"

    @staticmethod
    def _shorten(text: str, max_len: int) -> str:
        if len(text) <= max_len:
            return text
        return text[: max(1, max_len - 1)] + "."

    @staticmethod
    def _is_system_idle_process(info: dict) -> bool:
        name = (info.get("name") or "").lower()
        return info.get("pid") == 0 or name in {"system idle process", "idle"}
