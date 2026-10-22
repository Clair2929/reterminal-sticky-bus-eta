#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Clean 48x48 1-bit bus-stop sign, layout faithful to the user's SVG:
solid slanted parallelogram board with white info lines + star (cut out),
post attached under the board's left end, triangular base.
Output format matches icons.h: uint8_t kBUS_STOP[48][6], MSB first.
"""

W = H = 48
g = [[0] * W for _ in range(H)]


def put(x, y, v=1):
    if 0 <= x < W and 0 <= y < H:
        g[y][x] = v


def line(x0, y0, x1, y1, v=1):
    dx = x1 - x0
    dy = y1 - y0
    steps = max(abs(dx), abs(dy), 1)
    for i in range(steps + 1):
        t = i / steps
        put(int(round(x0 + dx * t)), int(round(y0 + dy * t)), v)


def fill_parall(x0, y0, x1, y1, x2, y2, x3, y3):
    """Fill quadrilateral (top-left, top-right, bottom-right, bottom-left)."""
    ymin = min(y0, y1, y2, y3)
    ymax = max(y0, y1, y2, y3)
    for yy in range(ymin, ymax + 1):
        xl = xr = None
        if min(y0, y1) <= yy <= max(y0, y1) and y1 != y0:
            xl = x0 + (x1 - x0) * (yy - y0) / (y1 - y0)
        if min(y2, y3) <= yy <= max(y2, y3) and y3 != y2:
            xr = x2 + (x3 - x2) * (yy - y2) / (y3 - y2)
        if xl is None:
            xl = x3
        if xr is None:
            xr = x2
        for xx in range(int(round(xl)), int(round(xr)) + 1):
            put(xx, yy)


def filled_tri(x0, y0, x1, y1, x2, y2):
    pts = sorted([(x0, y0), (x1, y1), (x2, y2)], key=lambda p: p[1])
    (ax, ay), (bx, by), (cx, cy) = pts
    for yy in range(ay, cy + 1):
        if yy == ay and by == ay:
            xa, xb = ax, bx          # horizontal base row: draw full span
        elif yy <= by:
            xa = ax + (bx - ax) * ((yy - ay) / (by - ay)) if by != ay else ax
            xb = ax + (cx - ax) * ((yy - ay) / (cy - ay)) if cy != ay else ax
        else:
            xa = bx + (cx - bx) * ((yy - by) / (cy - by)) if cy != by else bx
            xb = ax + (cx - ax) * ((yy - ay) / (cy - ay)) if cy != ay else ax
        for xx in range(int(min(xa, xb)), int(max(xa, xb)) + 1):
            put(xx, yy)


# --- solid slanted parallelogram board
# top-left (10,13) top-right (46,5) bottom-right (46,23) bottom-left (10,31)
fill_parall(10, 13, 46, 5, 46, 23, 10, 31)

# --- white info lines cut out (parallel to edges)
line(16, 20, 42, 13, 0)
line(18, 25, 42, 18, 0)

# --- white star ornament cut out (board center)
scx, scy = 28, 16
for dx, dy in ((0, -2), (0, 2), (-2, 0), (2, 0),
               (-1, -1), (1, -1), (-1, 1), (1, 1)):
    put(scx + dx, scy + dy, 0)

# --- post attached under the board's left end (3px wide)
for yy in range(31, 42):
    put(13, yy)
    put(14, yy)
    put(15, yy)

# --- triangular base
filled_tri(9, 42, 19, 42, 14, 47)

# --- output
print("static const uint8_t kBUS_STOP[48][6] = {")
for y in range(H):
    parts = []
    for bx in range(W // 8):
        v = 0
        for bit in range(8):
            if g[y][bx * 8 + bit]:
                v |= 0x80 >> bit
        parts.append(f"0x{v:02X}")
    print("    " + ", ".join(parts) + ",")
print("};")
print()
print("// ASCII preview:")
for y in range(H):
    print("// " + "".join("#" if g[y][x] else "." for x in range(W)))
