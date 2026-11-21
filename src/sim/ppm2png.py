#!/usr/bin/env python3
# ppm2png.py in.ppm out.png [scale]: the ISS frame shot (P6) as a PNG, no PIL needed
import struct, sys, zlib
d = open(sys.argv[1], 'rb').read()
parts = d.split(maxsplit=4)
assert parts[0] == b'P6'
w, h, data = int(parts[1]), int(parts[2]), parts[4]
k = int(sys.argv[3]) if len(sys.argv) > 3 else 1
rows = []
for y in range(h):
    r = data[3 * w * y: 3 * w * (y + 1)]
    r = b''.join(r[3 * x: 3 * x + 3] * k for x in range(w))
    rows += [b'\0' + r] * k
def chunk(t, b): return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b))
png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w * k, h * k, 8, 2, 0, 0, 0)) + \
      chunk(b'IDAT', zlib.compress(b''.join(rows))) + chunk(b'IEND', b'')
open(sys.argv[2], 'wb').write(png)
