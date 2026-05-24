"""
weather.py — Weather panel using OpenWeatherMap API.
Logic unchanged from Debian version; updated to use PanelCard.
"""
from io import BytesIO
import queue
import threading
import tkinter as tk

try:
    import requests
except ImportError:
    requests = None

try:
    from PIL import Image, ImageTk
except ImportError:
    Image = ImageTk = None

from utils.ui import PanelCard, make_label


_LAST_WEATHER: dict | None = None
_LAST_ICON_BYTES: bytes | None = None


class WeatherPanel:
    def __init__(self, parent, config: dict):
        self.config       = config
        self.weather_cfg  = config["weather"]
        self.widget       = PanelCard(parent, config, width=config["width"])
        self.icon_photo   = None
        self._fetch_queue: queue.Queue = queue.Queue()
        self._fetch_in_flight = False

        inner = self.widget.inner

        # Two-column layout inside inner frame
        self.left  = tk.Frame(inner, bg=config["panel_bg"])
        self.right = tk.Frame(inner, bg=config["panel_bg"])
        self.left.pack(side="left", fill="y", padx=(0, 8))
        self.right.pack(side="left", fill="both", expand=True)

        self.icon_label = make_label(
            self.left, config, text="☁", size=30, anchor="center"
        )
        self.icon_label.pack(anchor="center")

        self.temp_label = make_label(
            self.left, config, text="--°C", size=16, weight="bold", anchor="center"
        )
        self.temp_label.pack(anchor="center", pady=(4, 0))

        self.city_label = make_label(
            self.right, config,
            text=self.weather_cfg["city"], size=11, weight="bold",
            color=config["accent"]["primary"],
        )
        self.city_label.pack(fill="x")

        self.detail_label = make_label(
            self.right, config,
            text="Loading weather",
            size=9,
            color=config["accent"]["text_muted"],
        )
        self.detail_label.pack(fill="x", pady=(4, 0))

        self._refresh()
        self.widget.after(80, self.widget.update_height)

    def _refresh(self) -> None:
        if not self._fetch_in_flight:
            self._fetch_in_flight = True
            threading.Thread(target=self._fetch_threaded, daemon=True).start()
            self.widget.after(100, self._drain_fetch_queue)
        self.widget.after(self.weather_cfg["refresh_sec"] * 1000, self._refresh)

    def _fetch_threaded(self) -> None:
        global _LAST_ICON_BYTES
        data = self._fetch()
        icon_bytes = None
        if data:
            icon_bytes = self._fetch_icon_bytes(data)
            if icon_bytes:
                _LAST_ICON_BYTES = icon_bytes
        self._fetch_queue.put((data, icon_bytes))

    def _drain_fetch_queue(self) -> None:
        global _LAST_WEATHER
        global _LAST_ICON_BYTES
        try:
            data, icon_bytes = self._fetch_queue.get_nowait()
        except queue.Empty:
            if self._fetch_in_flight:
                self.widget.after(100, self._drain_fetch_queue)
            return

        self._fetch_in_flight = False
        if data:
            self._render(data, offline=False, icon_bytes=icon_bytes)
        elif _LAST_WEATHER:
            self._render(_LAST_WEATHER, offline=True, icon_bytes=_LAST_ICON_BYTES)
        else:
            self.detail_label.configure(text="Weather unavailable")

    def _fetch(self) -> dict | None:
        global _LAST_WEATHER
        key = self.weather_cfg["api_key"]
        if requests is None or not key or key == "YOUR_OPENWEATHERMAP_API_KEY":
            return None
        params: dict = {"appid": key, "units": self.weather_cfg["units"]}
        if self.weather_cfg.get("city_id"):
            params["id"] = self.weather_cfg["city_id"]
        else:
            params["q"] = f"{self.weather_cfg['city']},{self.weather_cfg['country_code']}"
        try:
            resp = requests.get(
                "https://api.openweathermap.org/data/2.5/weather",
                params=params, timeout=8,
            )
            resp.raise_for_status()
            _LAST_WEATHER = resp.json()
            return _LAST_WEATHER
        except Exception:
            return None

    def _fetch_icon_bytes(self, data: dict) -> bytes | None:
        weather = data.get("weather", [{}])[0]
        icon_code = weather.get("icon")
        if not icon_code or requests is None:
            return None
        try:
            resp = requests.get(
                f"https://openweathermap.org/img/wn/{icon_code}@2x.png",
                timeout=8,
            )
            resp.raise_for_status()
            return resp.content
        except Exception:
            return None

    def _render(
        self,
        data: dict,
        offline: bool = False,
        icon_bytes: bytes | None = None,
    ) -> None:
        weather = data.get("weather", [{}])[0]
        main    = data.get("main", {})
        wind    = data.get("wind", {})
        temp    = main.get("temp")
        city    = data.get("name", self.weather_cfg["city"])
        desc    = weather.get("description", "unknown").title()
        suffix  = "  (offline)" if offline else ""

        self.temp_label.configure(
            text=f"{round(temp) if temp is not None else '--'}°C"
        )
        self.city_label.configure(text=city)

        details = [f"{desc}{suffix}"]
        if self.weather_cfg["show_wind"]:
            speed = wind.get("speed")
            speed_text = f"{speed:.1f}" if isinstance(speed, (int, float)) else "--"
            details.append(f"Wind {speed_text} m/s")
        if self.weather_cfg["show_humidity"]:
            details.append(f"Hum {main.get('humidity', '--')}%")
        self.detail_label.configure(text="\n".join(details))

        if icon_bytes and Image and ImageTk:
            try:
                img = Image.open(BytesIO(icon_bytes)).resize((44, 44))
                self.icon_photo = ImageTk.PhotoImage(img)
                self.icon_label.configure(image=self.icon_photo, text="")
            except Exception:
                self.icon_label.configure(image="", text="☁")
        else:
            self.icon_label.configure(text="☁")

        self.widget.update_height()
