#!/bin/sh
# Downloads the images the app bundles (only the folders needed, not whole
# repositories) and copies them into romfs:
#   romfs/sprites  Pokemon box sprites and Poke Balls (PKHeX)
#   romfs/items    item icons (PKHeX)
#   romfs/types    type icons (PKHeX)
#   romfs/home     HOME-style renders, shrunk to WebP by tools/resize_home.py (PokeAPI)
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CACHE="$ROOT/lib/pkhex-sprites"
IMG="PKHeX.Drawing.PokeSprite/Resources/img"
TYPES="PKHeX.Drawing.Misc/Resources/img/types/square"

if [ ! -d "$CACHE/.git" ]; then
    git clone --depth 1 --filter=blob:none --sparse https://github.com/kwsch/PKHeX.git "$CACHE"
fi
git -C "$CACHE" sparse-checkout set \
    "$IMG/Artwork Pokemon Sprites" "$IMG/Artwork Shiny Sprites" \
    "$IMG/Big Pokemon Sprites" "$IMG/Big Shiny Sprites" "$IMG/ball" \
    "$IMG/Big Items" "$IMG/Artwork Items" "$TYPES"
git -C "$CACHE" pull --quiet --depth 1 || true

# Box sprites: the app uses the pixel set ("b_") and falls back to the artwork
# set ("a_") only where there is no pixel sprite, so only those are kept
mkdir -p "$ROOT/romfs/sprites"
for dir in "Big Pokemon Sprites" "Big Shiny Sprites" "ball"; do
    cp "$CACHE/$IMG/$dir/"*.png "$ROOT/romfs/sprites/"
done
for dir in "Artwork Pokemon Sprites" "Artwork Shiny Sprites"; do
    for f in "$CACHE/$IMG/$dir/"*.png; do
        name=$(basename "$f")
        [ -f "$ROOT/romfs/sprites/b_${name#a_}" ] || cp "$f" "$ROOT/romfs/sprites/"
    done
done
echo "Sprites: $(ls "$ROOT/romfs/sprites" | wc -l) files in romfs/sprites"

# Item icons: artwork ("aitem_") first, sprites ("bitem_") only where it is missing
mkdir -p "$ROOT/romfs/items"
cp "$CACHE/$IMG/Artwork Items/"*.png "$ROOT/romfs/items/"
for f in "$CACHE/$IMG/Big Items/"*.png; do
    name=$(basename "$f")
    [ -f "$ROOT/romfs/items/a${name#b}" ] || cp "$f" "$ROOT/romfs/items/"
done

# Type icons (white symbol on a coloured square; the app keeps only the symbol)
mkdir -p "$ROOT/romfs/types"
cp "$CACHE/$TYPES/"*.png "$ROOT/romfs/types/"

# HOME-style renders (512x512 PNG in PokeAPI, shrunk to WebP by tools/resize_home.py)
POKEAPI="$ROOT/lib/pokeapi-sprites"
if [ ! -d "$POKEAPI/.git" ]; then
    git clone --depth 1 --filter=blob:none --sparse https://github.com/PokeAPI/sprites.git "$POKEAPI"
fi
git -C "$POKEAPI" sparse-checkout set --no-cone \
    '/sprites/pokemon/other/home/*.png' '/sprites/pokemon/other/home/shiny/*.png' \
    '/sprites/pokemon/other/home/female/*.png' '/sprites/pokemon/other/home/shiny/female/*.png'
PY=python3
python3 -c "import PIL" 2>/dev/null || PY=python
"$PY" "$ROOT/tools/resize_home.py"
