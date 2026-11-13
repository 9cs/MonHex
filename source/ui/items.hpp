// Item icons, descriptions, and the move each TM/TR teaches
#pragma once

#include <string>

#include "enums/GameVersion.hpp"
#include "enums/Move.hpp"
#include "ui/gfx.hpp"

namespace monhex::ui
{
    // The item's picture (artwork, else sprite, else a generic one); never null
    // unless nothing at all could be loaded
    SDL_Texture* itemIcon(Gfx& gfx, u16 id, const std::string& englishName);
    // Move taught by a TM/TR in this game, or Move::None for other items
    pksm::Move machineMove(pksm::GameVersion game, const std::string& englishName);
    // Descriptions from PokeAPI in the menu language (English for Portuguese
    // and when missing), or empty
    const std::string& itemDescription(u16 id);
    const std::string& abilityDescription(u16 id);
}
