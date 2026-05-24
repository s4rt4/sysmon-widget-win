_WARNED = False


def apply_desktop_hints(wid: int, desktop_type: bool = False, splash_type: bool = True):
    global _WARNED
    try:
        set_window_hints(wid, desktop_type=desktop_type, splash_type=splash_type)
    except Exception as exc:
        if not _WARNED:
            print(f"Xlib hints unavailable: {exc}")
            _WARNED = True


def set_window_hints(wid: int, desktop_type: bool = False, splash_type: bool = True):
    from Xlib import X, display
    from Xlib.protocol import event
    from Xlib.Xatom import ATOM, CARDINAL

    d = display.Display()
    root = d.screen().root
    win = d.create_resource_object("window", wid)

    def atom(name: str):
        return d.intern_atom(name)

    if desktop_type or splash_type:
        window_type = "_NET_WM_WINDOW_TYPE_DESKTOP" if desktop_type else "_NET_WM_WINDOW_TYPE_SPLASH"
        win.change_property(
            atom("_NET_WM_WINDOW_TYPE"),
            ATOM,
            32,
            [atom(window_type)],
        )

    win.change_property(
        atom("_NET_WM_STATE"),
        ATOM,
        32,
        [atom("_NET_WM_STATE_STICKY"), atom("_NET_WM_STATE_SKIP_TASKBAR")],
    )
    win.change_property(atom("_NET_WM_DESKTOP"), CARDINAL, 32, [0xFFFFFFFF])
    for state in ("_NET_WM_STATE_STICKY", "_NET_WM_STATE_SKIP_TASKBAR"):
        message = event.ClientMessage(
            window=win,
            client_type=atom("_NET_WM_STATE"),
            data=(32, [1, atom(state), 0, 1, 0]),
        )
        root.send_event(message, event_mask=X.SubstructureRedirectMask | X.SubstructureNotifyMask)

    d.sync()
    d.close()
