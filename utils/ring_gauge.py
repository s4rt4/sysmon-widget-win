"""
ring_gauge.py — Animated circular gauge for the Windows widget.
Minor update: uses Segoe UI Variable instead of monospace.
"""
import tkinter as tk
from PIL import Image, ImageDraw, ImageTk


def _hex_to_rgb(hex_color: str) -> tuple:
    hex_color = hex_color.lstrip("#")
    if len(hex_color) >= 6:
        return tuple(int(hex_color[i:i + 2], 16) for i in (0, 2, 4))
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
        self._last_draw_value = None
        self._last_draw_label = None
        self._image_cache: dict[int, ImageTk.PhotoImage] = {}
        self._image_tk = None

        self.canvas = tk.Canvas(
            parent,
            width=size, height=size,
            bg=bg,
            highlightthickness=0,
        )
        self._job = None
        self._draw()

    # ------------------------------------------------------------------

    def set(self, pct: float, label_text: str | None = None) -> None:
        next_value = max(0.0, min(100.0, float(pct or 0)))
        next_label = self.label_text if label_text is None else label_text
        if (
            self._last_draw_value is not None
            and abs(next_value - self.value) < 0.5
            and next_label == self.label_text
        ):
            return
        self.value = next_value
        if label_text is not None:
            self.label_text = label_text
        self._draw()

    def animate_to(self, pct: float, label_text: str | None = None,
                   duration_ms: int = 300) -> None:
        target = max(0.0, min(100.0, float(pct or 0)))
        start  = self.value
        if abs(target - start) < 1.0:
            self.set(target, label_text)
            return
        steps  = max(1, min(3, duration_ms // 60))
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
        c.delete("all")
        cache_key = round(self.value)
        image = self._image_cache.get(cache_key)
        if image is None:
            scale = 3
            img_s = s * scale
            img = Image.new("RGB", (img_s, img_s), _hex_to_rgb(self.bg))
            draw = ImageDraw.Draw(img)

            pad = (self.ring_width + 2) * scale
            bounds = [pad, pad, img_s - pad, img_s - pad]
            width = self.ring_width * scale

            draw.arc(bounds, 0, 360, fill=_hex_to_rgb(self.track_color), width=width)
            extent = cache_key / 100.0 * 360.0
            if extent > 0.5:
                draw.arc(bounds, 270, 270 + extent, fill=_hex_to_rgb(self.color), width=width)

            img = img.resize((s, s), Image.Resampling.LANCZOS)
            image = ImageTk.PhotoImage(img)
            if len(self._image_cache) > 128:
                self._image_cache.clear()
            self._image_cache[cache_key] = image

        self._image_tk = image
        c.create_image(s / 2, s / 2, image=self._image_tk)

        # Centre label
        c.create_text(
            s / 2, s / 2,
            text=self.label_text,
            fill=self.text_color,
            font=(self.font, self.font_size, "bold"),
        )
        self._last_draw_value = self.value
        self._last_draw_label = self.label_text
