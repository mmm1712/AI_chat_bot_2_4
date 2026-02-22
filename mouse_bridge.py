#!/usr/bin/env python3
# Simple mouse-to-ESP32 bridge over UDP
# Usage: python3 mouse_bridge.py <ESP32_IP>
# Example: python3 mouse_bridge.py 192.168.1.42

import socket
import sys
import time

try:
    from pynput import mouse
except ImportError:
    print("Missing dependency: pynput")
    print("Install with: pip3 install pynput")
    sys.exit(1)

if len(sys.argv) < 2:
    print("Usage: python3 mouse_bridge.py <ESP32_IP>")
    sys.exit(1)

ESP32_IP = sys.argv[1]
ESP32_PORT = 4210

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

# Screen size from OS (approx) - use pynput for current position only.
# We'll map full desktop to 320x240.
try:
    import tkinter as tk
    _root = tk.Tk()
    _root.withdraw()
    SCREEN_W = _root.winfo_screenwidth()
    SCREEN_H = _root.winfo_screenheight()
    _root.destroy()
except Exception:
    SCREEN_W = 1920
    SCREEN_H = 1080

state = {
    "x": 0,
    "y": 0,
    "pressed": 0,
}


def clamp(v, lo, hi):
    return lo if v < lo else hi if v > hi else v


def send_state():
    x = int(state["x"] * 320 / max(1, SCREEN_W - 1))
    y = int(state["y"] * 240 / max(1, SCREEN_H - 1))
    x = clamp(x, 0, 319)
    y = clamp(y, 0, 239)
    p = 1 if state["pressed"] else 0
    msg = f"{x},{y},{p}".encode("utf-8")
    sock.sendto(msg, (ESP32_IP, ESP32_PORT))


def on_move(x, y):
    state["x"] = x
    state["y"] = y
    send_state()


def on_click(x, y, button, pressed):
    state["x"] = x
    state["y"] = y
    state["pressed"] = 1 if pressed else 0
    send_state()


print(f"Sending mouse to {ESP32_IP}:{ESP32_PORT} (screen {SCREEN_W}x{SCREEN_H})")
print("Press Ctrl+C to stop")

with mouse.Listener(on_move=on_move, on_click=on_click) as listener:
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        pass

