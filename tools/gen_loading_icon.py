#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Generate a 48x48 1-bit loading-spinner bitmap C array from the user's SVG
geometry (20 spokes, inner radius ~256/1024, outer ~427/1024, spoke width 17).
Output format matches icons.h: uint8_t kLOADING[H][W/8], MSB first, per row.
"""
import math

W = H = 48
CX = CY = 24.0
R1 = 12.0   # inner radius in pixels  (256 / (1024/48))
R2 = 20.0   # outer radius in pixels  (427 / (1024/48))
HALF_W = 0.45  # half spoke width in pixels (8.5 / (1024/48))
N = 20

out = [[0] * W for _ in range(H)]
step = 2 * math.pi / N
for y in range(H):
    for x in range(W):
        dx = (x + 0.5) - CX
        dy = (y + 0.5) - CY
        r = math.hypot(dx, dy)
        if r < R1 or r > R2:
            continue
        ang = math.atan2(dy, dx)
        nearest = round(ang / step)
        da = abs(ang - nearest * step)
        if da > math.pi:
            da = 2 * math.pi - da
        if r * da <= HALF_W:
            out[y][x] = 1

print(f"static const uint8_t kLOADING[{H}][{W // 8}] = {{")
for y in range(H):
    parts = []
    for bx in range(W // 8):
        v = 0
        for bit in range(8):
            if out[y][bx * 8 + bit]:
                v |= 0x80 >> bit
        parts.append(f"0x{v:02X}")
    print("    " + ", ".join(parts) + ",")
print("};")
