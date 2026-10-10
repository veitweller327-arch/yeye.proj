"""Renders preview PNGs of the ghost effect (needs Pillow + numpy).
Usage: python tools/make_preview.py input.png output_prefix
Each look below = (name, amount, distance, direction, distortion, offset)."""
import subprocess, sys, os, tempfile
import numpy as np
from PIL import Image

LOOKS = [
    ("default", 0.4, 40, 90, 0.3, 2),
    ("strong_up", 0.6, 120, 90, 0.6, 3),
    ("right_sharp", 0.5, 60, 0, 0.0, 2),
]
inp, prefix = sys.argv[1], sys.argv[2]
pan = os.environ.get("PREVIEW_PAN", "4")  # simulated camera motion, px/frame
exe = os.environ.get("PREVIEW_EXE", "build/preview")
img = np.asarray(Image.open(inp).convert("RGBA"), dtype=np.float32) / 255.0
h, w = img.shape[:2]
with tempfile.TemporaryDirectory() as d:
    src = os.path.join(d, "in.raw")
    img.tofile(src)
    for name, *vals in LOOKS:
        out = os.path.join(d, "out.raw")
        subprocess.run([exe, src, out, str(w), str(h), *map(str, vals), pan], check=True)
        res = np.fromfile(out, dtype=np.float32).reshape(h, w, 4)
        Image.fromarray((np.clip(res, 0, 1) * 255 + 0.5).astype(np.uint8)).convert("RGB").save(f"{prefix}_{name}.png")
        print("wrote", f"{prefix}_{name}.png")
