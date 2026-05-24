"""
storage.py — Disk usage panel.
Windows version: auto-detects all mounted drive letters.
"""
import tkinter as tk

try:
    import psutil
except ImportError:
    psutil = None

from utils.bar_meter import BarMeter
from utils.ui import PanelCard, make_label


def _auto_detect_drives() -> list[dict]:
    """Return a list of {label, path} for every real partition on Windows."""
    if psutil is None:
        return [{"label": "C:", "path": "C:\\"}]
    drives = []
    for part in psutil.disk_partitions(all=False):
        # Skip CD-ROM / network shares that have no actual usage
        if not part.mountpoint:
            continue
        try:
            psutil.disk_usage(part.mountpoint)
        except (PermissionError, OSError):
            continue
        label = part.mountpoint.rstrip("\\").rstrip("/") or part.device
        drives.append({"label": label, "path": part.mountpoint})
    return drives or [{"label": "C:", "path": "C:\\"}]


class StoragePanel:
    def __init__(self, parent, config: dict):
        self.config      = config
        self.storage_cfg = config["storage"]
        self.widget      = PanelCard(parent, config, width=config["width"])
        self.rows: list  = []

        inner = self.widget.inner

        make_label(
            inner, config, text="Storage", size=12, weight="bold",
            color=config["accent"]["primary"],
        ).pack(fill="x", pady=(0, 6))

        # Resolve paths
        paths_cfg = self.storage_cfg.get("paths", "auto")
        if paths_cfg == "auto" or not isinstance(paths_cfg, list):
            paths = _auto_detect_drives()
        else:
            paths = paths_cfg

        self._build_rows(inner, paths)
        self._tick()
        self.widget.after(80, self.widget.update_height)

    def _build_rows(self, parent, paths: list[dict]) -> None:
        bar_w = self.config["width"] - self.config["panel_padding"] * 2

        for item in paths:
            row = tk.Frame(parent, bg=self.config["panel_bg"])
            row.pack(fill="x", pady=(4, 0))

            header = tk.Frame(row, bg=self.config["panel_bg"])
            header.pack(fill="x")

            lbl = make_label(header, self.config, text=item["label"], size=10)
            val = make_label(
                header, self.config, text="--",
                size=10, color=self.config["accent"]["text_muted"], anchor="e",
            )
            lbl.pack(side="left")
            val.pack(side="right")

            meter = BarMeter(
                row, bar_w,
                self.storage_cfg.get("bar_height", 5),
                self.config["accent"]["primary"],
                self.config["accent"]["track_bg"],
                self.config["panel_bg"],
            )
            meter.canvas.pack(fill="x", pady=(4, 0))

            self.rows.append({"path": item["path"], "value": val, "meter": meter})

    def _tick(self) -> None:
        for row in self.rows:
            if psutil is None:
                row["value"].configure(text="psutil missing")
                continue
            try:
                usage = psutil.disk_usage(row["path"])
            except OSError:
                row["value"].configure(text="unavailable")
                row["meter"].animate_to(0)
                continue
            total_gib = usage.total / (1024 ** 3)
            row["value"].configure(
                text=f"{usage.percent:.0f}% ({total_gib:.0f} GiB)"
            )
            row["meter"].animate_to(usage.percent)

        self.widget.after(self.storage_cfg.get("refresh_sec", 30) * 1000, self._tick)
