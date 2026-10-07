"""Development helper: render PDF page without Pillow (pypdfium2 required)."""
import struct
import sys
import zlib
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / '.lab1-tools'))
import pypdfium2 as pdf

root = Path(__file__).resolve().parents[1]
doc = pdf.PdfDocument(root / 'docs/SDK1.1M.pdf')
for page in (1, 2):
    bmp = doc[page].render(scale=1.7, force_bitmap_format=pdf.raw.FPDFBitmap_BGR)
    data = bytes(bmp.buffer)
    rows = []
    for y in range(bmp.height):
        row = data[y * bmp.stride:y * bmp.stride + bmp.width * 3]
        rgb = bytearray(len(row))
        rgb[0::3], rgb[1::3], rgb[2::3] = row[2::3], row[1::3], row[0::3]
        rows.append(b'\0' + rgb)
    def chunk(kind, payload):
        return struct.pack('>I', len(payload)) + kind + payload + struct.pack('>I', zlib.crc32(kind + payload))
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', bmp.width, bmp.height, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(b''.join(rows))) + chunk(b'IEND', b'')
    out = root / 'build' / f'schematic-page-{page+1}.png'
    out.parent.mkdir(exist_ok=True)
    out.write_bytes(png)
    print(out)
