// Colours, layout and small helpers shared by the app's screens
#pragma once

#include <SDL.h>
#include <switch.h>

#include "enums/Language.hpp"
#include "ui/gfx.hpp"
#include "ui/input.hpp"

namespace monhex
{
    // Game data (species, moves...) comes from PKSM-Core, which has no Portuguese
    // Changed by setUiLanguage (lang.cpp)
    inline pksm::Language LANG = pksm::Language::ENG;

    namespace theme
    {
        using ui::Color;
        inline constexpr Color BG{24, 25, 34};
        inline constexpr Color CARD{40, 42, 58, 236};
        inline constexpr Color STAGE{31, 33, 46};
        inline constexpr Color RED{233, 69, 96};
        inline constexpr Color BLUE{70, 120, 240};
        inline constexpr Color LINE{255, 255, 255, 46};
        inline constexpr Color TEXT{255, 255, 255};
        inline constexpr Color MUTED{176, 178, 188};
        inline constexpr Color DIM{124, 126, 136};
        inline constexpr Color CYAN_A{0, 255, 214};
        inline constexpr Color CYAN_B{0, 186, 255};
        inline constexpr Color GOLD{255, 205, 60};
        inline constexpr Color GOLD_DEEP{214, 150, 30};
        inline constexpr Color MALE{98, 170, 255};
        inline constexpr Color FEMALE{255, 120, 160};
        inline constexpr Color IV_BAR{86, 214, 150};
        inline constexpr Color EV_BAR{112, 156, 255};
        inline constexpr Color BAR_BG{26, 27, 38};
        inline constexpr Color SHADOW{0, 0, 0, 110};
    }

    namespace layout
    {
        // Header and footer lines like the system menus
        inline constexpr int HEADER_H = 88, FOOTER_Y = 647, MARGIN = 30;
        inline constexpr int CONTENT_Y = HEADER_H + 16;

        // Box screen: the box (wallpaper) on the left, the summary on the right
        inline constexpr int BOX_X = MARGIN, BOX_Y = CONTENT_Y, BOX_W = 690, BOX_H = 528;
        inline constexpr int BOX_HEADER_Y = BOX_Y + 14, BOX_HEADER_H = 48;
        inline constexpr int CELL_W = 104, CELL_H = 80, CELL_GAP = 6;
        inline constexpr int GRID_COLS = 6, GRID_ROWS = 5, BOX_SLOTS = GRID_COLS * GRID_ROWS;
        inline constexpr int GRID_W = GRID_COLS * CELL_W + (GRID_COLS - 1) * CELL_GAP;
        inline constexpr int GRID_X = BOX_X + (BOX_W - GRID_W) / 2, GRID_Y = BOX_Y + 78;
        inline constexpr int PARTY_COLS = 2, PARTY_W = (GRID_W - CELL_GAP) / 2, PARTY_H = 134;
        inline constexpr float BOX_SPRITE_SCALE = 1.35f;
        inline constexpr int WALLPAPER_PATTERNS = 4;
        inline constexpr int BANK_BOXES = 50;
        inline constexpr int PANEL_X = 742, PANEL_Y = CONTENT_Y, PANEL_W = 508, PANEL_H = 528;

        inline SDL_Rect gridCell(int i)
        {
            return {GRID_X + (i % GRID_COLS) * (CELL_W + CELL_GAP),
                GRID_Y + (i / GRID_COLS) * (CELL_H + CELL_GAP), CELL_W, CELL_H};
        }

        inline SDL_Rect partyCard(int i)
        {
            return {GRID_X + (i % PARTY_COLS) * (PARTY_W + CELL_GAP),
                GRID_Y + (i / PARTY_COLS) * (PARTY_H + CELL_GAP), PARTY_W, PARTY_H};
        }
    }

    inline constexpr u64 KEY_UP    = HidNpadButton_AnyUp;
    inline constexpr u64 KEY_DOWN  = HidNpadButton_AnyDown;
    inline constexpr u64 KEY_LEFT  = HidNpadButton_AnyLeft;
    inline constexpr u64 KEY_RIGHT = HidNpadButton_AnyRight;

    // Moves a cursor through a grid of `count` items, `cols` wide
    inline int moveInGrid(int cursor, int count, int cols, u64 pressed)
    {
        if (count <= 0)
            return 0;
        if (pressed & KEY_RIGHT)
            cursor = SDL_min(cursor + 1, count - 1);
        if (pressed & KEY_LEFT)
            cursor = SDL_max(cursor - 1, 0);
        if ((pressed & KEY_DOWN) && cursor + cols < count)
            cursor += cols;
        if ((pressed & KEY_UP) && cursor - cols >= 0)
            cursor -= cols;
        return cursor;
    }

    inline bool tappedRect(const SDL_Rect& r, const ui::Input& in)
    {
        return in.tappedIn(r.x, r.y, r.w, r.h);
    }

    // Animation curves: progress 0..1 in, eased progress out
    inline float clamp01(float t) { return t < 0 ? 0 : (t > 1 ? 1 : t); }
    inline float easeOut(float t)
    {
        t = clamp01(t);
        return 1 - (1 - t) * (1 - t) * (1 - t);
    }
    inline float easeOutBack(float t)
    {
        t = clamp01(t);
        constexpr float c1 = 1.70158f, c3 = c1 + 1;
        return 1 + c3 * (t - 1) * (t - 1) * (t - 1) + c1 * (t - 1) * (t - 1);
    }
}
