// Picks the sprite for a Pokemon or Poke Ball from romfs
#pragma once

#include "pkx/PKX.hpp"
#include "ui/gfx.hpp"

namespace monhex::ui
{
    struct Sprite
    {
        SDL_Texture* tex = nullptr;
        int w = 0, h = 0;      // natural size (pixel art: cropped to its visible pixels)
        bool pixelArt = false;
    };

    // Small pixel-art box icon (68x56, from PKHeX), for grids and lists
    Sprite boxSprite(Gfx& gfx, const pksm::PKX& pk);
    // Box icon for a species in its usual form (for lists)
    Sprite speciesSprite(Gfx& gfx, u16 species);
    // Large HOME-style render (160x160, from PokeAPI) for the summary. Forms that
    // have no render fall back to the box icon.
    Sprite homeSprite(Gfx& gfx, const pksm::PKX& pk);
    // Draws a sprite centred in the box, as large as fits (and at most maxScale
    // times its natural size, when given)
    void drawSprite(Gfx& gfx, const Sprite& s, int x, int y, int w, int h, float maxScale = 0);

    // 20x20 Poke Ball icon, or nullptr
    SDL_Texture* ballSprite(Gfx& gfx, const pksm::PKX& pk);
}
