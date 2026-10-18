#!/usr/bin/env python3
# Copyright (C) 2026 Neil Rackett
# SPDX-License-Identifier: GPL-3.0-or-later
#
# png2lynx.py - turn a PNG into a full-colour Atari Lynx sprite, as C.
#
#   python3 tools/png2lynx.py IMAGE.png OUT.c NAME
#
# Writes NAME_palette, the 32 bytes cc65's tgi_setpalette() takes (the
# 16 GREEN registers, then the 16 BLUERED ones), and NAME_sprite, a
# literal 4-bit sprite to draw with an SCB_REHV_PAL. Each colour is
# rounded to the Lynx's 4 bits per channel, and the image may use at
# most 16 after that. Standard library only: no Pillow needed to build.

import struct
import sys
import zlib


def read_png(path):
    """(width, height, rows of (r, g, b) tuples) for an 8-bit or
    palette PNG, non-interlaced."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        sys.exit("%s: not a PNG" % path)

    pos, idat, palette = 8, b"", None
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            width, height, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
        elif kind == b"PLTE":
            palette = [tuple(body[i:i + 3]) for i in range(0, len(body), 3)]
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break

    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    if interlace or (ctype != 3 and depth != 8):
        sys.exit("%s: needs to be non-interlaced, 8-bit or palette" % path)

    raw = zlib.decompress(idat)
    stride = (width * channels * depth + 7) // 8
    bpp = max(1, channels * depth // 8)
    rows, prev, pos = [], bytearray(stride), 0
    for _ in range(height):
        kind, line = raw[pos], bytearray(raw[pos + 1:pos + 1 + stride])
        pos += 1 + stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if kind == 1:
                line[i] = (line[i] + a) & 0xFF
            elif kind == 2:
                line[i] = (line[i] + b) & 0xFF
            elif kind == 3:
                line[i] = (line[i] + (a + b) // 2) & 0xFF
            elif kind == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 0xFF
        prev = line

        if ctype == 3:
            per_byte = 8 // depth
            mask = (1 << depth) - 1
            idx = [(line[x // per_byte] >> (8 - depth * (x % per_byte + 1))) & mask
                   for x in range(width)]
            rows.append([palette[i] for i in idx])
        elif ctype in (0, 4):
            rows.append([(line[x * channels],) * 3 for x in range(width)])
        else:
            rows.append([tuple(line[x * channels:x * channels + 3]) for x in range(width)])
    return width, height, rows


def to_lynx(rgb):
    """An 8-bit colour as the Lynx's 4 bits per channel."""
    return tuple((v * 15 + 127) // 255 for v in rgb)


def main():
    if len(sys.argv) != 4:
        sys.exit("usage: png2lynx.py IMAGE.png OUT.c NAME")
    path, out, name = sys.argv[1:]
    width, height, rows = read_png(path)
    if width > 160 or height > 102:
        sys.exit("%s: %dx%d is bigger than the Lynx's 160x102" % (path, width, height))

    # Pens in order of how much of the picture they cover.
    counts = {}
    for row in rows:
        for rgb in row:
            c = to_lynx(rgb)
            counts[c] = counts.get(c, 0) + 1
    colours = sorted(counts, key=lambda c: -counts[c])
    if len(colours) > 16:
        sys.exit("%s: %d colours at 4 bits per channel; the Lynx has 16"
                 % (path, len(colours)))
    pens = {c: i for i, c in enumerate(colours)}
    colours += [(0, 0, 0)] * (16 - len(colours))

    # A literal sprite line is its own length (this byte included), then
    # the pixels, two to a byte with the left one in the high nibble. A
    # byte of padding keeps the last real pixel clear of Suzy's
    # end-of-line handling, which can drop it; the padding lands off the
    # right-hand edge of the screen. A zero length ends the sprite.
    sprite = bytearray()
    for row in rows:
        line = bytearray()
        for x in range(0, width, 2):
            left = pens[to_lynx(row[x])]
            right = pens[to_lynx(row[x + 1])] if x + 1 < width else 0
            line.append(left << 4 | right)
        line.append(0)
        sprite.append(len(line) + 1)
        sprite += line
    sprite.append(0)

    green = [c[1] for c in colours]
    bluered = [c[2] << 4 | c[0] for c in colours]

    def c_bytes(values):
        return ",\n".join("    " + ", ".join("0x%02X" % v for v in values[i:i + 16])
                          for i in range(0, len(values), 16))

    with open(out, "w") as f:
        f.write("/*\n"
                " * Copyright (C) 2026 Neil Rackett\n"
                " * SPDX-License-Identifier: GPL-3.0-or-later\n"
                " *\n"
                " * Generated from %s by tools/png2lynx.py: do not edit.\n"
                " */\n\n" % path.split("/")[-1])
        f.write("const unsigned char %s_palette[32] = {\n%s};\n\n"
                % (name, c_bytes(green + bluered)))
        f.write("unsigned char %s_sprite[%d] = {\n%s};\n"
                % (name, len(sprite), c_bytes(list(sprite))))


if __name__ == "__main__":
    main()
