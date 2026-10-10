#!/usr/bin/env python3
"""Wrap a raw add-in binary into a Casio fx-CG .g3a file.

A Python port of the header logic of mkg3a by Peter Marheine (tari),
https://github.com/tari/mkg3a (zlib licence). The icons are generated:
a small penguin-ish "Linux" badge, so no image files are needed.
"""
import argparse
import os
import struct
import sys
import time

HEADER_SIZE = 0x7000
ICON_W, ICON_H = 92, 64

# offsets inside the header (see mkg3a's g3a.h)
OFF_MAGIC = 0x0000
OFF_CPROT = 0x000E
OFF_CKSUM = 0x0020
OFF_PAD3 = 0x0024
OFF_CKSUM2 = 0x002E
OFF_NAME = 0x0040
OFF_SIZE = 0x005C
OFF_INTERNAL = 0x0060
OFF_LCNAMES = 0x006B
OFF_VERSION = 0x0130
OFF_TIMESTAMP = 0x013C
OFF_LLNAMES = 0x0170
OFF_FILENAME = 0x0EBC
OFF_ICON_UNSEL = 0x1000
OFF_ICON_SEL = 0x4000

MAGIC = bytes([0xAA, 0xAC, 0xBD, 0xAF, 0x90, 0x88, 0x9A,
               0x8D, 0xD3, 0xFF, 0xFE, 0xFF, 0xFE, 0xFF])


def rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


# 5x7 glyphs for the icon caption
GLYPHS = {
    'L': ["10000", "10000", "10000", "10000", "10000", "10000", "11111"],
    'I': ["11111", "00100", "00100", "00100", "00100", "00100", "11111"],
    'N': ["10001", "11001", "10101", "10011", "10001", "10001", "10001"],
    'U': ["10001", "10001", "10001", "10001", "10001", "10001", "01110"],
    'X': ["10001", "10001", "01010", "00100", "01010", "10001", "10001"],
}


def make_icon(selected):
    bg = rgb565(40, 60, 120) if selected else rgb565(250, 250, 250)
    fg = rgb565(255, 255, 255) if selected else rgb565(20, 20, 20)
    px = [[bg] * ICON_W for _ in range(ICON_H)]

    def disc(cx, cy, rx, ry, color):
        for y in range(ICON_H):
            for x in range(ICON_W):
                if ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1:
                    px[y][x] = color

    black, white = rgb565(10, 10, 10), rgb565(245, 245, 245)
    yellow = rgb565(250, 190, 20)
    disc(46, 26, 16, 20, black)          # body
    disc(46, 31, 10, 14, white)          # belly
    disc(40, 15, 3, 3, white)            # eyes
    disc(52, 15, 3, 3, white)
    disc(40, 15, 1.4, 1.6, black)
    disc(52, 15, 1.4, 1.6, black)
    disc(46, 21, 5, 2.5, yellow)         # beak
    disc(37, 46, 6, 2.5, yellow)         # feet
    disc(55, 46, 6, 2.5, yellow)

    text = "LINUX"
    x0 = (ICON_W - (len(text) * 6 - 1) * 1) // 2
    for i, ch in enumerate(text):
        for gy, row in enumerate(GLYPHS[ch]):
            for gx, bit in enumerate(row):
                if bit == '1':
                    px[54 + gy][x0 + i * 6 + gx] = fg
    return b''.join(struct.pack('>H', c) for row in px for c in row)


def put(buf, off, data, maxlen=None):
    if maxlen is not None:
        data = data[:maxlen]
    buf[off:off + len(data)] = data


def build(code, name, internal, version, filename):
    h = bytearray(HEADER_SIZE)
    put(h, OFF_MAGIC, MAGIC)
    h[OFF_PAD3] = 0x01
    h[OFF_PAD3 + 1] = 0x01

    size = len(code) + HEADER_SIZE + 4
    put(h, OFF_CKSUM2, struct.pack('>I', len(code)))
    put(h, OFF_SIZE, struct.pack('>I', size))

    # copy protection bytes, derived from the (big-endian) size
    inv = bytes((~b) & 0xFF for b in struct.pack('>I', size))
    put(h, OFF_CPROT + 2, inv)
    h[OFF_CPROT + 0] = (h[OFF_CPROT + 5] - 0x41) & 0xFF
    h[OFF_CPROT + 1] = 0xFE
    h[OFF_CPROT + 6] = (h[OFF_CPROT + 5] - 0xB8) & 0xFF

    put(h, OFF_NAME, name.encode(), 0x1C - 1)
    put(h, OFF_INTERNAL, ('@' + internal.upper()).encode(), 0xB - 1)
    for i in range(8):
        put(h, OFF_LCNAMES + i * 0x18, name.encode(), 0x18 - 1)
    put(h, OFF_VERSION, version.encode(), 0xC - 1)
    stamp = os.environ.get('SOURCE_DATE_EPOCH')
    t = time.gmtime(int(stamp)) if stamp else time.localtime()
    put(h, OFF_TIMESTAMP, time.strftime('%Y.%m%d.%H%M', t).encode(), 14)
    put(h, OFF_FILENAME, filename.encode(), 0x144 - 1)
    put(h, OFF_ICON_UNSEL, make_icon(False))
    put(h, OFF_ICON_SEL, make_icon(True))

    cksum = (sum(h) + sum(code)) & 0xFFFFFFFF
    put(h, OFF_CKSUM, struct.pack('>I', cksum))
    return bytes(h) + code + struct.pack('>I', cksum)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('input', help='raw binary (objcopy -O binary)')
    ap.add_argument('output', help='.g3a file to write')
    ap.add_argument('--name', default='Linux')
    ap.add_argument('--internal', default=None,
                    help='internal name (default: --name)')
    ap.add_argument('--version', default='01.00.0000')
    args = ap.parse_args()

    code = open(args.input, 'rb').read()
    if len(code) > 0x1000000:
        sys.exit('input larger than 16 MiB')
    out = build(code, args.name, args.internal or args.name, args.version,
                os.path.basename(args.output))
    open(args.output, 'wb').write(out)
    print(f'{args.output}: {len(out)} bytes')


if __name__ == '__main__':
    main()
