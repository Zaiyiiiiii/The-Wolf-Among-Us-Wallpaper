import struct, sys, io, os
from PIL import Image

# 用法: d3dtx2png.py <d3dtx> <宽> <高> <BC1|BC3> <输出png>
src, w, h, fmt, out = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4], sys.argv[5]
data = open(src, 'rb').read()
size = w * h // 2 if fmt == 'BC1' else w * h
pix = data[-size:]
fourcc = b'DXT1' if fmt == 'BC1' else b'DXT5'
hdr = b'DDS ' + struct.pack('<7I', 124, 0x81007, h, w, size, 0, 1) + b'\0' * 44
hdr += struct.pack('<2I4s5I', 32, 4, fourcc, 0, 0, 0, 0, 0) + struct.pack('<5I', 0x1000, 0, 0, 0, 0)
img = Image.open(io.BytesIO(hdr + pix))
img.convert('RGBA').save(out)
print(out, img.size, img.mode)
