"""Show the radio's USB joystick as Windows sees it: axes as bars, buttons as boxes.

Windows only. Uses the joystick API in winmm.dll through ctypes and tkinter
for the window, so it needs no extra packages:

    .venv/Scripts/python tools/joystick_test.py

winmm reports six axes and the buttons. Observed with firmware 0.7.0, it
shows the radio's axes X, Y, Z, Rz, slider and Rx, which are channels 1, 2,
3, 6, 7 and 4. Channels 5 (Ry) and 8 (dial) are not visible here, but are
in programs that use DirectInput, such as simulators.
"""
import ctypes
import sys
import tkinter as tk
from ctypes import wintypes

NAME = "LiteRadio"
JOY_RETURNALL = 0xFF
# In the order winmm returns them: X, Y, Z, R, U, V.
AXES = ["Kanal 1", "Kanal 2", "Kanal 3", "Kanal 6", "Kanal 7", "Kanal 4"]
BUTTONS = 8   # channels 9 to 16


class JOYCAPSW(ctypes.Structure):
    _fields_ = [("wMid", wintypes.WORD), ("wPid", wintypes.WORD), ("szPname", wintypes.WCHAR * 32),
                ("wXmin", wintypes.UINT), ("wXmax", wintypes.UINT), ("wYmin", wintypes.UINT), ("wYmax", wintypes.UINT),
                ("wZmin", wintypes.UINT), ("wZmax", wintypes.UINT), ("wNumButtons", wintypes.UINT),
                ("wPeriodMin", wintypes.UINT), ("wPeriodMax", wintypes.UINT),
                ("wRmin", wintypes.UINT), ("wRmax", wintypes.UINT), ("wUmin", wintypes.UINT), ("wUmax", wintypes.UINT),
                ("wVmin", wintypes.UINT), ("wVmax", wintypes.UINT), ("wCaps", wintypes.UINT), ("wMaxAxes", wintypes.UINT),
                ("wNumAxes", wintypes.UINT), ("wMaxButtons", wintypes.UINT),
                ("szRegKey", wintypes.WCHAR * 32), ("szOEMVxD", wintypes.WCHAR * 260)]


class JOYINFOEX(ctypes.Structure):
    _fields_ = [("dwSize", wintypes.DWORD), ("dwFlags", wintypes.DWORD),
                ("dwXpos", wintypes.DWORD), ("dwYpos", wintypes.DWORD), ("dwZpos", wintypes.DWORD),
                ("dwRpos", wintypes.DWORD), ("dwUpos", wintypes.DWORD), ("dwVpos", wintypes.DWORD),
                ("dwButtons", wintypes.DWORD), ("dwButtonNumber", wintypes.DWORD), ("dwPOV", wintypes.DWORD),
                ("dwReserved1", wintypes.DWORD), ("dwReserved2", wintypes.DWORD)]


winmm = ctypes.WinDLL("winmm")


def find_joystick():
    """Return (id, caps) of the first joystick whose name contains NAME, or the first one at all."""
    found = []
    for joy_id in range(winmm.joyGetNumDevs()):
        caps = JOYCAPSW()
        info = JOYINFOEX(dwSize=ctypes.sizeof(JOYINFOEX), dwFlags=JOY_RETURNALL)
        if winmm.joyGetDevCapsW(joy_id, ctypes.byref(caps), ctypes.sizeof(caps)) != 0:
            continue
        if winmm.joyGetPosEx(joy_id, ctypes.byref(info)) != 0:
            continue
        found.append((joy_id, caps))
    for joy_id, caps in found:
        if NAME.lower() in caps.szPname.lower():
            return joy_id, caps
    return found[0] if found else (None, None)


def main():
    joy_id, caps = find_joystick()
    if joy_id is None:
        sys.exit("No joystick found. Is the radio plugged in with firmware 0.7.0 or later?")
    ranges = [(caps.wXmin, caps.wXmax), (caps.wYmin, caps.wYmax), (caps.wZmin, caps.wZmax),
              (caps.wRmin, caps.wRmax), (caps.wUmin, caps.wUmax), (caps.wVmin, caps.wVmax)]

    root = tk.Tk()
    root.title(f"Joystick-Test: {caps.szPname}")
    root.configure(bg="#0e0f11")
    canvas = tk.Canvas(root, width=520, height=60 + 40 * len(AXES) + 70, bg="#0e0f11", highlightthickness=0)
    canvas.pack(padx=16, pady=16)
    canvas.create_text(0, 10, anchor="w", fill="#8b9099", font=("Segoe UI", 10),
                       text=f"{caps.szPname}, {caps.wNumAxes} Achsen, {caps.wNumButtons} Tasten (Windows-Joystick {joy_id})")
    bars, labels = [], []
    for i, name in enumerate(AXES):
        y = 50 + 40 * i
        canvas.create_text(0, y, anchor="w", fill="#f2f3f5", font=("Segoe UI", 10), text=name)
        canvas.create_rectangle(120, y - 6, 460, y + 6, fill="#2b2e34", outline="")
        bars.append(canvas.create_rectangle(120, y - 6, 120, y + 6, fill="#5aa2ff", outline=""))
        labels.append(canvas.create_text(520, y, anchor="e", fill="#f2f3f5", font=("Segoe UI", 10), text=""))
    top = 50 + 40 * len(AXES) + 10
    canvas.create_text(0, top, anchor="w", fill="#f2f3f5", font=("Segoe UI", 10), text="Tasten (Kanal 9 bis 16)")
    boxes = []
    for b in range(BUTTONS):
        x = 120 + b * 42
        boxes.append(canvas.create_rectangle(x, top + 14, x + 34, top + 44, fill="#202227", outline=""))
        canvas.create_text(x + 17, top + 29, fill="#8b9099", font=("Segoe UI", 9), text=str(9 + b))

    def update():
        info = JOYINFOEX(dwSize=ctypes.sizeof(JOYINFOEX), dwFlags=JOY_RETURNALL)
        if winmm.joyGetPosEx(joy_id, ctypes.byref(info)) == 0:
            values = [info.dwXpos, info.dwYpos, info.dwZpos, info.dwRpos, info.dwUpos, info.dwVpos]
            for i, (value, (low, high)) in enumerate(zip(values, ranges)):
                part = (value - low) / (high - low) if high > low else 0
                canvas.coords(bars[i], 120, 50 + 40 * i - 6, 120 + 340 * part, 50 + 40 * i + 6)
                canvas.itemconfigure(labels[i], text=f"{part * 100:5.1f} %")
            for b, box in enumerate(boxes):
                canvas.itemconfigure(box, fill="#5aa2ff" if info.dwButtons & (1 << b) else "#202227")
        else:
            canvas.itemconfigure(labels[0], text="getrennt")
        root.after(20, update)

    update()
    root.mainloop()


if __name__ == "__main__":
    main()
