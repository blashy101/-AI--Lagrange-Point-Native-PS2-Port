"""Encode cover artwork as a double-sided, textured PS2 browser plaque.

Texture words use the GS native PSMCT16 layout, as used by HDLGameInstaller's
icon renderer. Coordinates, normals and UVs use signed 4.12 fixed point.
"""
from pathlib import Path
from struct import pack

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


def build():
    image = Image.open(ROOT / 'assets/save-cover.png').convert('RGB')
    if image.size != (128, 128):
        raise ValueError('The memory card texture must be 128 by 128 pixels.')
    vertices = []

    def face(points, normal):
        for i in (0, 1, 2, 0, 2, 3):
            x, y, z, u, v = points[i]
            # Browser coordinates are Y-down, with the front facing negative Z.
            values = [round(n * 4096) for n in
                      (x, -y, -z, 0, normal[0], -normal[1], -normal[2], 0, u, v)]
            vertices.append(pack('<4h4h2h4B', *values, 128, 128, 128, 128))

    extent, depth = 1.5, .055
    # Each face shows readable artwork when the browser rotates the plaque.
    face([(-extent, extent, depth, 0, 0), (-extent, -extent, depth, 0, 1),
          (extent, -extent, depth, 1, 1), (extent, extent, depth, 1, 0)], (0, 0, 1))
    face([(extent, extent, -depth, 0, 0), (extent, -extent, -depth, 0, 1),
          (-extent, -extent, -depth, 1, 1), (-extent, extent, -depth, 1, 0)], (0, 0, -1))
    # A narrow blue rim samples one dark texel rather than stretching the logo.
    for a, b, normal in [
        ((-extent, extent), (extent, extent), (0, 1, 0)),
        ((extent, -extent), (-extent, -extent), (0, -1, 0)),
        ((extent, extent), (extent, -extent), (1, 0, 0)),
        ((-extent, -extent), (-extent, extent), (-1, 0, 0)),
    ]:
        face([(a[0], a[1], -depth, 0, 0), (a[0], a[1], depth, 0, 0),
              (b[0], b[1], depth, 0, 0), (b[0], b[1], -depth, 0, 0)], normal)
    model = pack('<IIIfI', 0x10000, 1, 7, 1., len(vertices)) + b''.join(vertices)
    model += pack('<IIfII', 1, 1, 1., 0, 1) + pack('<IIff', 0, 1, 0., 1.)
    pixels = image.tobytes()
    texture = b''.join(pack('<H', (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) | 0x8000)
                       for r, g, b in zip(pixels[0::3], pixels[1::3], pixels[2::3]))
    (ROOT / 'assets/save.icn').write_bytes(model + texture)
    print(f'Generated textured browser icon: {len(vertices)} vertices, {len(model + texture)} bytes')


if __name__ == '__main__':
    build()
