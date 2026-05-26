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
import os
import queue
import random
import threading
import tkinter as tk

from utils.ui import PanelCard, make_label

# Set env var SYSMON_DEBUG_MUSIC=1 to print SMTC diagnostics to stdout.
_DEBUG_MUSIC = bool(os.environ.get("SYSMON_DEBUG_MUSIC"))


def _dbg(msg: str) -> None:
    if _DEBUG_MUSIC:
        print(f"[music] {msg}", flush=True)

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
# UI constants
# ---------------------------------------------------------------------------
# Per-status icon glyphs. Unknown / Stopped falls back to the music note.
STATUS_ICONS = {
    "Playing": "▶",
    "Paused":  "⏸",
}
_DEFAULT_STATUS_ICON = "♪"


def _status_icon(status: str) -> str:
    return STATUS_ICONS.get(status, _DEFAULT_STATUS_ICON)


def _clean_meta(value: str) -> str:
    """Filter literal 'Unknown' values that some players publish to SMTC."""
    if not value:
        return ""
    if value.strip().lower() == "unknown":
        return ""
    return value


# ---------------------------------------------------------------------------
# Async helper — runs in a background thread
# ---------------------------------------------------------------------------

def _member(obj, *names):
    for name in names:
        if hasattr(obj, name):
            return getattr(obj, name)
    raise AttributeError(names[0])


def _call(obj, *names):
    return _member(obj, *names)()


async def _call_async(obj, *names):
    return await _member(obj, *names)()


def _read_attr(obj, *names, default=None):
    for name in names:
        if hasattr(obj, name):
            return getattr(obj, name)
    return default


def _status_name(raw) -> str:
    name = getattr(raw, "name", None)
    if name:
        name = str(name).upper()
        if "PLAY" in name:
            return "Playing"
        if "PAUS" in name:
            return "Paused"
        if "STOP" in name:
            return "Stopped"

    value = getattr(raw, "value", raw)
    try:
        value = int(value)
    except Exception:
        return "Stopped"
    return {
        0: "Closed",
        1: "Opened",
        2: "Changing",
        3: "Stopped",
        4: "Playing",
        5: "Paused",
    }.get(value, "Stopped")


def _seconds(value) -> float:
    if value is None:
        return 0.0
    if hasattr(value, "total_seconds"):
        try:
            return float(value.total_seconds())
        except Exception:
            return 0.0
    try:
        return float(value) / 10_000_000.0
    except Exception:
        return 0.0


async def _smtc_query() -> dict:
    """Return now-playing info from SMTC or an empty/stopped dict."""
    try:
        manager = await _call_async(_GSMTC, "request_async", "RequestAsync")
        session = _call(manager, "get_current_session", "GetCurrentSession")
        if session is None:
            _dbg("smtc: no current session")
            return {"status": "Stopped"}

        props = await _call_async(
            session,
            "try_get_media_properties_async",
            "TryGetMediaPropertiesAsync",
        )
        playback = _call(session, "get_playback_info", "GetPlaybackInfo")
        timeline = _call(
            session,
            "get_timeline_properties",
            "GetTimelineProperties",
        )

        # winrt 3.x returns an enum — handle both .name and int .value gracefully
        status = _status_name(
            _read_attr(playback, "playback_status", "PlaybackStatus")
        )

        # Position / duration in 100-ns ticks → seconds
        pos_sec = _seconds(_read_attr(timeline, "position", "Position"))
        dur_sec = _seconds(_read_attr(timeline, "end_time", "EndTime"))

        result = {
            "status":   status,
            "title":    _read_attr(props, "title", "Title", default="") or "",
            "artist":   _read_attr(props, "artist", "Artist", default="") or "",
            "album":    _read_attr(props, "album_title", "AlbumTitle", default="") or "",
            "position": pos_sec,
            "duration": dur_sec,
        }
        _dbg(
            f"smtc: status={result['status']} "
            f"title={result['title']!r} pos={pos_sec:.1f} dur={dur_sec:.1f}"
        )
        return result
    except Exception as exc:
        _dbg(f"smtc: exception {exc!r}")
        return {"status": "Stopped"}


def _fetch_smtc_threaded(result_queue: queue.Queue, request_id: int) -> None:
    """Run the async SMTC query in a fresh event loop and push result."""
    loop = None
    try:
        loop = asyncio.new_event_loop()
        info = loop.run_until_complete(_smtc_query())
    except Exception:
        info = {"status": "Stopped"}
    finally:
        if loop is not None:
            loop.close()
    result_queue.put((request_id, info))


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
        self._fetch_in_flight = False
        self._fetch_started_ms = 0
        self._fetch_request_id = 0
        self._fetch_thread: threading.Thread | None = None
        self._fetch_timed_out = False
        # Health tracking for SMTC polling
        self._fetch_durations: list[float] = []  # last N query durations (ms)
        self._circuit_open_until_ms: int = 0     # pause SMTC polling until ms
        # End-of-track detection (status -> Stopped if position pinned at
        # duration for several ticks without SMTC refresh)
        self._end_of_track_ticks: int = 0

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
            inner, height=4,
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

        # Bind click on every widget in this panel to act as a "manual
        # refresh" — useful when the publishing app (e.g. Dopamine) fails to
        # update SMTC after a track switch and the panel gets stuck on stale
        # metadata. Cheap: one bind per child widget at startup, no runtime
        # cost.
        self._bind_click_recursive(self.widget)

    # ------------------------------------------------------------------
    # Manual refresh (click anywhere on the panel)
    # ------------------------------------------------------------------

    def _bind_click_recursive(self, widget) -> None:
        try:
            widget.bind("<Button-1>", self._force_refresh)
        except Exception:
            pass
        try:
            children = widget.winfo_children()
        except Exception:
            children = []
        for child in children:
            self._bind_click_recursive(child)

    def _force_refresh(self, event=None) -> None:
        """Force an immediate SMTC fetch and reset polling guards.

        Tap on the music panel to call this. Useful when SMTC is serving
        stale metadata (some players don't update SMTC on every track
        switch) and the panel appears frozen.
        """
        if not _WINRT_OK:
            return
        _dbg("manual refresh requested")
        # Drop polling guards so the next _start_fetch_if_idle isn't blocked.
        self._fetch_in_flight = False
        self._fetch_thread = None
        self._fetch_timed_out = False
        self._circuit_open_until_ms = 0
        self._fetch_durations.clear()
        self._start_fetch_if_idle()

    # ------------------------------------------------------------------
    # Health helpers
    # ------------------------------------------------------------------

    def _now_ms(self) -> int:
        return int(self.widget.winfo_toplevel().tk.call("clock", "milliseconds"))

    def _circuit_open(self) -> bool:
        """True while SMTC polling is paused due to consecutive slow queries."""
        if self._circuit_open_until_ms == 0:
            return False
        if self._now_ms() < self._circuit_open_until_ms:
            return True
        # Cooldown elapsed: reset state and resume normal polling.
        self._circuit_open_until_ms = 0
        self._fetch_durations.clear()
        return False

    def _record_duration(self, ms: float) -> None:
        """Track query duration; open circuit if SMTC is consistently slow."""
        self._fetch_durations.append(float(ms))
        if len(self._fetch_durations) > 5:
            self._fetch_durations.pop(0)
        # If 3+ of the last 5 queries took >10s (i.e. they tripped the existing
        # timeout marker), pause polling for 10 minutes.
        if len(self._fetch_durations) >= 3:
            slow = sum(1 for d in self._fetch_durations if d > 10_000)
            if slow >= 3:
                _dbg(
                    f"circuit breaker tripped → pausing SMTC polling 10min "
                    f"(durations_ms={[int(d) for d in self._fetch_durations]})"
                )
                self._circuit_open_until_ms = self._now_ms() + 600_000
                self._fetch_durations.clear()

    # ------------------------------------------------------------------
    # SMTC polling (non-blocking: fires off thread, checks queue next tick)
    # ------------------------------------------------------------------

    def _schedule_refresh(self) -> None:
        if _WINRT_OK and not self._circuit_open():
            self._start_fetch_if_idle()
        self.widget.after(self._refresh_interval_ms(), self._check_refresh)

    def _refresh_interval_ms(self) -> int:
        refresh_ms = max(5000, int(self.music_cfg.get("refresh_ms", 5000)))
        if self._circuit_open():
            # Circuit open: tick rarely. Polling is suppressed in
            # _schedule_refresh, but we still need ticks so the panel resumes
            # automatically once the cooldown ends.
            return max(refresh_ms, 60000)
        if self._fetch_timed_out:
            return max(refresh_ms, 30000)
        if self.status == "Playing":
            return max(refresh_ms, 15000)
        return refresh_ms

    def _start_fetch_if_idle(self) -> None:
        if self._fetch_in_flight:
            # Watchdog: if the previous thread has been running > 5 minutes,
            # abandon it. Its eventual result is ignored because we bump
            # _fetch_request_id before starting the new thread below.
            elapsed = self._now_ms() - self._fetch_started_ms
            if elapsed > 300_000:
                _dbg(f"watchdog: abandoning hung thread (elapsed={elapsed}ms)")
                self._fetch_thread = None
                self._fetch_in_flight = False
                self._record_duration(elapsed)
            elif self._fetch_thread is not None and self._fetch_thread.is_alive():
                return
            else:
                self._fetch_in_flight = False
                self._fetch_thread = None

        self._fetch_in_flight = True
        self._fetch_timed_out = False
        self._fetch_started_ms = self._now_ms()
        self._fetch_request_id += 1
        self._fetch_thread = threading.Thread(
            target=_fetch_smtc_threaded,
            args=(self._info_queue, self._fetch_request_id),
            daemon=True,
        )
        self._fetch_thread.start()

    def _check_refresh(self) -> None:
        info = None
        while True:
            try:
                item = self._info_queue.get_nowait()
            except queue.Empty:
                break
            request_id, result = item
            if request_id == self._fetch_request_id:
                info = result

        if info is not None:
            duration_ms = self._now_ms() - self._fetch_started_ms
            self._record_duration(duration_ms)
            self._fetch_in_flight = False
            self._fetch_thread = None
            self._fetch_timed_out = False
            self._apply_info(info)
        else:
            now_ms = self._now_ms()
            if (
                self._fetch_in_flight
                and not self._fetch_timed_out
                and now_ms - self._fetch_started_ms > 10000
            ):
                self._fetch_timed_out = True
            if (
                self._fetch_in_flight
                and self._fetch_thread is not None
                and not self._fetch_thread.is_alive()
            ):
                self._fetch_in_flight = False
                self._fetch_thread = None
        self._schedule_refresh()

    def _apply_info(self, info: dict) -> None:
        self.status    = info.get("status", "Stopped")
        self.position  = float(info.get("position", 0))
        self.duration  = float(info.get("duration", 0))
        self.title_text = _clean_meta(info.get("title") or "")
        # Fresh SMTC info — reset the local "stuck at end" counter.
        self._end_of_track_ticks = 0

        artist = _clean_meta(info.get("artist") or "")
        album  = _clean_meta(info.get("album") or "")

        self.status_label.configure(text=f"{_status_icon(self.status)}  {self.status}")
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
            new_pos = self.position + 1.0
            if self.duration > 0 and new_pos >= self.duration:
                new_pos = self.duration
                self._end_of_track_ticks += 1
                # If we've been pinned at the end for 3+ ticks (~3s) without
                # SMTC pushing a fresh update, treat the track as ended. Some
                # players (e.g. Dopamine) stop publishing SMTC updates between
                # songs, leaving status=Playing forever otherwise. Once SMTC
                # finally refreshes, _apply_info will correct the state.
                if self._end_of_track_ticks >= 3:
                    _dbg("end-of-track local transition → status=Stopped")
                    self.status = "Stopped"
                    self.status_label.configure(
                        text=f"{_status_icon(self.status)}  {self.status}"
                    )
            else:
                self._end_of_track_ticks = 0
            self.position = new_pos
            self._draw_time()
            self._draw_progress()
        else:
            self._end_of_track_ticks = 0
        self.widget.after(1000, self._local_tick)

    # ------------------------------------------------------------------
    # Animation loop
    # ------------------------------------------------------------------

    def _animate(self) -> None:
        if self.status == "Playing":
            self.vis_values = [
                max(4, min(self.music_cfg["vis_height"],
                           int(v * 0.55 + random.randint(4, self.music_cfg["vis_height"]) * 0.45)))
                for v in self.vis_values
            ]
        else:
            self.vis_values = [max(4, v - 2) for v in self.vis_values]

        self._draw_visualizer()
        self._draw_title()
        delay = self.music_cfg.get("animate_ms", 200)
        if self.status != "Playing":
            delay = max(1000, delay)
        self.widget.after(delay, self._animate)

    # ------------------------------------------------------------------
    # Drawing helpers
    # ------------------------------------------------------------------

    def _draw_title(self) -> None:
        c = self.title_canvas
        c.delete("all")
        w = max(1, c.winfo_width())
        text = self.title_text
        if not text:
            self.title_offset = 0.0
            return
        fill = self.config["accent"]["text_main"]
        font = (self.config["clock"]["font"], 12, "bold")
        text_id = c.create_text(
            -self.title_offset, 11,
            anchor="w", text=text, fill=fill, font=font,
        )
        bbox = c.bbox(text_id)
        text_w = (bbox[2] - bbox[0]) if bbox else 0
        if text_w > w:
            # Draw a second copy after a gap for seamless scroll wrap-around.
            # When the first copy slides off the left, the second is already
            # in view, so the marquee never visually "jumps".
            gap = 30
            c.create_text(
                -self.title_offset + text_w + gap, 11,
                anchor="w", text=text, fill=fill, font=font,
            )
            speed = self.music_cfg.get("marquee_speed", 30)
            if self.status == "Playing":
                self.title_offset = (self.title_offset + speed / 12.5) % (text_w + gap)
        else:
            self.title_offset = 0.0

    def _draw_time(self) -> None:
        def fmt(s: float) -> str:
            s = max(0, int(s))
            return f"{s // 60}:{s % 60:02d}"
        # Empty state: no time text when there's no track and nothing playing.
        if self.duration <= 0 and self.position <= 0:
            self.time_label.configure(text="")
            return
        # Streaming / unknown duration: show only elapsed.
        if self.duration <= 0:
            self.time_label.configure(text=fmt(self.position))
            return
        self.time_label.configure(
            text=f"{fmt(self.position)} / {fmt(self.duration)}"
        )

    def _draw_progress(self) -> None:
        c = self.prog_canvas
        c.delete("all")
        w = max(1, c.winfo_width())
        h = 4
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
