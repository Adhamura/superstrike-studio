#!/usr/bin/env python3
"""Plays the promo demo on the running app (inside Xvfb): moves a real pointer and clicks real
widgets through XTEST. Widget positions come from the app itself (SS_SHOT_DUMP, see main.cpp).
Writes "scene <name> <seconds>" lines to the marks file, for the captions in make_promo.sh.
Usage: drive.py DUMP_FILE MARKS_FILE     (needs python-xlib and libXcursor)"""
import ctypes
import sys
import time

from Xlib import X, display
from Xlib.ext import xtest

DUMP, MARKS = sys.argv[1], sys.argv[2]
d = display.Display()
root = d.screen().root
t0 = time.time()
marks = open(MARKS, "w")
pos = [660, 420]


def set_root_cursor():
    """A normal arrow (Adwaita theme) instead of the X server's default cross."""
    x11 = ctypes.CDLL("libX11.so.6")
    xc = ctypes.CDLL("libXcursor.so.1")
    x11.XOpenDisplay.restype = ctypes.c_void_p
    xc.XcursorLibraryLoadCursor.restype = ctypes.c_ulong
    xc.XcursorLibraryLoadCursor.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    x11.XDefaultRootWindow.restype = ctypes.c_ulong
    x11.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
    x11.XDefineCursor.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong]
    x11.XFlush.argtypes = [ctypes.c_void_p]
    dpy = x11.XOpenDisplay(None)
    cur = xc.XcursorLibraryLoadCursor(dpy, b"left_ptr")
    x11.XDefineCursor(dpy, x11.XDefaultRootWindow(dpy), cur)
    x11.XFlush(dpy)
    return dpy  # keep the connection open, or the cursor goes away


def widgets():
    out = []
    for line in open(DUMP, encoding="utf-8"):
        p = line.rstrip("\n").split(" ", 5)
        if len(p) == 6:
            out.append((float(p[0]), float(p[1]), float(p[2]), float(p[3]), p[4], p[5]))
    return out


def find(text, nth=0, prefix=False):
    """Centre of the nth visible widget whose label (or widget name) is text."""
    for _ in range(40):
        hits = [w for w in widgets() if (w[5].startswith(text) if prefix else w[5] == text)]
        if len(hits) > nth:
            x, y, w, h = hits[nth][:4]
            return [x, y, w, h]
        time.sleep(0.15)
    raise SystemExit(f"widget not found: {text}")


def move(x, y, dur=0.45):
    steps = max(8, int(dur * 60))
    sx, sy = pos
    for i in range(1, steps + 1):
        t = i / steps
        e = t * t * (3 - 2 * t)  # smoothstep
        xtest.fake_input(d, X.MotionNotify, x=int(sx + (x - sx) * e), y=int(sy + (y - sy) * e))
        d.sync()
        time.sleep(dur / steps)
    pos[:] = [x, y]


def click(hold=0.07):
    xtest.fake_input(d, X.ButtonPress, 1)
    d.sync()
    time.sleep(hold)
    xtest.fake_input(d, X.ButtonRelease, 1)
    d.sync()


def at(text, fx=0.5, fy=0.5, nth=0, prefix=False, dur=0.45, wait=0.6):
    x, y, w, h = find(text, nth, prefix)
    move(x + w * fx, y + h * fy, dur)
    time.sleep(0.12)
    click()
    time.sleep(wait)


def scene(name):
    marks.write(f"scene {name} {time.time() - t0:.2f}\n")
    marks.flush()


keep = set_root_cursor()
xtest.fake_input(d, X.MotionNotify, x=pos[0], y=pos[1])
d.sync()
time.sleep(1.0)

scene("hits")
at("Rapid")
at("level-bar", fx=0.5 / 5)          # rapid trigger level 1: most sensitive
at("Haptics")
at("level-bar", fx=5.5 / 6)          # haptics 5: strongest
at("Actuation")
at("travel-bar", fx=0.6 / 10, fy=0.53, wait=1.0)  # actuation 1: shortest press
scene("test")
x, y, w, h = find("Test here — click with any button")
move(x + w * 0.5, y + 10)
for _ in range(7):
    click(0.05)
    time.sleep(0.12)
time.sleep(1.0)

scene("save")
at("Save to mouse", wait=2.6)

at("Buttons", wait=0.0)
scene("buttons")  # the caption changes with the page
time.sleep(0.8)
at("Left side", wait=0.9)
at("Back (rear side)", wait=0.6)
at("Keyboard", wait=1.2)

at("Sensor", wait=0.0)
scene("sensor")  # the caption changes with the page
time.sleep(0.8)
at("1600", wait=0.7)
at("8K", wait=1.2)

at("Profiles", wait=0.0)
scene("profiles")  # the caption changes with the page
time.sleep(0.8)
at("Activate", nth=1, wait=2.4)  # profile 2 (the first one is already active)
scene("end")
time.sleep(1.2)
