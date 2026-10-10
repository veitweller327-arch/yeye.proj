"""Renders before/after preview PNGs with the effect core (needs Pillow + numpy).
Usage: python tools/make_preview.py input.png output_prefix [frame] [pan]"""
import subprocess, sys, os, tempfile
import numpy as np
from PIL import Image

inp, prefix = sys.argv[1], sys.argv[2]
frame = sys.argv[3] if len(sys.argv) > 3 else "12"
pan = sys.argv[4] if len(sys.argv) > 4 else "6"
exe = os.environ.get("PREVIEW_EXE", "build/preview")
img = np.asarray(Image.open(inp).convert("RGBA"), dtype=np.float32) / 255.0
h, w = img.shape[:2]
with tempfile.TemporaryDirectory() as d:
    src = os.path.join(d, "in.raw")
    img.tofile(src)
    for preset, name in ((0, "subtle"), (1, "musicvideo")):
        out = os.path.join(d, "out.raw")
        subprocess.run([exe, src, out, str(w), str(h), str(preset), frame, pan], check=True)
        res = np.fromfile(out, dtype=np.float32).reshape(h, w, 4)
        Image.fromarray((np.clip(res, 0, 1) * 255 + 0.5).astype(np.uint8)).convert("RGB").save(f"{prefix}_{name}.png")
        print("wrote", f"{prefix}_{name}.png")
