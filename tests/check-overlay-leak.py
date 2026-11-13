"""Scans every frame of a recording for AutoFrame debug graphics.

The stats panel title "V MEDIA AI AUTOFRAME" and LOCK labels use saturated colours
(yellow 255,196,0 / green 0,230,118) that do not occur in normal footage near the top of
the frame. A frame "leaks" when the top-left region contains a block of those pixels.

usage: python check-overlay-leak.py <video.mp4>
exit code 0 = no frame contains tracking graphics
"""
import subprocess
import sys

import numpy as np


def probe(path):
    out = subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'v:0', '-show_entries',
                          'stream=width,height,r_frame_rate', '-of', 'csv=p=0', path],
                         capture_output=True, text=True, check=True).stdout.strip().split(',')
    num, den = out[2].split('/')
    return int(out[0]), int(out[1]), float(num) / float(den)


def main(path):
    w, h, fps = probe(path)
    rw, rh = int(w * 0.75), int(h * 0.15)  # top-left area: stats panel + upper box labels
    cmd = ['ffmpeg', '-v', 'error', '-i', path, '-vf', f'crop={rw}:{rh}:0:0', '-f', 'rawvideo',
           '-pix_fmt', 'rgb24', '-']
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    size = rw * rh * 3
    leaks, n = [], 0
    while True:
        buf = proc.stdout.read(size)
        if len(buf) < size:
            break
        f = np.frombuffer(buf, np.uint8).reshape(rh, rw, 3).astype(np.int16)
        r, g, b = f[..., 0], f[..., 1], f[..., 2]
        yellow = (r > 215) & (g > 150) & (g < 225) & (b < 80)
        green = (r < 80) & (g > 190) & (b > 70) & (b < 170)
        count = int(yellow.sum() + green.sum())
        if count > 150:
            leaks.append((n, count))
        n += 1
    proc.wait()
    print(f'{path}: {w}x{h}, {n} frames scanned')
    if leaks:
        first, last = leaks[0][0], leaks[-1][0]
        print(f'LEAK: tracking graphics in {len(leaks)} frames '
              f'(frames {first}-{last} = {first / fps:.2f}s-{last / fps:.2f}s)')
        return 1
    print('CLEAN: no frame contains tracking graphics')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1]))
