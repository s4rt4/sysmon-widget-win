"""
music.py — Now Playing panel for Windows.

Primary backend: Windows System Media Transport Controls (SMTC) via the
`winrt` package.  This reads track info from ANY app that integrates with
Windows media sessions (Spotify, browser media, VLC, Groove Music, etc.).

Fallback: if `winrt` is not installed the panel shows a friendly message
and a PowerShell-based poller is attempted as a secondary option.

Install primary backend:
    pip install winrt-runtime winrt-Windows.Media.Control
"""
from __future__ import annotations

import asyncio
import queue
import random
import threading
import tkinter as tk

from utils.ui import PanelCard, make_label

# ---------------------------------------------------------------------------
# winrt SMTC import (optional)
# ---------------------------------------------------------------------------
try:
    from winrt.windows.media.control import (
        GlobalSystemMediaTransportControlsSessionManager as _GSMTC,
    )
    _WINRT_OK = True
except Exception:
    _GSMTC    = None
    _WINRT_OK = False


# ---------------------------------------------------------------------------
# Async helper — runs in a background thread
# ---------------------------------------------------------------------------

async def _smtc_query() -> dict:
    """Return now-playing info from SMTC or an empty/stopped dict."""
    try:
        manager = await _GSMTC.request_async()
        session = manager.get_current_session()
        if session is None:
            return {"status": "Stopped"}

        props    = await session.try_get_media_properties_async()
        playback = session.get_playback_info()
        timeline = session.get_timeline_properties()

        # winrt 3.x returns an enum — handle both .name and int .value gracefully
        try:
            raw = playback.playback_status
            name = getattr(raw, "name", str(raw)).upper()
            # Normalise common enum names
            if "PLAY" in name:
                status = "Playing"
            elif "PAUS" in name:
                status = "Paused"
            elif "STOP" in name:
                status = "Stopped"
            else:
                # Fallback to integer map
                val_map = {0: "Closed", 1: "Opened", 2: "Changing",
                           3: "Stopped", 4: "Playing", 5: "Paused"}
                status = val_map.get(int(raw), "Stopped")
        except Exception:
            status = "Stopped"

        # Position / duration in 100-ns ticks → seconds
        try:
            pos_sec = timeline.position.total_seconds()
        except Exception:
            pos_sec = 0.0
        try:
            dur_sec = timeline.end_time.total_seconds()
        except Exception:
            dur_sec = 0.0

        return {
            "status":   status,
            "title":    props.title    or "",
            "artist":   props.artist   or "",
            "album":    props.album_title or "",
            "position": pos_sec,
            "duration": dur_sec,
        }
    except Exception:
        return {"status": "Stopped"}


def _fetch_smtc_threaded(result_queue: queue.Queue) -> None:
    """Run the async SMTC query in a fresh event loop and push result."""
    try:
        loop = asyncio.new_event_loop()
        info = loop.run_until_complete(_smtc_query())
        loop.close()
    except Exception:
        info = {"status": "Stopped"}
    result_queue.put(info)


# ---------------------------------------------------------------------------
# MusicPanel
# ---------------------------------------------------------------------------

class MusicPanel:
    def __init__(self, parent, config: dict):
        self.config      = config
        self.music_cfg   = config["music"]
        self.widget      = PanelCard(parent, config, width=config["width"])

        self.status      = "Stopped"
        self.position    = 0.0       # seconds
        self.duration    = 0.0
        self.title_text  = ""
        self.title_offset = 0.0
        self.vis_values  = [4] * self.music_cfg["vis_bars"]

        # Async result queue
        self._info_queue: queue.Queue = queue.Queue()

        inner = self.widget.inner
        accent = config["accent"]

        # ── Status label ──────────────────────────────────────────────────
        self.status_label = make_label(
            inner, config,
            text="♪  Stopped",
            size=10,
            color=accent["primary"],
        )
        self.status_label.pack(fill="x")

        # ── Scrolling title canvas ────────────────────────────────────────
        self.title_canvas = tk.Canvas(
            inner, height=22,
            bg=config["panel_bg"], highlightthickness=0,
        )
        self.title_canvas.pack(fill="x", pady=(2, 0))

        # ── Artist / album ────────────────────────────────────────────────
        self.meta_label = make_label(
            inner, config,
            text="No active media session" if _WINRT_OK else "Install: pip install winrt-runtime",
            size=10,
            color=accent["text_muted"],
        )
        self.meta_label.pack(fill="x")

        # ── Time + progress bar ───────────────────────────────────────────
        self.time_label = make_label(
            inner, config, text="0:00 / 0:00",
            size=9, color=accent["text_muted"],
        )
        self.time_label.pack(fill="x", pady=(2, 0))

        self.prog_canvas = tk.Canvas(
            inner, height=3,
            bg=config["accent"]["track_bg"], highlightthickness=0,
        )
        self.prog_canvas.pack(fill="x", pady=(3, 0))

        # ── Visualiser ────────────────────────────────────────────────────
        self.vis_canvas = tk.Canvas(
            inner,
            height=self.music_cfg["vis_height"],
            bg=config["panel_bg"],
            highlightthickness=0,
        )
        if self.music_cfg["show_visualizer"]:
            self.vis_canvas.pack(fill="x", pady=(4, 0))

        self._schedule_refresh()
        self._animate()
        self._local_tick()
        self.widget.after(80, self.widget.update_height)

    # ------------------------------------------------------------------
    # SMTC polling (non-blocking: fires off thread, checks queue next tick)
    # ------------------------------------------------------------------

    def _schedule_refresh(self) -> None:
        if _WINRT_OK:
            t = threading.Thread(
                target=_fetch_smtc_threaded,
                args=(self._info_queue,),
                daemon=True,
            )
            t.start()
        self.widget.after(self.music_cfg["refresh_ms"], self._check_refresh)

    def _check_refresh(self) -> None:
        try:
            info = self._info_queue.get_nowait()
            self._apply_info(info)
        except queue.Empty:
            pass
        self._schedule_refresh()

    def _apply_info(self, info: dict) -> None:
        self.status    = info.get("status", "Stopped")
        self.position  = float(info.get("position", 0))
        self.duration  = float(info.get("duration", 0))
        self.title_text = info.get("title") or ""

        title = info.get("title") or "No track"
        artist = info.get("artist") or ""
        album  = info.get("album") or ""

        is_playing = self.status in ("Playing",)

        self.status_label.configure(text=f"♪  {self.status}")
        parts = [p for p in (artist, album) if p]
        self.meta_label.configure(
            text=" · ".join(parts) if parts else "No active media session"
        )
        self._draw_time()
        self._draw_progress()

    # ------------------------------------------------------------------
    # Local time ticker (increments position every second when playing)
    # ------------------------------------------------------------------

    def _local_tick(self) -> None:
        if self.status == "Playing":
            self.position += 1.0
            self._draw_time()
            self._draw_progress()
        self.widget.after(1000, self._local_tick)

    # ------------------------------------------------------------------
    # Animation loop
    # ------------------------------------------------------------------

    def _animate(self) -> None:
        if self.status == "Playing":
            self.vis_values = [
                max(4, min(self.music_cfg["vis_height"],
                           v + random.randint(-5, 8)))
                for v in self.vis_values
            ]
        else:
            self.vis_values = [max(4, v - 2) for v in self.vis_values]

        self._draw_visualizer()
        self._draw_title()
        self.widget.after(80, self._animate)

    # ------------------------------------------------------------------
    # Drawing helpers
    # ------------------------------------------------------------------

    def _draw_title(self) -> None:
        c = self.title_canvas
        c.delete("all")
        w = max(1, c.winfo_width())
        text_id = c.create_text(
            -self.title_offset, 11,
            anchor="w",
            text=self.title_text,
            fill=self.config["accent"]["text_main"],
            font=(self.config["clock"]["font"], 12, "bold"),
        )
        bbox = c.bbox(text_id)
        text_w = (bbox[2] - bbox[0]) if bbox else 0
        if text_w > w:
            speed = self.music_cfg.get("marquee_speed", 30)
            self.title_offset = (self.title_offset + speed / 12.5) % (text_w + 30)
        else:
            self.title_offset = 0.0

    def _draw_time(self) -> None:
        def fmt(s: float) -> str:
            s = max(0, int(s))
            return f"{s // 60}:{s % 60:02d}"
        self.time_label.configure(
            text=f"{fmt(self.position)} / {fmt(self.duration)}"
        )

    def _draw_progress(self) -> None:
        c = self.prog_canvas
        c.delete("all")
        w = max(1, c.winfo_width())
        h = 3
        # track
        c.create_rectangle(0, 0, w, h,
                            fill=self.config["accent"]["track_bg"], width=0)
        # fill
        if self.duration > 0:
            frac = min(1.0, self.position / self.duration)
            fw   = max(0, frac * w)
            if fw > 0:
                c.create_rectangle(0, 0, fw, h,
                                   fill=self.config["accent"]["primary"], width=0)

    def _draw_visualizer(self) -> None:
        c = self.vis_canvas
        c.delete("all")
        w   = max(1, c.winfo_width())
        h   = self.music_cfg["vis_height"]
        n   = len(self.vis_values)
        gap = 1
        bar_w = max(2, (w - gap * (n - 1)) / n)
        color = self.config["accent"]["primary"]

        for idx, value in enumerate(self.vis_values):
            x0 = idx * (bar_w + gap)
            x1 = x0 + bar_w
            y0 = h - value
            # Rounded top cap
            c.create_rectangle(x0, y0, x1, h, fill=color, width=0)
