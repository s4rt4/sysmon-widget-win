"""
clock.py — Clock panel (date + time).
"""
from datetime import datetime
from utils.ui import PanelCard, make_label


DAY_NAMES = (
    "Senin",
    "Selasa",
    "Rabu",
    "Kamis",
    "Jumat",
    "Sabtu",
    "Minggu",
)

MONTH_NAMES = (
    "Januari",
    "Februari",
    "Maret",
    "April",
    "Mei",
    "Juni",
    "Juli",
    "Agustus",
    "September",
    "Oktober",
    "November",
    "Desember",
)


class ClockPanel:
    def __init__(self, parent, config: dict):
        self.config = config
        clock_cfg   = config["clock"]
        accent      = config["accent"]

        self.widget = PanelCard(parent, config, width=config["width"])

        self.time_label = make_label(
            self.widget.inner, config,
            size=clock_cfg["time_font_size"],
            color=accent["text_main"],
            weight="bold",
            anchor="center",
        )
        self.date_label = make_label(
            self.widget.inner, config,
            size=clock_cfg["date_font_size"],
            color=accent["text_muted"],
            anchor="center",
        )
        self.time_label.pack(fill="x")
        self.date_label.pack(fill="x")

        self._tick()
        self.widget.after(50, self.widget.update_height)

    def _tick(self) -> None:
        fmt = "%H:%M:%S" if self.config["clock"]["show_seconds"] else "%H:%M"
        now = datetime.now()
        self.time_label.configure(text=now.strftime(fmt))
        day = DAY_NAMES[now.weekday()]
        month = MONTH_NAMES[now.month - 1]
        self.date_label.configure(text=f"{day} {now.day:02d} {month}")
        self.widget.after(1000, self._tick)
