#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Rasterize bus_stop.svg (1024x1024) into a 48x48 1-bit bitmap C array.
Format matches icons.h: uint8_t kBUS_STOP[48][6], MSB first, per row.
Each SVG path is filled independently (nonzero winding); a sample point is
black if it lies inside ANY path (layered paint semantics, no hole carving).
"""

import math
import re

SVG = open('tools/bus_stop.svg', encoding='utf-8').read()

# ---------- SVG path tokenizer ----------
def tokenize(d):
    return re.findall(r'[MmLlHhVvCcSsQqTtAaZz]|-?\d+\.?\d*(?:[eE][+-]?\d+)?', d)

def parse_d(d):
    """Return list of subpaths; each subpath = list of (x,y) points (closed)."""
    toks = tokenize(d)
    subpaths = []
    cur = None          # current subpath point list
    cur_pt = [0.0, 0.0]
    start_pt = [0.0, 0.0]
    last_ctrl = None    # (x, y, kind) kind in {'c','s','q','t'}
    i = 0

    def num():
        nonlocal i
        v = float(toks[i]); i += 1
        return v

    def rel(pt, v, is_rel):
        return pt + v if is_rel else v

    def close_subpath():
        nonlocal cur
        if cur is not None and len(cur) >= 2:
            if cur[0] != cur[-1]:
                cur.append(cur[0])
            subpaths.append(cur)
        cur = None

    while i < len(toks):
        cmd = toks[i]; i += 1
        if cmd == 'Z' or cmd == 'z':
            close_subpath()
            cur_pt = list(start_pt)
            last_ctrl = None
            continue
        rel = cmd.islower() and cmd != 'z'
        u = cmd.upper()
        if u == 'M':
            if cur is not None and len(cur) >= 2:
                close_subpath()
            cur = [tuple(cur_pt)]
            while i < len(toks) and not toks[i][0].isalpha():
                x = num(); y = num()
                if rel:
                    x += cur_pt[0]; y += cur_pt[1]
                cur.append((x, y))
                cur_pt = [x, y]
                start_pt = [x, y]
                rel = False
            last_ctrl = None
        elif u == 'L':
            while i < len(toks) and not toks[i][0].isalpha():
                x = num(); y = num()
                if rel:
                    x += cur_pt[0]; y += cur_pt[1]
                cur.append((x, y))
                cur_pt = [x, y]
                last_ctrl = None
        elif u == 'H':
            while i < len(toks) and not toks[i][0].isalpha():
                x = num()
                if rel:
                    x += cur_pt[0]
                cur.append((x, cur_pt[1]))
                cur_pt = [x, cur_pt[1]]
                last_ctrl = None
        elif u == 'V':
            while i < len(toks) and not toks[i][0].isalpha():
                y = num()
                if rel:
                    y += cur_pt[1]
                cur.append((cur_pt[0], y))
                cur_pt = [cur_pt[0], y]
                last_ctrl = None
        elif u == 'C':
            while i < len(toks) and not toks[i][0].isalpha():
                x1 = num(); y1 = num(); x2 = num(); y2 = num(); x = num(); y = num()
                if rel:
                    x1 += cur_pt[0]; y1 += cur_pt[1]
                    x2 += cur_pt[0]; y2 += cur_pt[1]
                    x += cur_pt[0]; y += cur_pt[1]
                for t in range(1, 33):
                    s = t / 32.0
                    a = (1 - s) ** 3; b = 3 * (1 - s) ** 2 * s; c = 3 * (1 - s) * s ** 2; d = s ** 3
                    px = a * cur_pt[0] + b * x1 + c * x2 + d * x
                    py = a * cur_pt[1] + b * y1 + c * y2 + d * y
                    cur.append((px, py))
                cur_pt = [x, y]
                last_ctrl = (x2, y2, 'c')
        elif u == 'S':
            while i < len(toks) and not toks[i][0].isalpha():
                x2 = num(); y2 = num(); x = num(); y = num()
                if rel:
                    x2 += cur_pt[0]; y2 += cur_pt[1]
                    x += cur_pt[0]; y += cur_pt[1]
                if last_ctrl and last_ctrl[2] == 'c':
                    x1 = 2 * cur_pt[0] - last_ctrl[0]
                    y1 = 2 * cur_pt[1] - last_ctrl[1]
                else:
                    x1, y1 = cur_pt
                for t in range(1, 33):
                    s = t / 32.0
                    a = (1 - s) ** 3; b = 3 * (1 - s) ** 2 * s; c = 3 * (1 - s) * s ** 2; d = s ** 3
                    px = a * cur_pt[0] + b * x1 + c * x2 + d * x
                    py = a * cur_pt[1] + b * y1 + c * y2 + d * y
                    cur.append((px, py))
                cur_pt = [x, y]
                last_ctrl = (x2, y2, 'c')
        elif u == 'Q':
            while i < len(toks) and not toks[i][0].isalpha():
                x1 = num(); y1 = num(); x = num(); y = num()
                if rel:
                    x1 += cur_pt[0]; y1 += cur_pt[1]
                    x += cur_pt[0]; y += cur_pt[1]
                for t in range(1, 17):
                    s = t / 16.0
                    a = (1 - s) ** 2; b = 2 * (1 - s) * s; c = s ** 2
                    px = a * cur_pt[0] + b * x1 + c * x
                    py = a * cur_pt[1] + b * y1 + c * y
                    cur.append((px, py))
                cur_pt = [x, y]
                last_ctrl = (x1, y1, 'q')
        elif u == 'T':
            while i < len(toks) and not toks[i][0].isalpha():
                x = num(); y = num()
                if rel:
                    x += cur_pt[0]; y += cur_pt[1]
                if last_ctrl and last_ctrl[2] == 'q':
                    x1 = 2 * cur_pt[0] - last_ctrl[0]
                    y1 = 2 * cur_pt[1] - last_ctrl[1]
                else:
                    x1, y1 = cur_pt
                for t in range(1, 17):
                    s = t / 16.0
                    a = (1 - s) ** 2; b = 2 * (1 - s) * s; c = s ** 2
                    px = a * cur_pt[0] + b * x1 + c * x
                    py = a * cur_pt[1] + b * y1 + c * y
                    cur.append((px, py))
                cur_pt = [x, y]
                last_ctrl = (x1, y1, 'q')
        elif u == 'A':
            while i < len(toks) and not toks[i][0].isalpha():
                rx = num(); ry = num(); phi = num(); laf = num(); sf = num()
                x = num(); y = num()
                x1, y1 = cur_pt
                if rel:
                    x += x1; y += y1
                # SVG arc -> points (F.6 implementation)
                rx = abs(rx); ry = abs(ry)
                ph = math.radians(phi)
                cph, sph = math.cos(ph), math.sin(ph)
                dx = (x1 - x) / 2.0; dy = (y1 - y) / 2.0
                x1p = cph * dx + sph * dy
                y1p = -sph * dx + cph * dy
                lam = (x1p ** 2) / (rx ** 2) + (y1p ** 2) / (ry ** 2)
                if lam > 1:
                    rx *= math.sqrt(lam); ry *= math.sqrt(lam)
                num_ = rx ** 2 * y1p ** 2 + ry ** 2 * x1p ** 2
                den_ = rx ** 2 * ry ** 2
                rad = 0.0 if num_ <= den_ else math.sqrt(max(0.0, (den_ - num_) / num_))
                sgn = -1.0 if (laf == sf) else 1.0
                cxp = sgn * rad * rx * y1p / ry
                cyp = sgn * rad * ry * x1p / rx
                cx = cph * cxp - sph * cyp + (x1 + x) / 2.0
                cy = sph * cxp + cph * cyp + (y1 + y) / 2.0
                def ang(ux, uy, vx, vy):
                    d = (ux * vx + uy * vy) / max(1e-12, math.hypot(ux, uy) * math.hypot(vx, vy))
                    d = max(-1.0, min(1.0, d))
                    a = math.acos(d)
                    if ux * vy - uy * vx < 0:
                        a = -a
                    return a
                ux = (x1p - cxp) / rx; uy = (y1p - cyp) / ry
                vx = (-x1p - cxp) / rx; vy = (-y1p - cyp) / ry
                t1 = ang(1.0, 0.0, ux, uy)
                dt = ang(ux, uy, vx, vy)
                if sf == 0 and dt > 0:
                    dt -= 2 * math.pi
                elif sf == 1 and dt < 0:
                    dt += 2 * math.pi
                steps = max(4, int(abs(dt) / 0.1))
                for k in range(1, steps + 1):
                    t = t1 + dt * k / steps
                    ex = rx * math.cos(t); ey = ry * math.sin(t)
                    px = cx + cph * ex - sph * ey
                    py = cy + sph * ex + cph * ey
                    cur.append((px, py))
                cur_pt = [x, y]
                last_ctrl = None
    close_subpath()
    return subpaths

# ---------- build all subpaths ----------
paths = []
for m in re.finditer(r'<path\s+d="([^"]+)"', SVG):
    for sp in parse_d(m.group(1)):
        if len(sp) >= 3:
            paths.append(sp)

print(f"parsed subpaths: {len(paths)}", file=__import__('sys').stderr)

# ---------- nonzero winding point-in-polygon ----------
def winding(poly, x, y):
    w = 0
    for i in range(len(poly) - 1):
        xa, ya = poly[i]
        xb, yb = poly[i + 1]
        if ya <= y:
            if yb > y and (xb - xa) * (y - ya) - (yb - ya) * (x - xa) > 0:
                w += 1
        else:
            if yb <= y and (xb - xa) * (y - ya) - (yb - ya) * (x - xa) < 0:
                w -= 1
    return w

# per-subpath bbox for fast rejection
bboxes = []
for poly in paths:
    xs = [p[0] for p in poly]; ys = [p[1] for p in poly]
    bboxes.append((min(xs), min(ys), max(xs), max(ys)))

S = 4                      # supersample factor
W = H = 48
out = [[0] * W for _ in range(H)]
for py in range(H):
    for px in range(W):
        hits = 0
        for sy in range(S):
            for sx in range(S):
                x = (px + (sx + 0.5) / S) * 1024.0 / W
                y = (py + (sy + 0.5) / S) * 1024.0 / H
                inside = False
                for idx, poly in enumerate(paths):
                    x0, y0, x1, y1 = bboxes[idx]
                    if x < x0 or x > x1 or y < y0 or y > y1:
                        continue
                    if winding(poly, x, y) != 0:
                        inside = True
                        break
                if inside:
                    hits += 1
        if hits >= S * S // 2:
            out[py][px] = 1

print("static const uint8_t kBUS_STOP[48][6] = {")
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
