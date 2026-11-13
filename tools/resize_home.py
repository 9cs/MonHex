"""Shrinks the PokeAPI HOME-style renders (512x512) to the size the app shows
(240 px) and stores them as WebP. Output: romfs/home/<species>[f][s].webp
Run tools/fetch_sprites.sh first (it downloads lib/pokeapi-sprites). Needs Pillow."""
import os
from multiprocessing import Pool

from PIL import Image

SIZE = 240
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "lib", "pokeapi-sprites", "sprites", "pokemon", "other", "home")
DST = os.path.join(ROOT, "romfs", "home")
SETS = (("", ""), ("shiny", "s"), ("female", "f"), (os.path.join("shiny", "female"), "fs"))


def jobs():
    for sub, suffix in SETS:
        folder = os.path.join(SRC, sub)
        if not os.path.isdir(folder):
            continue
        for name in os.listdir(folder):
            stem, ext = os.path.splitext(name)
            if ext != ".png" or not stem.isdigit() or not 1 <= int(stem) <= 1025:
                continue
            src = os.path.join(folder, name)
            out = os.path.join(DST, f"{stem}{suffix}.webp")
            if not os.path.exists(out) or os.path.getmtime(out) < os.path.getmtime(src):
                yield src, out


def convert(job):
    src, out = job
    img = Image.open(src).convert("RGBA").resize((SIZE, SIZE), Image.LANCZOS)
    img.save(out, "WEBP", quality=90, alpha_quality=100, method=6)


if __name__ == "__main__":
    os.makedirs(DST, exist_ok=True)
    todo = list(jobs())
    with Pool() as pool:
        pool.map(convert, todo, chunksize=8)
    print(f"HOME renders: {len(todo)} written to romfs/home")
