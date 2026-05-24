"""
ring_gauge.py — Animated circular gauge for the Windows widget.
Minor update: uses Segoe UI Variable instead of monospace.
"""
import tkinter as tk
from PIL import Image, ImageTk, ImageDraw

from config import TRANSPARENT_KEY

def _hex_to_rgb(hex_color: str) -> tuple:
    hex_color = hex_color.lstrip('#')
    if len(hex_color) >= 6:
        return tuple(int(hex_color[i:i+2], 16) for i in (0, 2, 4))
    return (0, 0, 0)

class RingGauge:
    """Animated ring / donut gauge drawn on a transparent Canvas."""

    def __init__(self, parent, size: int, ring_width: int,
                 color: str, track_color: str, text_color: str,
                 bg: str, font: str = "Segoe UI Variable", font_size: int = 11):
        self.size       = size
        self.ring_width = ring_width
        self.color      = color
        self.track_color = track_color
        self.text_color  = text_color
        self.bg          = bg
        self.font        = font
        self.font_size   = font_size
        self.value       = 0.0
        self.label_text  = "--"

        self.canvas = tk.Canvas(
            parent,
            width=size, height=size,
            bg=bg,
            highlightthickness=0,
        )
        self._job = None
        self._image_tk = None
        self._draw()

    # ------------------------------------------------------------------

    def set(self, pct: float, label_text: str | None = None) -> None:
        self.value = max(0.0, min(100.0, float(pct or 0)))
        if label_text is not None:
            self.label_text = label_text
        self._draw()

    def animate_to(self, pct: float, label_text: str | None = None,
                   duration_ms: int = 300) -> None:
        target = max(0.0, min(100.0, float(pct or 0)))
        start  = self.value
        steps  = max(1, duration_ms // 30)
        if self._job:
            self.canvas.after_cancel(self._job)

        def tick(step: int = 1):
            t = step / steps
            self.value = start + (target - start) * t
            if label_text is not None:
                self.label_text = label_text
            self._draw()
            if step < steps:
                self._job = self.canvas.after(30, lambda: tick(step + 1))
            else:
                self._job = None

        tick()

    # ------------------------------------------------------------------

    def _draw(self) -> None:
        c   = self.canvas
        s   = self.size

        # Super-sample for anti-aliasing (draw at 4x size, scale down)
        scale = 4
        img_s = s * scale
        
        # Base image with background color
        bg_rgb = _hex_to_rgb(self.bg)
        img = Image.new("RGB", (img_s, img_s), bg_rgb)
        draw = ImageDraw.Draw(img)

        pad = (self.ring_width + 2) * scale
        bounds = [pad, pad, img_s - pad, img_s - pad]
        width = self.ring_width * scale

        track_rgb = _hex_to_rgb(self.track_color)
        color_rgb = _hex_to_rgb(self.color)

        # Track (full circle)
        draw.arc(bounds, 0, 360, fill=track_rgb, width=width)

        # Filled arc (Pillow: 0 is 3 o'clock, clockwise. We want to start at 270 / top)
        extent = self.value / 100.0 * 360.0
        if extent > 0.5:
            draw.arc(bounds, 270, 270 + extent, fill=color_rgb, width=width)

        # Resize with Lanczos for smooth anti-aliasing
        img = img.resize((s, s), Image.Resampling.LANCZOS)
        self._image_tk = ImageTk.PhotoImage(img)

        c.delete("all")
        c.create_image(s / 2, s / 2, image=self._image_tk)

        # Centre label
        c.create_text(
            s / 2, s / 2,
            text=self.label_text,
            fill=self.text_color,
            font=(self.font, self.font_size, "bold"),
        )
