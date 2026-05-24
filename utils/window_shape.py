def apply_panel_shape(root, radius=14):
    try:
        _apply_panel_shape(root, radius)
    except Exception as exc:
        print(f"Window shape unavailable: {exc}")


def _apply_panel_shape(root, radius):
    from Xlib import X, display
    from Xlib.ext import shape

    root.update_idletasks()
    d = display.Display()
    win = d.create_resource_object("window", root.winfo_id())

    rectangles = []
    for widget in _card_widgets(root):
        x = widget.winfo_rootx() - root.winfo_rootx()
        y = widget.winfo_rooty() - root.winfo_rooty()
        width = widget.winfo_width()
        height = widget.winfo_height()
        rectangles.extend(_rounded_rectangles(x, y, width, height, radius))

    if rectangles:
        win.shape_rectangles(shape.SO.Set, shape.SK.Bounding, X.Unsorted, 0, 0, rectangles)
        win.shape_rectangles(shape.SO.Set, shape.SK.Clip, X.Unsorted, 0, 0, rectangles)
        d.sync()
    d.close()


def _card_widgets(widget):
    widgets = []
    if getattr(widget, "card", False):
        widgets.append(widget)
    for child in widget.winfo_children():
        widgets.extend(_card_widgets(child))
    return widgets


def _rounded_rectangles(x, y, width, height, radius):
    if width <= 0 or height <= 0:
        return []
    radius = int(max(0, min(radius, width // 2, height // 2)))
    if radius <= 1:
        return [(int(x), int(y), int(width), int(height))]

    rectangles = []
    step = 2
    for yy in range(0, height, step):
        band_h = min(step, height - yy)
        center_y = yy + band_h / 2
        inset = 0
        if center_y < radius:
            inset = radius - (radius * radius - (radius - center_y) ** 2) ** 0.5
        elif center_y > height - radius:
            inset = radius - (radius * radius - (center_y - (height - radius)) ** 2) ** 0.5
        inset = int(round(inset))
        rectangles.append((int(x + inset), int(y + yy), int(width - inset * 2), int(band_h)))
    return rectangles
