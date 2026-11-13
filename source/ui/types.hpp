// Colours and names for Pokemon types and move categories
#pragma once

#include <string>

#include "enums/Move.hpp"
#include "enums/Type.hpp"
#include "ui/gfx.hpp"

namespace monhex::ui
{
    // The type colours of the newer games, slightly calmed for a dark background
    Color typeColor(pksm::Type type);
    Color typeColor(u8 typeIndex);
    // Type name in Portuguese
    const char* typeName(u8 typeIndex);
    // Nature name in Portuguese (the games and PKSM-Core have no Portuguese)
    const char* natureName(u8 nature);
    // Which stats a nature raises and lowers, e.g. "+Ataque, −At. Esp.", or "neutra"
    std::string natureEffect(u8 nature);
    // Text colour that reads well on top of the colour
    Color textOn(Color background);

    struct MoveDetails
    {
        bool known;    // the table has this move
        u8 type;       // 0..17
        u8 category;   // 0 status, 1 physical, 2 special
        u8 power;
        u8 pp;
        u8 accuracy;   // 0 when it never misses
    };
    MoveDetails moveDetails(pksm::Move move);

    // White symbol of the type, to tint (nullptr if missing)
    SDL_Texture* typeIcon(Gfx& gfx, u8 typeIndex);
    // Pill with the type's symbol and name in its colour; returns its width
    int typeBadge(Gfx& gfx, u8 typeIndex, int x, int y, int height, int fontSize);
    // Width typeBadge would draw
    int typeBadgeWidth(Gfx& gfx, u8 typeIndex, int height, int fontSize);
    // One or two badges in maxWidth; when two full badges don't fit they shrink to
    // their symbols only
    void typeBadges(Gfx& gfx, u8 type1, u8 type2, int x, int y, int height, int fontSize, int maxWidth);
    // The type's symbol in a small circle of its colour
    void typeCircle(Gfx& gfx, u8 typeIndex, int cx, int cy, int radius);
}
