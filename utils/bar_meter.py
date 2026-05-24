"""
bar_meter.py — Rounded animated horizontal bar.
Windows version: draws with rounded caps using polygon for a modern look.
"""
import tkinter as tk


class BarMeter:
    """Horizontal progress bar with smooth animation and rounded ends."""

    def __init__(self, parent, width: int, height: int,
                 fill_color: str, track_color: str, bg: str):
        self.width      = width
        self.height     = height
        self.fill_color  = fill_color
        self.track_color = track_color
        self.value      = 0.0
        self.canvas     = tk.Canvas(
            parent, width=width, height=height,
            bg=bg, highlightthickness=0,
        )
        self._job = None
        self._draw()

    # ------------------------------------------------------------------

    def set(self, pct: float) -> None:
        self.value = max(0.0, min(100.0, float(pct or 0)))
        self._draw()

    def animate_to(self, pct: float, duration_ms: int = 250) -> None:
        target = max(0.0, min(100.0, float(pct or 0)))
        start  = self.value
        steps  = max(1, duration_ms // 20)
        if self._job:
            self.canvas.after_cancel(self._job)

        def tick(step: int = 1):
            self.value = start + (target - start) * (step / steps)
            self._draw()
            if step < steps:
                self._job = self.canvas.after(20, lambda: tick(step + 1))
            else:
                self._job = None

        tick()

    # ------------------------------------------------------------------

    def _draw(self) -> None:
        c  = self.canvas
        w  = self.width
        h  = self.height
        r  = h / 2          # radius = half height → pill shape
        c.delete("all")

        # Track (pill background)
        self._pill(c, 0, 0, w, h, r, self.track_color)

        # Fill (pill foreground)
        fill_w = max(0.0, w * (self.value / 100))
        if fill_w >= h:     # only draw when wide enough to look like a pill
            self._pill(c, 0, 0, fill_w, h, r, self.fill_color)

    @staticmethod
    def _pill(canvas: tk.Canvas, x1: float, y1: float,
              x2: float, y2: float, r: float, color: str) -> None:
        """Draw a horizontal pill (rectangle with semicircle caps)."""
        r = min(r, (x2 - x1) / 2, (y2 - y1) / 2)
        points = [
            x1 + r, y1,  x2 - r, y1,
            x2,     y1,  x2,     y1 + r,
            x2,     y2 - r,  x2, y2,
            x2 - r, y2,  x1 + r, y2,
            x1,     y2,  x1,     y2 - r,
            x1,     y1 + r,  x1, y1,
        ]
        canvas.create_polygon(points, smooth=True, fill=color, outline="")
