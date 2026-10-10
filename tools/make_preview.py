"""Renders preview PNGs of the effect (needs Pillow + numpy).
Usage: python tools/make_preview.py input.png output_prefix [key=value ...]
Without key=value pairs a few example looks are rendered; with them, exactly
that one look (keys: see tools/preview.cpp)."""
import subprocess, sys, os, tempfile
import numpy as np
from PIL import Image

LOOKS = {
    "default": [],
    "symmetric": ["symmetric=1"],
    "ghost": ["ghost=0.4", "gdist=120", "strength=0.6"],
    "strong": ["strength=1.8"],
}
inp, prefix = sys.argv[1], sys.argv[2]
looks = {"custom": sys.argv[3:]} if len(sys.argv) > 3 else LOOKS
exe = os.environ.get("PREVIEW_EXE", "build/preview")
img = np.asarray(Image.open(inp).convert("RGBA"), dtype=np.float32) / 255.0
h, w = img.shape[:2]
with tempfile.TemporaryDirectory() as d:
    src = os.path.join(d, "in.raw")
    img.tofile(src)
    for name, args in looks.items():
        out = os.path.join(d, "out.raw")
        subprocess.run([exe, src, out, str(w), str(h), *args], check=True)
        res = np.fromfile(out, dtype=np.float32).reshape(h, w, 4)
        Image.fromarray((np.clip(res, 0, 1) * 255 + 0.5).astype(np.uint8)).convert("RGB").save(f"{prefix}_{name}.png")
        print("wrote", f"{prefix}_{name}.png")
