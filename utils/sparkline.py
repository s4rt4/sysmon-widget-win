import tkinter as tk


class Sparkline:
    def __init__(self, parent, width, height, history_len, color, bg):
        self.width = width
        self.height = height
        self.history_len = history_len
        self.color = color
        self.values = [0.0] * history_len
        self.canvas = tk.Canvas(parent, width=width, height=height, bg=bg, highlightthickness=0)
        self._draw()

    def push(self, value):
        self.values.append(max(0, float(value)))
        self.values = self.values[-self.history_len :]
        self._draw()

    def _draw(self):
        self.canvas.delete("all")
        if len(self.values) < 2:
            return
        max_value = max(max(self.values), 1.0)
        points = []
        for idx, value in enumerate(self.values):
            x = idx / (self.history_len - 1) * self.width
            y = self.height - (value / max_value) * (self.height - 2) - 1
            points.extend((x, y))
        self.canvas.create_line(points, fill=self.color, width=2, smooth=True)
