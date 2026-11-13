"""Builds docs/guide/AutoFrame-Quick-Guide.html (self-contained, images inlined) from
guide.template.html and the JPEGs in the given image folder.

usage: python build-guide.py [image dir]   (default: docs/guide/images)
"""
import base64
import pathlib
import sys

here = pathlib.Path(__file__).parent
img = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else here / "images"
html = (here / 'guide.template.html').read_text(encoding='utf-8')
for key, name in {'WIDE': 'wide_cam.jpg', 'FILTERS': 'filters.jpg', 'COMPARE': 'compare.jpg',
                  'VPREACHER': 'vert_preacher.jpg', 'VBAND': 'vert_band.jpg'}.items():
    data = base64.b64encode((img / name).read_bytes()).decode('ascii')
    html = html.replace('{{' + key + '}}', 'data:image/jpeg;base64,' + data)
out = here / 'AutoFrame-Quick-Guide.html'
out.write_text(html, encoding='utf-8')
print(out, len(html) // 1024, 'KB')
