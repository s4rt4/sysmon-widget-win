"""
ui.py — Core UI primitives for the Windows widget.

Key change from Debian version:
  PanelFrame (tk.Frame) → PanelCard (tk.Canvas)

PanelCard draws a rounded-rectangle background directly on a Canvas so that
any pixel that is NOT part of the panel card remains the transparent-colour
key and becomes see-through on Windows layered windows.
"""
import tkinter as tk

from config import TRANSPARENT_KEY


# ---------------------------------------------------------------------------
# Rounded-rectangle polygon helper
# ---------------------------------------------------------------------------

def _rounded_rect(canvas: tk.Canvas, x1: float, y1: float, x2: float, y2: float,
                  r: float, **kwargs):
    """Draw a smooth rounded-rectangle polygon on *canvas*.

    Returns the polygon item id so callers can use tag_lower / tag_raise.
    """
    r = min(r, (x2 - x1) / 2, (y2 - y1) / 2)
    points = [
        x1 + r, y1,  x2 - r, y1,
        x2,     y1,  x2,     y1 + r,
        x2,     y2 - r,  x2, y2,
        x2 - r, y2,  x1 + r, y2,
        x1,     y2,  x1,     y2 - r,
        x1,     y1 + r,  x1, y1,
    ]
    return canvas.create_polygon(points, smooth=True, **kwargs)


# ---------------------------------------------------------------------------
# PanelCard — Canvas-based rounded panel
# ---------------------------------------------------------------------------

class PanelCard(tk.Canvas):
    """A transparent Canvas that draws a rounded-corner panel background.

    Children should be added to ``self.inner`` (a plain tk.Frame placed
    inside the canvas at the correct padding offset).
    """

    def __init__(self, parent, config: dict, **kwargs):
        self._cfg         = config
        self._panel_bg    = config.get("panel_bg", "#192F2D")
        self._radius      = config.get("corner_radius", 14)
        self._padding     = config.get("panel_padding", 14)
        self._transparent = config.get("transparent_key", TRANSPARENT_KEY)

        super().__init__(
            parent,
            bg=self._transparent,
            highlightthickness=0,
            **kwargs,
        )

        # Inner frame where panel content lives
        self.inner = tk.Frame(self, bg=self._panel_bg)

        self.bind("<Configure>", self._redraw)

    # ------------------------------------------------------------------
    # Override cget so child widgets asking for "bg" get the panel colour
    # ------------------------------------------------------------------
    def cget(self, key: str):
        if key in ("bg", "background"):
            return self._panel_bg
        return super().cget(key)

    # ------------------------------------------------------------------
    # Drawing
    # ------------------------------------------------------------------
    def _redraw(self, _event=None):
        self.delete("_bg")
        w = max(1, self.winfo_width())
        h = max(1, self.winfo_height())
        _rounded_rect(
            self, 1, 1, w - 1, h - 1,
            self._radius,
            fill=self._panel_bg,
            outline=self._cfg.get("separator_color", "#58757B"),
            width=0,
            tags="_bg",
        )
        self.tag_lower("_bg")
        # Re-place inner frame with padding
        p = self._padding
        self.inner.place(x=p, y=p, width=max(1, w - p * 2), height=max(1, h - p * 2))

    def update_height(self):
        """Call after all children are packed into self.inner to resize canvas."""
        self.inner.update_idletasks()
        inner_h = self.inner.winfo_reqheight()
        total_h = inner_h + self._padding * 2
        self.configure(height=total_h)
        self._redraw()


# ---------------------------------------------------------------------------
# Convenience factory: label inside a PanelCard's inner frame or any widget
# ---------------------------------------------------------------------------

def make_label(parent, config: dict, text: str = "", size: int = 12,
               color: str | None = None, weight: str = "normal",
               anchor: str = "w") -> tk.Label:
    accent = config["accent"]
    # Determine the background colour of the parent
    try:
        bg = parent.cget("bg")
    except Exception:
        bg = config.get("panel_bg", "#192F2D")

    return tk.Label(
        parent,
        text=text,
        bg=bg,
        fg=color or accent["text_main"],
        font=(config["clock"]["font"], size, weight),
        anchor=anchor,
        justify="left",
    )


# ---------------------------------------------------------------------------
# Thin separator line between panels (drawn on the root frame)
# ---------------------------------------------------------------------------

def make_separator(parent, config: dict) -> tk.Frame:
    return tk.Frame(
        parent,
        bg=config.get("transparent_key", TRANSPARENT_KEY),
        height=config.get("panel_gap", 10),
        highlightthickness=0,
    )


# ---------------------------------------------------------------------------
# Byte formatting helper (unchanged from Debian version)
# ---------------------------------------------------------------------------

def format_bytes(value: float) -> str:
    value = float(max(0, value))
    for unit in ("B", "KiB", "MiB", "GiB"):
        if value < 1024 or unit == "GiB":
            if unit == "B":
                return f"{value:.0f} B"
            return f"{value:.2f} {unit}"
        value /= 1024
    return f"{value:.2f} GiB"
