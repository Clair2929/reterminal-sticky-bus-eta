#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ASCII preview of the generated loading bitmap, to sanity-check the shape."""
import math

W = H = 48
CX = CY = 24.0
R1, R2, HALF_W, N = 12.0, 20.0, 0.45, 20
step = 2 * math.pi / N
for y in range(H):
    line = ""
    for x in range(W):
        dx = (x + 0.5) - CX
        dy = (y + 0.5) - CY
        r = math.hypot(dx, dy)
        on = False
        if R1 <= r <= R2:
            ang = math.atan2(dy, dx)
            nearest = round(ang / step)
            da = abs(ang - nearest * step)
            if da > math.pi:
                da = 2 * math.pi - da
            if r * da <= HALF_W:
                on = True
        line += "#" if on else "."
    print(line)
