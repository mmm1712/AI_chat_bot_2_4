#!/usr/bin/env python3
# Mouse bridge using a local window (no system-wide hooks)
# Usage: python3 mouse_bridge_window.py <ESP32_IP>

import socket
import sys

try:
    import pygame
except ImportError:
    print("Missing dependency: pygame")
    print("Install with: pip3 install pygame")
    sys.exit(1)

if len(sys.argv) < 2:
    print("Usage: python3 mouse_bridge_window.py <ESP32_IP>")
    sys.exit(1)

ESP32_IP = sys.argv[1]
ESP32_PORT = 4210

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

WIDTH, HEIGHT = 320, 240
SCALE = 2
WIN_W, WIN_H = WIDTH * SCALE, HEIGHT * SCALE

pygame.init()
screen = pygame.display.set_mode((WIN_W, WIN_H))
pygame.display.set_caption("ESP32 Mouse Bridge (click + drag inside)")
clock = pygame.time.Clock()

pressed = False
wheel_delta = 0

print(f"Sending mouse to {ESP32_IP}:{ESP32_PORT}")
print("Use mouse inside this window. Close window to stop.")

running = True
while running:
    for event in pygame.event.get():
        if event.type == pygame.QUIT:
            running = False
        elif event.type == pygame.MOUSEBUTTONDOWN:
            pressed = True
        elif event.type == pygame.MOUSEBUTTONUP:
            pressed = False
        elif event.type == pygame.MOUSEWHEEL:
            wheel_delta += event.y

    mx, my = pygame.mouse.get_pos()
    x = int(mx / SCALE)
    y = int(my / SCALE)
    if x < 0: x = 0
    if y < 0: y = 0
    if x > 319: x = 319
    if y > 239: y = 239

    msg = f"{x},{y},{1 if pressed else 0},{wheel_delta}".encode("utf-8")
    wheel_delta = 0
    sock.sendto(msg, (ESP32_IP, ESP32_PORT))

    # simple UI
    screen.fill((30, 60, 120))
    pygame.draw.rect(screen, (255, 255, 255), (0, 0, WIN_W-1, WIN_H-1), 1)
    pygame.draw.circle(screen, (255, 255, 255), (mx, my), 4)
    pygame.display.flip()
    clock.tick(60)

pygame.quit()
