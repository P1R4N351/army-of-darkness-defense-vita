#!/usr/bin/env python3
"""Convert ux0:data/aodd/diag/frame_*.rgba dumps (DIAGNOSTIC build) to PNG.
Header: 6 x uint32 LE = 'DIAG', version, width, height, pitch(px), format(0=A8B8G8R8); then pitch*height*4 bytes."""
import struct, sys, zlib
for path in sys.argv[1:]:
    d = open(path, 'rb').read()
    magic, ver, w, h, pitch, fmt = struct.unpack_from('<6I', d, 0)
    if magic != 0x47414944 or fmt != 0:
        print(f'{path}: not a DIAG A8B8G8R8 dump'); continue
    if not (0 < w <= pitch) or h <= 0 or len(d) != 24 + pitch * h * 4:
        print(f'{path}: bad geometry or truncated ({w}x{h} pitch {pitch}, {len(d)} bytes)'); continue
    px = d[24:]
    rows = []
    nonblack = 0
    for y in range(h):
        row = px[y * pitch * 4: y * pitch * 4 + w * 4]
        out = bytearray()
        for x in range(w):
            r, g, b = row[4 * x], row[4 * x + 1], row[4 * x + 2]
            if r > 8 or g > 8 or b > 8: nonblack += 1
            out += bytes((r, g, b))
        rows.append(b'\0' + bytes(out))
    chunk = lambda t, b: struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b))
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(b''.join(rows))) + chunk(b'IEND', b'')
    open(path + '.png', 'wb').write(png)
    print(f'{path}: {w}x{h} nonblack {nonblack}/{w*h} -> {path}.png')
