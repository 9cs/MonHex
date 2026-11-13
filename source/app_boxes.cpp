// The box screen: wallpapered boxes, the party, the summary panel and the
// detail view opened with A
#include <cmath>
#include <cstdio>

#include "app.hpp"
#include "lang.hpp"
#include "theme.hpp"
#include "ui/items.hpp"
#include "ui/sprites.hpp"
#include "ui/types.hpp"
#include "utils/i18n.hpp"

using namespace monhex::ui;
using namespace monhex::layout;

namespace monhex
{
    namespace
    {
        constexpr float BOX_SLIDE_TIME = 0.30f;
        constexpr float SUMMARY_TIME   = 0.42f;
        constexpr float DETAIL_TIME    = 0.32f;

        // Stat order used on screen (the radar goes clockwise from the top)
        struct StatInfo
        {
            pksm::Stat stat;
            const char* label;
            const char* shortLabel;
        };
        constexpr StatInfo STATS[] = {{pksm::Stat::HP, "PS", "PS"}, {pksm::Stat::ATK, "Ataque", "Atq"},
            {pksm::Stat::DEF, "Defesa", "Def"}, {pksm::Stat::SPATK, "At. Esp.", "AtE"},
            {pksm::Stat::SPDEF, "Def. Esp.", "DfE"}, {pksm::Stat::SPD, "Velocidade", "Vel"}};
        constexpr int RADAR_ORDER[] = {0, 1, 2, 5, 4, 3}; // HP, Atk, Def, Spe, SpD, SpA

        u8 baseStat(const pksm::PKX& pk, pksm::Stat s)
        {
            switch (s)
            {
                case pksm::Stat::HP:
                    return pk.baseHP();
                case pksm::Stat::ATK:
                    return pk.baseAtk();
                case pksm::Stat::DEF:
                    return pk.baseDef();
                case pksm::Stat::SPATK:
                    return pk.baseSpa();
                case pksm::Stat::SPDEF:
                    return pk.baseSpd();
                default:
                    return pk.baseSpe();
            }
        }

        // Natures raise one stat and lower another, in the order Atk, Def, Spe, SpA, SpD
        int natureEffect(const pksm::PKX& pk, pksm::Stat s)
        {
            int n = u8(pk.nature());
            if (n / 5 == n % 5 || s == pksm::Stat::HP)
                return 0;
            static const pksm::Stat ORDER[] = {pksm::Stat::ATK, pksm::Stat::DEF, pksm::Stat::SPD,
                pksm::Stat::SPATK, pksm::Stat::SPDEF};
            if (ORDER[n / 5] == s)
                return 1;
            if (ORDER[n % 5] == s)
                return -1;
            return 0;
        }

        // Detail card and the stage holding the Pokemon
        constexpr int DX = 64, DY = 56, DW = SCREEN_W - 128, DH = 566;
        constexpr int SX = DX + 28, SY = DY + 28, SW = 360, SH = 290;

        const char* CATEGORY[]        = {"Estado", "Físico", "Especial"};
        const char* CATEGORY_SHORT[]  = {"Est.", "Fís.", "Esp."};
        const Color CATEGORY_COLOR[] = {{170, 174, 190}, {246, 140, 80}, {120, 160, 255}};
    }

    // ------------------------------------------------------------------ state

    void App::loadSlots(void)
    {
        if (mBankView)
        {
            loadBankSlots();
            return;
        }
        mSlots.clear();
        const pksm::Sav& sav = *mSave.sav;
        if (mPartyView)
        {
            int count = sav.partyCount();
            for (int i = 0; i < 6; i++)
                mSlots.push_back(i < count ? sav.pkm(static_cast<u8>(i)) : nullptr);
        }
        else
        {
            for (int slot = 0; slot < BOX_SLOTS; slot++)
                mSlots.push_back(sav.pkm(static_cast<u8>(mBox), static_cast<u8>(slot)));
        }
        // Empty slots are kept as nullptr
        for (auto& pk : mSlots)
        {
            if (pk && pk->species() == pksm::Species::None)
                pk.reset();
        }
    }

    const pksm::PKX* App::selectedPokemon(void) const
    {
        if (mSlotCursor < 0 || mSlotCursor >= static_cast<int>(mSlots.size()))
            return nullptr;
        return mSlots[mSlotCursor].get();
    }

    std::string App::boxTitle(int box) const
    {
        std::string name   = mSave.sav->boxName(static_cast<u8>(box));
        std::string number = tr("Caixa ") + std::to_string(box + 1);
        return name.empty() || name == number ? number : name;
    }

    void App::changeBox(int delta)
    {
        int boxes   = mBankView ? BANK_BOXES : mSave.sav->maxBoxes();
        int& box    = mBankView ? mBankBox : mBox;
        mPrevSlots  = std::move(mSlots);
        mPrevBox    = box;
        box         = (box + delta + boxes) % boxes;
        mBoxAnimDir = delta > 0 ? 1 : -1;
        mBoxAnimTime = mGfx.time();
        loadSlots();
    }

    void App::openDetail(void)
    {
        // Freeze the current screen, blurred, as the backdrop
        mGfx.beginCapture();
        drawBackground();
        drawBoxes();
        mBlurred = mGfx.endCapture();
        mHintAreas.clear();
        mDetail     = true;
        mDetailTime = mGfx.time();
        mDetailSwapTime = mDetailTime;
    }

    // ------------------------------------------------------------------ views

    App::View App::currentView(void) const
    {
        if (mBagView)
            return View::Bag;
        if (mPartyView)
            return View::Party;
        if (mBankView)
            return View::Bank;
        return View::Boxes;
    }

    void App::setView(View view)
    {
        if (view == currentView())
            return;
        // Moving Pokemon can't go into the bag
        if (view == View::Bag && !mHeld.empty())
            return;
        mBagView     = view == View::Bag;
        mPartyView   = view == View::Party;
        mBankView    = view == View::Bank;
        mSlotCursor  = 0;
        mBoxAnimTime = -10;
        mBagFocus    = 0;
        mPrevSlots.clear();
        mHighlight.snap();
        if (!mBagView)
            loadSlots();
    }

    std::vector<std::pair<App::View, SDL_Rect>> App::viewTabs(void)
    {
        static const std::pair<View, const char*> TABS[] = {
            {View::Boxes, "Caixas"}, {View::Party, "Equipe"}, {View::Bag, "Mochila"}, {View::Bank, "Banco"}};
        std::vector<std::pair<View, SDL_Rect>> out;
        // Start well clear of the logo and name
        int x = 98 + mGfx.textWidth("MonHex", 32) + 40;
        for (const auto& [view, label] : TABS)
        {
            int w = mGfx.textWidth(label, 19) + 64;
            out.push_back({view, {x, 22, w, 44}});
            x += w + 8;
        }
        return out;
    }

    void App::drawViewTabs(void)
    {
        static const char* LABELS[] = {"Caixas", "Equipe", "Mochila", "Banco"};
        static const u32 KEYS[]     = {glyph::Y, glyph::X, glyph::PLUS, glyph::MINUS};
        View now = currentView();
        for (const auto& [view, r] : viewTabs())
        {
            int i    = int(view);
            bool sel = view == now;
            mGfx.fillRoundRect(r.x, r.y, r.w, r.h, r.h / 2, sel ? Color{0, 170, 230, 120} : Color{255, 255, 255, 16});
            if (sel)
                mGfx.roundRect(r.x, r.y, r.w, r.h, r.h / 2, theme::CYAN_A, 2);
            int tx = r.x + 20;
            mGfx.text(tr(LABELS[i]), tx, r.y + (r.h - mGfx.textHeight(19)) / 2, 19, sel ? theme::TEXT : theme::MUTED);
            if (KEYS[i])
                mGfx.buttonGlyph(KEYS[i], r.x + r.w - 32, r.y + 10, 22, sel ? theme::TEXT : theme::DIM);
        }
        if (!mBagView)
        {
            SDL_Rect s = searchButton();
            mGfx.fillRoundRect(s.x, s.y, s.w, s.h, s.h / 2, Color{255, 255, 255, 16});
            mGfx.text(tr("Buscar"), s.x + 20, s.y + (s.h - mGfx.textHeight(19)) / 2, 19, theme::MUTED);
            mGfx.buttonGlyph(glyph::RS, s.x + s.w - 32, s.y + 10, 22, theme::DIM);
        }
    }

    SDL_Rect App::searchButton(void)
    {
        auto tabs = viewTabs();
        const SDL_Rect& last = tabs.back().second;
        return {last.x + last.w + 20, last.y, mGfx.textWidth(tr("Buscar"), 19) + 64, last.h};
    }

    bool App::tapViewTabs(void)
    {
        for (const auto& [view, r] : viewTabs())
        {
            if (tappedRect(r, mInput))
            {
                setView(view);
                return true;
            }
        }
        return false;
    }

    // ------------------------------------------------------------------ input

    void App::updateBoxes(u64 pressed)
    {
        if (mDetail)
        {
            if (mEditing)
            {
                updateEditor(pressed);
                return;
            }
            if (mPicker.open)
            {
                updatePicker(pressed); // choosing a species to create
                return;
            }
            if (pressed & HidNpadButton_X)
            {
                startEditing();
                return;
            }
            if (pressed & (HidNpadButton_B | HidNpadButton_A))
            {
                mDetail = false;
                return;
            }
            if (mInput.tappedIn(SX, SY, SW, SH))
            {
                playCry();
                return;
            }
            // Left/right step through the Pokemon in this box or the party
            int dir = (pressed & (KEY_RIGHT | HidNpadButton_R)) ? 1 : ((pressed & (KEY_LEFT | HidNpadButton_L)) ? -1 : 0);
            if (dir)
            {
                int n = static_cast<int>(mSlots.size());
                for (int step = 1; step < n; step++)
                {
                    int i = (mSlotCursor + dir * step + n) % n;
                    if (mSlots[i])
                    {
                        mSlotCursor     = i;
                        mDetailSwapTime = mGfx.time();
                        mHighlight.snap();
                        break;
                    }
                }
            }
            return;
        }

        if (mSearch.open)
        {
            updateSearch(pressed);
            return;
        }
        if (mMenu.open)
        {
            updateMenu(pressed);
            return;
        }
        if (mBagView && mPicker.open)
        {
            updateBag(pressed);
            return;
        }
        if (tapViewTabs())
            return;
        // Views: Y boxes, X party, + bag, - bank
        if (pressed & HidNpadButton_Y)
        {
            setView(View::Boxes);
            return;
        }
        if (pressed & HidNpadButton_X)
        {
            setView(View::Party);
            return;
        }
        if (pressed & HidNpadButton_Plus)
        {
            setView(View::Bag);
            return;
        }
        if (pressed & HidNpadButton_Minus)
        {
            setView(View::Bank);
            return;
        }

        // B goes back one step at a time: cancel the move, clear the marks, leave
        // the bag's items for its pouches, leave other views for the boxes, and
        // only from the boxes back to the list of games
        if (pressed & HidNpadButton_B)
        {
            if (!mHeld.empty())
            {
                mHeld.clear();
                showToast(tr("Cancelado."));
            }
            else if (!mMarked.empty())
                mMarked.clear();
            else if (mBagView && mBagFocus == 1)
                mBagFocus = 0;
            else if (currentView() != View::Boxes)
                setView(View::Boxes);
            else if (hasUnsaved())
                askUnsaved(DialogAction::LeaveBoxes, DialogAction::SaveThenLeave);
            else
                runDialogAction(DialogAction::LeaveBoxes);
            return;
        }

        if (mBagView)
        {
            updateBag(pressed);
            return;
        }

        if ((pressed & HidNpadButton_ZL) && hasUnsaved() && mHeld.empty())
        {
            saveChanges(DialogAction::None);
            return;
        }
        if ((pressed & HidNpadButton_ZR) && mHeld.empty())
            toggleMark();
        if (((pressed & HidNpadButton_StickR) || tappedRect(searchButton(), mInput)) && mHeld.empty())
        {
            openSearch();
            return;
        }

        if (!mPartyView)
        {
            int col = mSlotCursor % GRID_COLS;
            // L/R, the arrows, or walking off the side of the grid change the box
            bool walkLeft  = (pressed & KEY_LEFT) && col == 0;
            bool walkRight = (pressed & KEY_RIGHT) && col == GRID_COLS - 1;
            bool prev = (pressed & HidNpadButton_L) || walkLeft ||
                        mInput.tappedIn(BOX_X, BOX_HEADER_Y, 130, BOX_HEADER_H);
            bool next = (pressed & HidNpadButton_R) || walkRight ||
                        mInput.tappedIn(BOX_X + BOX_W - 130, BOX_HEADER_Y, 130, BOX_HEADER_H);
            if (prev || next)
            {
                if (walkLeft)
                    mSlotCursor += GRID_COLS - 1;
                else if (walkRight)
                    mSlotCursor -= GRID_COLS - 1;
                mHighlight.snap();
                changeBox(prev ? -1 : 1);
            }
            else
            {
                mSlotCursor = moveInGrid(mSlotCursor, BOX_SLOTS, GRID_COLS, pressed);
            }
            for (int i = 0; i < BOX_SLOTS; i++)
            {
                if (tappedRect(gridCell(i), mInput))
                {
                    if (i == mSlotCursor)
                        pressed |= HidNpadButton_A; // a second tap opens (or creates) it
                    mSlotCursor = i;
                }
            }
        }
        else
        {
            mSlotCursor = moveInGrid(mSlotCursor, 6, PARTY_COLS, pressed);
            for (int i = 0; i < 6; i++)
            {
                if (tappedRect(partyCard(i), mInput))
                {
                    if (i == mSlotCursor)
                        pressed |= HidNpadButton_A;
                    mSlotCursor = i;
                }
            }
        }

        if (pressed & HidNpadButton_A)
        {
            mPressTime = mGfx.time();
            if (!mHeld.empty())
                dropHeld();
            else
                openSlotMenu();
        }
    }

    // ------------------------------------------------------------------ shiny

    void App::drawShinyMark(int cx, int cy, int size)
    {
        // Two still stars, a big one and a small one up to its right, with a dark
        // rim so they read on any background
        int small = size * 11 / 20;
        int sx = cx + size * 9 / 20, sy = cy - size * 9 / 20;
        mGfx.sparkle(cx + 1, cy + 1, size + 4, Color{60, 36, 0, 150});
        mGfx.sparkle(sx + 1, sy + 1, small + 3, Color{60, 36, 0, 150});
        mGfx.sparkle(cx, cy, size, theme::GOLD);
        mGfx.sparkle(sx, sy, small, Color{255, 236, 160});
    }

    void App::drawShinyStage(int x, int y, int w, int h, int radius)
    {
        // A light gold wash with small sparkles twinkling all over it
        mGfx.fillRoundRect(x, y, w, h, radius, lerp(theme::STAGE, theme::GOLD_DEEP, 0.20f));
        mGfx.shadow(x + w / 4, y + h / 4, w / 2, h / 2, w / 4, w / 5, theme::GOLD.withAlpha(34));
        float t = mGfx.time();
        int count = SDL_max(8, w * h / 2600);
        for (int i = 0; i < count; i++)
        {
            // Low-discrepancy positions spread the sparkles evenly over the stage
            float fx = std::fmod(0.13f + i * 0.6180339f, 1.0f);
            float fy = std::fmod(0.37f + i * 0.4142135f, 1.0f);
            float drift = std::sin(t * 0.4f + i) * 3;
            int px = x + 8 + int((w - 16) * fx + drift);
            int py = y + 8 + int((h - 16) * fy - std::fmod(t * 4 + i * 7, 12.0f));
            float twinkle = 0.5f + 0.5f * std::sin(t * (1.1f + 0.13f * (i % 5)) + i * 2.1f);
            twinkle *= twinkle;
            mGfx.sparkle(px, py, 7 + (i % 3) * 4, Color{255, 236, 170, u8(95 * twinkle)});
        }
    }

    // ------------------------------------------------------------------ box drawing

    void App::buildWallpapers(void)
    {
        // White patterns, tinted per box when drawn; corners rounded to the box shape
        constexpr int W = BOX_W, H = BOX_H, R = 20;
        auto edge = [](float v) { return v < 0 ? 0.0f : (v > 1 ? 1.0f : v); };
        for (int p = 0; p < WALLPAPER_PATTERNS; p++)
        {
            SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_RGBA32);
            if (!surf)
                continue;
            u32* px = static_cast<u32*>(surf->pixels);
            for (int y = 0; y < H; y++)
            {
                for (int x = 0; x < W; x++)
                {
                    float fx = x + 0.5f, fy = y + 0.5f, a = 0;
                    switch (p)
                    {
                        case 0: // diagonal stripes
                        {
                            float s = std::fmod(fx + fy, 44.0f);
                            a = edge(11 - std::fabs(s - 22) + 0.5f) * 0.55f;
                            break;
                        }
                        case 1: // dots in a staggered grid
                        {
                            float row = std::floor(fy / 34);
                            float ox  = std::fmod(fx + (int(row) % 2) * 17, 34.0f) - 17;
                            float oy  = std::fmod(fy, 34.0f) - 17;
                            a = edge(5.5f - std::sqrt(ox * ox + oy * oy) + 0.5f) * 0.7f;
                            break;
                        }
                        case 2: // small Poke Ball outlines
                        {
                            float ox = std::fmod(fx, 76.0f) - 38, oy = std::fmod(fy, 76.0f) - 38;
                            if (int(fy / 76) % 2)
                                ox = std::fmod(fx + 38, 76.0f) - 38;
                            float d    = std::sqrt(ox * ox + oy * oy);
                            float ring = edge(2.2f - std::fabs(d - 15) + 0.5f);
                            float band = edge(1.6f - std::fabs(oy) + 0.5f) * edge(15 - d + 0.5f) *
                                         edge(std::fabs(ox) - 5 + 0.5f);
                            float btn  = edge(1.8f - std::fabs(d - 4.5f) + 0.5f);
                            a = std::max({ring, band, btn}) * 0.6f;
                            break;
                        }
                        default: // soft waves
                        {
                            float wave = std::sin(fx / 38.0f) * 9;
                            float s    = std::fmod(fy + wave + 400, 40.0f);
                            a = edge(4 - std::fabs(s - 20) + 0.5f) * 0.6f;
                            break;
                        }
                    }
                    // Fade the pattern towards the bottom and round the corners
                    a *= 1.0f - 0.55f * (fy / H);
                    float qx = std::fabs(fx - W / 2.0f) - (W / 2.0f - R);
                    float qy = std::fabs(fy - H / 2.0f) - (H / 2.0f - R);
                    float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
                    float d  = std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - R;
                    a *= edge(0.5f - d);
                    px[y * (surf->pitch / 4) + x] = SDL_MapRGBA(surf->format, 255, 255, 255, u8(a * 255));
                }
            }
            mWallpapers[p] = mGfx.textureFromSurface(surf, true);
            SDL_FreeSurface(surf);
        }
    }

    void App::drawWallpaper(int box, bool party)
    {
        // Each box gets its own colour and pattern, like the boxes in the games
        static const Color COLORS[] = {{46, 104, 82}, {60, 72, 120}, {132, 100, 58}, {44, 98, 140},
            {136, 62, 62}, {104, 66, 128}, {88, 116, 58}, {64, 112, 130}, {132, 70, 104}, {86, 80, 72}};
        Color c = party       ? Color{150, 52, 70}
                  : mBankView ? Color{78, 58, 130}
                              : COLORS[box % int(sizeof(COLORS) / sizeof(COLORS[0]))];
        mGfx.fillRoundRect(BOX_X, BOX_Y, BOX_W, BOX_H, 20, lerp(theme::BG, c, 0.62f));
        if (SDL_Texture* pattern = mWallpapers[(party || mBankView ? 2 : box) % WALLPAPER_PATTERNS])
            mGfx.draw(pattern, BOX_X, BOX_Y, BOX_W, BOX_H, 34);
    }

    void App::drawBoxHeader(const std::string& title, bool arrows)
    {
        int y = BOX_HEADER_Y, h = BOX_HEADER_H, cy = y + h / 2;
        // Box name on a darker tab in the middle, L/R at the corners
        int tw = SDL_max(260, mGfx.textWidth(title, 26) + 80);
        int tx = BOX_X + (BOX_W - tw) / 2;
        mGfx.fillRoundRect(tx, y, tw, h, h / 2, Color{0, 0, 0, 90});
        mGfx.text(title, BOX_X + BOX_W / 2, y + (h - mGfx.textHeight(26)) / 2, 26, theme::TEXT,
            Align::Center, tw - 40);
        if (arrows)
        {
            mGfx.fillRoundRect(BOX_X + 14, y, 104, h, h / 2, Color{0, 0, 0, 70});
            mGfx.buttonGlyph(glyph::L, BOX_X + 26, cy - 15, 28, theme::TEXT);
            mGfx.arrow(BOX_X + 92, cy, 14, false, theme::TEXT);
            mGfx.fillRoundRect(BOX_X + BOX_W - 118, y, 104, h, h / 2, Color{0, 0, 0, 70});
            mGfx.arrow(BOX_X + BOX_W - 92, cy, 14, true, theme::TEXT);
            mGfx.buttonGlyph(glyph::R, BOX_X + BOX_W - 26, cy - 15, 28, theme::TEXT, Align::Right);
        }
    }

    void App::drawPointer(int cx, int bottomY)
    {
        // The PC's pointer: bobs over the selected slot, red with a white rim
        float bob = std::sin(mGfx.time() * 5.0f) * 4.0f;
        float y   = bottomY - 6 + bob;
        mGfx.triangle({cx - 15.0f, y - 22 + 3}, {cx + 15.0f, y - 22 + 3}, {float(cx), y + 3},
            Color{0, 0, 0, 90});
        mGfx.triangle({cx - 15.0f, y - 22}, {cx + 15.0f, y - 22}, {float(cx), y}, theme::TEXT);
        mGfx.triangle({cx - 10.0f, y - 19}, {cx + 10.0f, y - 19}, {float(cx), y - 5}, theme::RED);
    }

    void App::drawSlotMarks(const SDL_Rect& r, const SlotRef& ref)
    {
        if (isMarked(ref))
        {
            // The slot glows blue, with a bold white tick in a blue badge at the top left
            mGfx.fillRoundRect(r.x, r.y, r.w, r.h, 14, Color{40, 150, 255, 45});
            mGfx.roundRect(r.x, r.y, r.w, r.h, 14, Color{60, 165, 255}, 3);
            float cx = r.x + 17.0f, cy = r.y + 17.0f;
            mGfx.fillCircle(int(cx), int(cy), 14, theme::TEXT);
            mGfx.fillCircle(int(cx), int(cy), 12, Color{30, 140, 255});
            mGfx.thickLine({cx - 6.5f, cy - 0.5f}, {cx - 1.2f, cy + 5.6f}, 3.4f, theme::TEXT);
            mGfx.thickLine({cx - 2.6f, cy + 5.6f}, {cx + 6.5f, cy - 5.0f}, 3.4f, theme::TEXT);
        }
    }

    void App::drawBoxSlots(const std::vector<std::unique_ptr<pksm::PKX>>& slots, int cursor, int box)
    {
        float t = mGfx.time();
        for (int i = 0; i < BOX_SLOTS && i < int(slots.size()); i++)
        {
            SDL_Rect r = gridCell(i);
            const pksm::PKX* pk = slots[i].get();
            bool sel   = i == cursor;
            bool shiny = pk && pk->shiny() && !pk->egg();
            mGfx.fillRoundRect(r.x, r.y, r.w, r.h, 14, Color{255, 255, 255, u8(sel ? 46 : 16)});
            if (!pk)
                continue;
            if (shiny)
            {
                // A warm glow behind shiny Pokemon so they stand out in the box
                mGfx.shadow(r.x + 18, r.y + 14, r.w - 36, r.h - 28, 20, 14, theme::GOLD.withAlpha(60));
            }
            Sprite s = boxSprite(mGfx, *pk);
            // Every icon at the same scale, so big Pokemon stay bigger than small ones
            int dw = int(s.w * BOX_SPRITE_SCALE), dh = int(s.h * BOX_SPRITE_SCALE);
            if (dw > r.w - 6 || dh > r.h - 8)
            {
                float fit = SDL_min(float(r.w - 6) / dw, float(r.h - 8) / dh);
                dw = int(dw * fit), dh = int(dh * fit);
            }
            int hop = sel ? int(std::fabs(std::sin(t * 6.0f)) * 5) : 0;
            int sx  = r.x + (r.w - dw) / 2;
            int sy  = r.y + (r.h - dh) / 2 + 2 - hop;
            SlotRef ref{false, box, i, mBankView};
            mGfx.draw(s.tex, sx, sy, dw, dh, isHeldOrigin(ref) ? 70 : 255);
            drawSlotMarks(r, ref);
            if (shiny)
                drawShinyMark(r.x + r.w - 16, r.y + 15, 20);
        }
    }

    void App::drawParty(void)
    {
        for (int i = 0; i < 6; i++)
        {
            SDL_Rect r = partyCard(i);
            bool sel = i == mSlotCursor;
            mGfx.fillRoundRect(r.x, r.y, r.w, r.h, 16, Color{0, 0, 0, u8(sel ? 70 : 90)});
            const pksm::PKX* pk = mSlots[i].get();
            if (!pk)
            {
                mGfx.text(tr("Vazio"), r.x + r.w / 2, r.y + (r.h - mGfx.textHeight(22)) / 2, 22,
                    theme::MUTED, Align::Center);
                continue;
            }
            constexpr int ST = PARTY_H - 24;
            bool shiny = pk->shiny() && !pk->egg();
            if (shiny)
                drawShinyStage(r.x + 12, r.y + 12, ST, ST, 14);
            else
            {
                Color stage = pk->egg() ? theme::STAGE : lerp(theme::STAGE, typeColor(pk->type1()), 0.25f);
                mGfx.fillRoundRect(r.x + 12, r.y + 12, ST, ST, 14, stage);
            }
            {
                float o = mGfx.opacity();
                if (isHeldOrigin({true, 0, i}))
                    mGfx.setOpacity(o * 0.3f);
                drawSprite(mGfx, homeSprite(mGfx, *pk), r.x + 16, r.y + 16, ST - 8, ST - 8);
                mGfx.setOpacity(o);
            }
            drawSlotMarks(r, {true, 0, i});
            int tx = r.x + ST + 26;
            std::string name = pk->egg() ? tr("Ovo") : pk->nickname();
            mGfx.text(name, tx, r.y + 20, 24, theme::TEXT, Align::Left, r.x + r.w - tx - 34);
            if (!pk->egg())
            {
                mGfx.text(tr("Nv. ") + std::to_string(pk->level()), tx, r.y + 56, 19, theme::MUTED);
                typeBadges(mGfx, u8(pk->type1()), u8(pk->type2()), tx, r.y + 88, 26, 14, r.x + r.w - 12 - tx);
                if (shiny)
                    drawShinyMark(r.x + r.w - 24, r.y + 24, 24);
            }
        }
        SDL_Rect r = partyCard(mSlotCursor);
        mHighlight.moveTo(r.x, r.y, r.w, r.h);
        drawHighlight(16);
    }

    // ------------------------------------------------------------------ summary

    void App::drawSummary(const pksm::PKX* pk)
    {
        const int x = PANEL_X, y = PANEL_Y, w = PANEL_W;
        mGfx.shadow(x, y + 4, w, PANEL_H, 18, 16, theme::SHADOW);
        mGfx.fillRoundRect(x, y, w, PANEL_H, 18, theme::CARD);

        // Restart the entrance animation whenever a different slot is shown
        int key = (mPartyView ? 100000 : (mBankView ? 200000 + mBankBox * 100 : mBox * 100)) + mSlotCursor;
        if (key != mSummaryKey)
        {
            mSummaryKey  = key;
            mSummaryTime = mGfx.time();
        }
        float t = mGfx.time() - mSummaryTime;
        float p = easeOut(t / SUMMARY_TIME);

        if (!pk)
        {
            mGfx.setOpacity(p);
            mGfx.draw(mWatermark, x + w / 2 - 70, y + PANEL_H / 2 - 110, 140, 140, 90);
            mGfx.text(tr("Espaço vazio"), x + w / 2, y + PANEL_H / 2 + 46, 24, theme::DIM, Align::Center);
            mGfx.resetLayer();
            return;
        }

        constexpr int PAD = 20, STAGE = 140;
        bool shiny = pk->shiny() && !pk->egg();
        Color main = pk->egg() ? theme::DIM : (shiny ? theme::GOLD : typeColor(pk->type1()));

        // Stage lit in the colour of the first type (gold and sparkles when shiny)
        if (shiny)
            drawShinyStage(x + PAD, y + PAD, STAGE, STAGE, 16);
        else
        {
            mGfx.fillRoundRect(x + PAD, y + PAD, STAGE, STAGE, 16, lerp(theme::STAGE, main, 0.22f));
            mGfx.shadow(x + PAD + 30, y + PAD + 30, STAGE - 60, STAGE - 60, (STAGE - 60) / 2, 26,
                main.withAlpha(90));
        }
        // The sprite pops in
        {
            Sprite s    = homeSprite(mGfx, *pk);
            float scale = 0.75f + 0.25f * easeOutBack(t / 0.36f);
            int box     = int((STAGE - 8) * scale);
            int off     = (STAGE - 8 - box) / 2;
            mGfx.setOpacity(clamp01(t / 0.15f));
            drawSprite(mGfx, s, x + PAD + 4 + off, y + PAD + 4 + off, box, box);
            mGfx.resetLayer();
        }
        int tx = x + PAD + STAGE + 20, tw = w - (tx - x) - PAD;

        // Name block slides in from the right
        mGfx.setOpacity(p);
        mGfx.setOrigin(int((1 - p) * 18), 0);
        if (pk->egg())
        {
            mGfx.text(tr("Ovo"), tx, y + 30, 32, theme::TEXT);
            mGfx.text(i18n::species(LANG, pk->species()), tx, y + 74, 20, theme::MUTED, Align::Left, tw);
            mGfx.resetLayer();
            return;
        }
        int nx = tx + mGfx.text(pk->nickname(), tx, y + 20, 30, theme::TEXT, Align::Left, tw - 60);
        if (pk->gender() != pksm::Gender::Genderless)
        {
            bool female = pk->gender() == pksm::Gender::Female;
            u32 glyphCp = female ? 0x2640 : 0x2642;
            std::string sym = mGfx.hasGlyph(glyphCp, 26) ? (female ? "♀" : "♂") : (female ? "F" : "M");
            nx += 10 + mGfx.text(sym, nx + 10, y + 23, 26, female ? theme::FEMALE : theme::MALE);
        }
        if (shiny)
            drawShinyMark(nx + 24, y + 40, 26);
        std::string line = tr("Nv. ") + std::to_string(pk->level());
        if (pk->nicknamed())
            line = i18n::species(LANG, pk->species()) + "  ·  " + line;
        mGfx.text(line, tx, y + 60, 19, theme::MUTED, Align::Left, tw);
        int bx = tx + typeBadge(mGfx, u8(pk->type1()), tx, y + 94, 28, 16) + 8;
        if (pk->type2() != pk->type1())
            typeBadge(mGfx, u8(pk->type2()), bx, y + 94, 28, 16);
        if (SDL_Texture* ball = ballSprite(mGfx, *pk))
            mGfx.draw(ball, tx, y + 136, 22, 22);
        mGfx.text(i18n::ball(LANG, pk->ball()), tx + 30, y + 137, 17, theme::MUTED, Align::Left, tw - 30);
        mGfx.resetLayer();

        // Details in two columns
        mGfx.setOpacity(p);
        drawDivider(x + PAD, y + 172, w - 2 * PAD);
        u16 item = pk->heldItem();
        struct Field
        {
            const char* label;
            std::string value;
        };
        Field fields[] = {{tr("Natureza"), natureName(u8(pk->nature()))},
            {tr("Habilidade"), i18n::ability(LANG, pk->ability())},
            {tr("Item"), item ? i18n::item(LANG, item) : tr("Nenhum")}, {tr("Treinador"), pk->otName()}};
        int colW = (w - 2 * PAD) / 2;
        for (int i = 0; i < 4; i++)
        {
            int fx = x + PAD + (i % 2) * colW, fy = y + 182 + (i / 2) * 44;
            mGfx.text(fields[i].label, fx, fy, 15, theme::DIM);
            int vx = fx;
            if (i == 2 && item)
            {
                mGfx.draw(itemIcon(mGfx, item, i18n::item(pksm::Language::ENG, item)), fx - 2, fy + 16, 26, 26);
                vx += 28;
            }
            mGfx.text(fields[i].value, vx, fy + 18, 19, theme::TEXT, Align::Left, colW - 12 - (vx - fx));
        }

        // IVs and EVs: the bars grow and the numbers count up
        drawDivider(x + PAD, y + 272, w - 2 * PAD);
        float bars = easeOut((t - 0.08f) / 0.45f);
        int sy     = y + 280;
        const int ivBarX = x + 128, ivBarW = 110, ivNumX = x + 274;
        const int evBarX = x + 304, evBarW = 140, evNumX = x + w - PAD;
        mGfx.text(tr("Status"), x + PAD, sy, 15, theme::DIM);
        mGfx.text("IV", ivNumX, sy, 15, theme::DIM, Align::Right);
        mGfx.text("EV", evNumX, sy, 15, theme::DIM, Align::Right);
        for (int i = 0; i < 6; i++)
        {
            int ry = sy + 21 + i * 20;
            u8 iv  = pk->iv(STATS[i].stat);
            u16 ev = pk->ev(STATS[i].stat);
            int effect = natureEffect(*pk, STATS[i].stat);
            Color label = effect > 0 ? Color{255, 140, 120} : (effect < 0 ? Color{130, 170, 255} : theme::TEXT);
            mGfx.text(tr(STATS[i].label), x + PAD, ry, 17, label);
            mGfx.fillRoundRect(ivBarX, ry + 7, ivBarW, 8, 4, theme::BAR_BG);
            int ivW = int(ivBarW * iv / 31.0f * bars);
            if (ivW > 0)
                mGfx.fillRoundRect(ivBarX, ry + 7, SDL_max(8, ivW), 8, 4, iv == 31 ? theme::GOLD : theme::IV_BAR);
            mGfx.text(std::to_string(int(iv * bars + 0.5f)), ivNumX, ry, 17, theme::TEXT, Align::Right);
            mGfx.fillRoundRect(evBarX, ry + 7, evBarW, 8, 4, theme::BAR_BG);
            int evW = int(evBarW * SDL_min(ev, 252) / 252.0f * bars);
            if (evW > 0)
                mGfx.fillRoundRect(evBarX, ry + 7, SDL_max(8, evW), 8, 4, theme::EV_BAR);
            mGfx.text(std::to_string(int(ev * bars + 0.5f)), evNumX, ry, 17, theme::TEXT, Align::Right);
        }
        mGfx.resetLayer();

        // Moves slide in one after another
        drawDivider(x + PAD, y + 426, w - 2 * PAD);
        int moveW = (w - 2 * PAD - 8) / 2;
        for (int i = 0; i < 4; i++)
        {
            float mp = easeOut((t - 0.10f - 0.06f * i) / 0.30f);
            mGfx.setOpacity(mp);
            mGfx.setOrigin(int((1 - mp) * 30), 0);
            pksm::Move move = pk->move(static_cast<u8>(i));
            int mx = x + PAD + (i % 2) * (moveW + 8);
            int my = y + 438 + (i / 2) * 42;
            constexpr int MH = 36;
            if (move == pksm::Move::None)
            {
                mGfx.roundRect(mx, my, moveW, MH, 10, theme::LINE, 1);
                mGfx.text("—", mx + moveW / 2, my + (MH - mGfx.textHeight(18)) / 2, 18, theme::DIM, Align::Center);
                continue;
            }
            MoveDetails info = moveDetails(move);
            Color tc = info.known ? typeColor(info.type) : theme::DIM;
            mGfx.fillRoundRect(mx, my, moveW, MH, 10, lerp(theme::STAGE, tc, 0.30f));
            int nameX = mx + 12;
            if (info.known)
            {
                typeCircle(mGfx, info.type, mx + 20, my + MH / 2, 11);
                nameX = mx + 38;
            }
            int right = mx + moveW - 10;
            if (info.known)
            {
                const Color cc = CATEGORY_COLOR[info.category];
                int cw = mGfx.textWidth(tr(CATEGORY_SHORT[info.category]), 13) + 12;
                mGfx.fillRoundRect(right - cw, my + 9, cw, 18, 9, cc.withAlpha(60));
                mGfx.text(tr(CATEGORY_SHORT[info.category]), right - cw / 2, my + 9 + (18 - mGfx.textHeight(13)) / 2,
                    13, cc, Align::Center);
                right -= cw + 8;
                if (info.power > 0)
                    right -= 6 + mGfx.text(std::to_string(info.power), right,
                                     my + (MH - mGfx.textHeight(16)) / 2, 16, theme::MUTED, Align::Right);
            }
            mGfx.text(i18n::move(LANG, move), nameX, my + (MH - mGfx.textHeight(17)) / 2, 17, theme::TEXT,
                Align::Left, right - nameX - 4);
        }
        mGfx.resetLayer();
    }

    // ------------------------------------------------------------------ box screen

    void App::drawBoxes(void)
    {
        if (mDetail)
        {
            drawDetail(); // covers the whole screen with its blurred backdrop
            return;
        }
        drawHeader("");
        drawViewTabs();
        if (mBagView)
        {
            drawBag();
            return;
        }

        mGfx.shadow(BOX_X, BOX_Y + 4, BOX_W, BOX_H, 20, 16, theme::SHADOW);
        float slide = easeOut((mGfx.time() - mBoxAnimTime) / BOX_SLIDE_TIME);
        bool sliding = !mPartyView && slide < 1 && !mPrevSlots.empty();
        if (sliding)
        {
            // The old box slides out and the new one in, clipped to the box area
            SDL_Rect clip{BOX_X, BOX_Y, BOX_W, BOX_H};
            SDL_RenderSetClipRect(mGfx.renderer(), &clip);
            int shift = int(slide * (BOX_W + 24)) * mBoxAnimDir;
            mGfx.setOrigin(-shift, 0);
            drawWallpaper(mPrevBox, false);
            drawBoxHeader(mBankView ? tr("Banco ") + std::to_string(mPrevBox + 1) : boxTitle(mPrevBox), true);
            drawBoxSlots(mPrevSlots, -1, mPrevBox);
            mGfx.setOrigin(mBoxAnimDir * (BOX_W + 24) - shift, 0);
            drawWallpaper(mBankView ? mBankBox : mBox, false);
            drawBoxHeader(mBankView ? tr("Banco ") + std::to_string(mBankBox + 1) : boxTitle(mBox), true);
            drawBoxSlots(mSlots, -1, mBankView ? mBankBox : mBox);
            mGfx.resetLayer();
            SDL_RenderSetClipRect(mGfx.renderer(), nullptr);
        }
        else
        {
            if (!mPartyView && slide >= 1)
                mPrevSlots.clear();
            drawWallpaper(mBankView ? mBankBox : mBox, mPartyView);
            drawBoxHeader(mPartyView  ? tr("Equipe")
                          : mBankView ? tr("Banco ") + std::to_string(mBankBox + 1)
                                      : boxTitle(mBox),
                !mPartyView);
            if (mPartyView)
                drawParty();
            else
            {
                drawBoxSlots(mSlots, mSlotCursor, mBankView ? mBankBox : mBox);
                SDL_Rect r = gridCell(mSlotCursor);
                mHighlight.moveTo(r.x, r.y, r.w, r.h);
                drawHighlight(14);
                drawPointer(int(mHighlight.x) + r.w / 2, int(mHighlight.y) - 2);
            }
        }
        drawSummary(!mHeld.empty() ? mHeld[0].pk.get() : selectedPokemon());
        if (!mHeld.empty())
        {
            SDL_Rect r = mPartyView ? partyCard(mSlotCursor) : gridCell(mSlotCursor);
            drawHeld(int(mHighlight.x) + r.w / 2, int(mHighlight.y) - 2);
        }
        if (mMenu.open)
            drawMenu();
        if (mSearch.open)
        {
            drawSearch();
            return;
        }

        // Left of the footer: whether there are edits waiting to be written
        if (hasUnsaved())
        {
            const char* label = tr("Alterações não gravadas");
            int bw = mGfx.textWidth(label, 18) + 40;
            mGfx.fillRoundRect(MARGIN + 6, FOOTER_Y + 20, bw, 32, 16, Color{230, 140, 40, 70});
            mGfx.fillCircle(MARGIN + 22, FOOTER_Y + 36, 5, Color{255, 170, 60});
            mGfx.text(label, MARGIN + 34, FOOTER_Y + 25, 18, Color{255, 210, 160});
        }
        std::vector<Hint> hints;
        if (!mHeld.empty())
        {
            hints.push_back({HidNpadButton_A, glyph::A, mHeldClone ? tr("Colar aqui") : tr("Soltar aqui")});

            hints.push_back({HidNpadButton_B, glyph::B, tr("Cancelar")});
        }
        else if (mMenu.open)
        {
            hints.push_back({HidNpadButton_A, glyph::A, "OK"});
            hints.push_back({HidNpadButton_B, glyph::B, tr("Fechar")});
        }
        else
        {
            if (hasUnsaved())
                hints.push_back({HidNpadButton_ZL, glyph::ZL, tr("Gravar")});
            hints.push_back({HidNpadButton_A, glyph::A, tr("Opções")});
            if (selectedPokemon())
                hints.push_back({HidNpadButton_ZR, glyph::ZR,
                    mMarked.empty() ? tr("Marcar") : tr("Marcar (") + std::to_string(mMarked.size()) + ")"});
            hints.push_back({HidNpadButton_B, glyph::B,
                !mMarked.empty() ? tr("Desmarcar") : (currentView() != View::Boxes ? tr("Caixas") : tr("Voltar"))});
        }
        drawFooter(hints);
    }

    // ------------------------------------------------------------------ detail media

    void App::updateDetailMedia(const pksm::PKX& pk)
    {
        // The cry is the only thing fetched from the internet; everything else is in the app
        std::string id = mConfig.downloadMedia && !pk.egg()
                             ? Media::showdownId(i18n::species(LANG, pk.species()))
                             : "";
        if (id != mCryId)
        {
            mCryId     = id;
            mCryWanted = false;
            mCryTime   = -10;
            if (!mCryId.empty())
            {
                std::string path;
                mMedia.cry(mCryId, path); // fetch early so a tap plays at once
            }
        }
        if (mCryWanted)
        {
            std::string path;
            Media::State st = mMedia.cry(mCryId, path);
            if (st == Media::State::Ready)
            {
                mMedia.play(path);
                mCryTime   = mGfx.time();
                mCryWanted = false;
            }
            else if (st == Media::State::Failed)
            {
                mCryWanted = false;
            }
        }
    }

    void App::playCry(void)
    {
        if (mCryId.empty())
            return;
        std::string path;
        switch (mMedia.cry(mCryId, path))
        {
            case Media::State::Ready:
                mMedia.play(path);
                mCryTime = mGfx.time();
                break;
            case Media::State::Pending:
                mCryWanted = true; // plays as soon as it arrives
                break;
            default:
                break; // no internet and not downloaded before: no cry
        }
    }

    void App::drawDetailSprite(const pksm::PKX& pk, int x, int y, int w, int h, float swapTime)
    {
        float now   = mGfx.time();
        float entry = 0.8f + 0.2f * easeOutBack(swapTime / 0.4f);
        float o     = mGfx.opacity();
        mGfx.setOpacity(o * clamp01(swapTime / 0.15f));

        // The cry: a squash and stretch with a little shake, and rings going out
        float c   = now - mCryTime;
        float sx  = 1, sy = 1;
        int shake = 0;
        if (c >= 0 && c < 0.9f)
        {
            float decay = 1 - c / 0.9f;
            float wave  = std::sin(c * 26) * decay;
            sx    = 1 + 0.10f * wave;
            sy    = 1 - 0.08f * wave;
            shake = int(std::sin(c * 60) * 4 * decay);
            for (int r = 0; r < 2; r++)
            {
                float rc = c - r * 0.18f;
                if (rc < 0 || rc > 0.6f)
                    continue;
                int radius = int(60 + rc * 260);
                mGfx.roundRect(x + w / 2 - radius, y + h / 2 - radius, radius * 2, radius * 2, radius,
                    Color{255, 255, 255, u8(110 * (1 - rc / 0.6f))}, 3);
            }
        }

        float bob = std::sin(now * 1.8f) * 4;
        int size  = int(std::min(w, h) * 0.92f * entry);
        int dw = int(size * sx), dh = int(size * sy);
        drawSprite(mGfx, homeSprite(mGfx, pk), x + (w - dw) / 2 + shake, y + (h - dh) / 2 + int(bob), dw, dh);
        mGfx.setOpacity(o);
    }

    // ------------------------------------------------------------------ detail view

    void App::drawRadar(const pksm::PKX& pk, int cx, int cy, int radius, float grow)
    {
        // Rings, then EVs (blue) under IVs (cyan); HP at the top, clockwise
        auto point = [&](int k, float r) {
            float a = -float(M_PI) / 2 + k * float(M_PI) / 3;
            return SDL_FPoint{cx + std::cos(a) * r, cy + std::sin(a) * r};
        };
        for (int ring = 3; ring >= 1; ring--)
        {
            SDL_FPoint pts[6];
            for (int k = 0; k < 6; k++)
                pts[k] = point(k, radius * ring / 3.0f);
            mGfx.polygon(pts, 6, Color{255, 255, 255, u8(ring == 3 ? 16 : 12)});
        }
        // Spokes as thin triangles
        for (int k = 0; k < 6; k++)
        {
            SDL_FPoint e = point(k, float(radius));
            float a      = -float(M_PI) / 2 + k * float(M_PI) / 3 + float(M_PI) / 2;
            float ox = std::cos(a) * 0.8f, oy = std::sin(a) * 0.8f;
            mGfx.triangle({cx + ox, cy + oy}, {cx - ox, cy - oy}, e, Color{255, 255, 255, 40});
        }
        SDL_FPoint evPts[6], ivPts[6];
        for (int k = 0; k < 6; k++)
        {
            const auto& s = STATS[RADAR_ORDER[k]];
            float ev = SDL_min(pk.ev(s.stat), 252) / 252.0f;
            float iv = pk.iv(s.stat) / 31.0f;
            evPts[k] = point(k, radius * (0.06f + 0.94f * ev) * grow);
            ivPts[k] = point(k, radius * (0.06f + 0.94f * iv) * grow);
        }
        mGfx.polygon(evPts, 6, theme::EV_BAR.withAlpha(110));
        mGfx.polygon(ivPts, 6, theme::CYAN_A.withAlpha(120));
        for (int k = 0; k < 6; k++)
            mGfx.fillCircle(int(ivPts[k].x), int(ivPts[k].y), 4, theme::CYAN_A);

        // Labels outside each corner: short name and IV
        for (int k = 0; k < 6; k++)
        {
            const auto& s = STATS[RADAR_ORDER[k]];
            SDL_FPoint lp = point(k, radius + 30.0f);
            u8 iv         = pk.iv(s.stat);
            int effect    = natureEffect(pk, s.stat);
            Color lc = effect > 0 ? Color{255, 140, 120} : (effect < 0 ? Color{130, 170, 255} : theme::MUTED);
            mGfx.text(tr(s.shortLabel), int(lp.x), int(lp.y) - 20, 16, lc, Align::Center);
            mGfx.text(std::to_string(iv), int(lp.x), int(lp.y) - 1, 18, iv == 31 ? theme::GOLD : theme::TEXT,
                Align::Center);
        }
    }

    void App::drawDetail(void)
    {
        const pksm::PKX* pk = mEditing && mEditPk ? mEditPk.get() : selectedPokemon();
        float t  = mGfx.time() - mDetailTime;
        float in = easeOut(t / DETAIL_TIME);

        // Blurred, darkened copy of the box screen behind the card
        if (mBlurred)
            mGfx.draw(mBlurred, 0, 0, SCREEN_W, SCREEN_H);
        mGfx.fillRect(0, 0, SCREEN_W, SCREEN_H, Color{8, 9, 16, u8(150 * in)});
        mHintAreas.clear();
        if (!pk)
        {
            if (mPicker.open)
            {
                constexpr int X = DX, Y = DY, W = DW, H = DH;
                mGfx.setOpacity(in);
                mGfx.setOrigin(0, int((1 - in) * 30));
                mGfx.shadow(X, Y + 8, W, H, 24, 28, Color{0, 0, 0, 160});
                mGfx.fillRoundRect(X, Y, W, H, 24, Color{34, 36, 50, 250});
                mGfx.draw(mWatermark, X + 70, Y + 130, 240, 240, 60);
                bool importing = mPicker.kind == PickerKind::ImportFile;
                mGfx.text(importing ? tr("Importar Pokémon") : tr("Novo Pokémon"), X + 190, Y + 400, 30, theme::TEXT,
                    Align::Center);
                mGfx.textWrapped(importing ? tr("Arquivos de sdmc:/switch/monhex/pkm e sdmc:/pkm. Pokémon de outros "
                                             "jogos são convertidos quando possível.")
                                           : tr("Escolha a espécie. Depois dá para ajustar nível, golpes, origem e tudo mais."),
                    X + 190, Y + 446, 18, theme::MUTED, Align::Center, 320, 3, 26);
                drawPicker();
                mGfx.resetLayer();
                drawFooter({{HidNpadButton_Y, glyph::Y, tr("Buscar")}, {HidNpadButton_A, glyph::A, tr("Escolher")},
                    {HidNpadButton_B, glyph::B, tr("Cancelar")}});
            }
            return;
        }

        constexpr int X = DX, Y = DY, W = DW, H = DH;
        updateDetailMedia(*pk);
        mGfx.setOpacity(in);
        mGfx.setOrigin(0, int((1 - in) * 30));
        bool shiny = pk->shiny() && !pk->egg();
        Color main = pk->egg() ? theme::DIM : (shiny ? theme::GOLD : typeColor(pk->type1()));
        mGfx.shadow(X, Y + 8, W, H, 24, 28, Color{0, 0, 0, 160});
        mGfx.fillRoundRect(X, Y, W, H, 24, Color{34, 36, 50, 250});
        mGfx.fillRoundRect(X + 30, Y, W - 60, 5, 2, main);

        // Content re-animates when stepping to another Pokemon
        float ts = mGfx.time() - mDetailSwapTime;

        // Left: big render on its stage, identity below
        if (shiny)
            drawShinyStage(SX, SY, SW, SH, 20);
        else
        {
            mGfx.fillRoundRect(SX, SY, SW, SH, 20, lerp(theme::STAGE, main, 0.24f));
            mGfx.shadow(SX + 90, SY + 70, SW - 180, SH - 140, 60, 60, main.withAlpha(80));
        }
        drawDetailSprite(*pk, SX, SY, SW, SH, ts);

        int lx = SX, ly = SY + SH + 18;
        std::string name = pk->egg() ? tr("Ovo") : pk->nickname();
        int nx = lx + mGfx.text(name, lx, ly, 32, theme::TEXT, Align::Left, SW - 70);
        if (!pk->egg() && pk->gender() != pksm::Gender::Genderless)
        {
            bool female = pk->gender() == pksm::Gender::Female;
            std::string sym = mGfx.hasGlyph(female ? 0x2640 : 0x2642, 28) ? (female ? "♀" : "♂") : (female ? "F" : "M");
            nx += 10 + mGfx.text(sym, nx + 10, ly + 2, 28, female ? theme::FEMALE : theme::MALE);
        }
        if (shiny)
            drawShinyMark(nx + 26, ly + 20, 30);
        mGfx.text(i18n::species(LANG, pk->species()) + "  ·  " + tr("Nv. ") + std::to_string(pk->level()), lx, ly + 42,
            20, theme::MUTED, Align::Left, SW);
        if (!pk->egg())
        {
            int bx = lx + typeBadge(mGfx, u8(pk->type1()), lx, ly + 76, 30, 17) + 8;
            if (pk->type2() != pk->type1())
                typeBadge(mGfx, u8(pk->type2()), bx, ly + 76, 30, 17);
        }
        if (SDL_Texture* ball = ballSprite(mGfx, *pk))
            mGfx.draw(ball, lx, ly + 120, 24, 24);
        mGfx.text(i18n::ball(LANG, pk->ball()), lx + 32, ly + 121, 18, theme::MUTED, Align::Left, SW - 32);
        char ids[96];
        snprintf(ids, sizeof(ids), "%s  ·  ID %06lu", pk->otName().c_str(), (unsigned long)pk->formatTID());
        mGfx.text(ids, lx, ly + 150, 18, theme::DIM, Align::Left, SW);

        if (mEditing)
        {
            drawEditor();
            mGfx.resetLayer();
            if (mPicker.open)
            {
                std::vector<Hint> hints = {{HidNpadButton_Y, glyph::Y, tr("Buscar")}};
                if (mPicker.allowNumber)
                    hints.push_back({HidNpadButton_X, glyph::X, tr("Digitar número")});
                hints.push_back({HidNpadButton_A, glyph::A, tr("Escolher")});
                hints.push_back({HidNpadButton_B, glyph::B, tr("Voltar")});
                drawFooter(hints);
            }
            else
            {
                drawFooter({{0, glyph::DLEFT, tr("Mudar")}, {HidNpadButton_A, glyph::A, tr("Editar")},
                    {0, glyph::R, "±10"}, {HidNpadButton_X, glyph::X, mCreating ? tr("Cancelar") : tr("Descartar")},
                    {HidNpadButton_B, glyph::B, mCreating ? tr("Criar") : tr("Concluir")}});
            }
            if (mNatureChart)
                drawNatureChart();
            return;
        }

        // Middle: IV/EV radar and nature, ability, item, origin
        int mx = X + 420, mw = 330;
        mGfx.text(tr("IVs e EVs"), mx, Y + 28, 18, theme::DIM);
        mGfx.fillCircle(mx + mw - 104, Y + 39, 6, theme::CYAN_A);
        mGfx.text("IV", mx + mw - 92, Y + 29, 16, theme::MUTED);
        mGfx.fillCircle(mx + mw - 44, Y + 39, 6, theme::EV_BAR);
        mGfx.text("EV", mx + mw - 32, Y + 29, 16, theme::MUTED);
        drawRadar(*pk, mx + mw / 2, Y + 200, 100, easeOut((ts - 0.05f) / 0.5f));

        struct Field
        {
            const char* label;
            std::string value;
        };
        u16 item = pk->heldItem();
        Field fields[] = {{tr("Natureza"), natureName(u8(pk->nature()))},
            {tr("Habilidade"), i18n::ability(LANG, pk->ability())},
            {tr("Item"), item ? i18n::item(LANG, item) : tr("Nenhum")},
            {tr("Origem"), i18n::game(LANG, pk->version())}};
        for (int i = 0; i < 4; i++)
        {
            int fx = mx + (i % 2) * (mw / 2), fy = Y + 392 + (i / 2) * 52;
            mGfx.text(fields[i].label, fx, fy, 15, theme::DIM);
            int vx = fx;
            if (i == 2 && item)
            {
                mGfx.draw(itemIcon(mGfx, item, i18n::item(pksm::Language::ENG, item)), fx - 2, fy + 16, 28, 28);
                vx += 30;
            }
            mGfx.text(fields[i].value, vx, fy + 19, 19, theme::TEXT, Align::Left, mw / 2 - 10 - (vx - fx));
        }
        const std::string& abilityText = abilityDescription(u16(pk->ability()));
        if (!abilityText.empty())
            mGfx.textWrapped(abilityText, mx, Y + 498, 15, theme::DIM, Align::Left, mw, 3, 20);

        // Right: full stat table, then the moves in detail
        int rx = X + 780, rw = W - 780 - 28;
        int cBase = rx + 170, cIv = rx + 214, cEv = rx + 262, cVal = rx + rw;
        static const char* SHORT[] = {"PS", "Ataque", "Defesa", "At. Esp.", "Def. Esp.", "Veloc."};
        mGfx.text(tr("Status"), rx + 20, Y + 28, 15, theme::DIM);
        mGfx.text(tr("Base"), cBase, Y + 28, 15, theme::DIM, Align::Right);
        mGfx.text("IV", cIv, Y + 28, 15, theme::DIM, Align::Right);
        mGfx.text("EV", cEv, Y + 28, 15, theme::DIM, Align::Right);
        mGfx.text(tr("Valor"), cVal, Y + 28, 15, theme::DIM, Align::Right);
        float count = easeOut((ts - 0.08f) / 0.45f);
        int total   = 0;
        for (int i = 0; i < 6; i++)
        {
            int ry     = Y + 52 + i * 30;
            const auto& s = STATS[i];
            int effect = natureEffect(*pk, s.stat);
            Color lc = effect > 0 ? Color{255, 140, 120} : (effect < 0 ? Color{130, 170, 255} : theme::TEXT);
            u8 iv = pk->iv(s.stat);
            int value = pk->stat(s.stat);
            total += value;
            if (i % 2 == 0)
                mGfx.fillRoundRect(rx - 8, ry - 3, rw + 16, 28, 8, Color{255, 255, 255, 8});
            if (effect > 0)
                mGfx.triangle({rx + 1.0f, ry + 16.0f}, {rx + 13.0f, ry + 16.0f}, {rx + 7.0f, ry + 6.0f}, lc);
            else if (effect < 0)
                mGfx.triangle({rx + 1.0f, ry + 7.0f}, {rx + 13.0f, ry + 7.0f}, {rx + 7.0f, ry + 17.0f}, lc);
            mGfx.text(tr(SHORT[i]), rx + 20, ry, 17, lc);
            mGfx.text(std::to_string(baseStat(*pk, s.stat)), cBase, ry, 17, theme::MUTED, Align::Right);
            mGfx.text(std::to_string(int(iv * count + 0.5f)), cIv, ry, 17, iv == 31 ? theme::GOLD : theme::TEXT,
                Align::Right);
            mGfx.text(std::to_string(int(pk->ev(s.stat) * count + 0.5f)), cEv, ry, 17, theme::TEXT, Align::Right);
            mGfx.text(std::to_string(int(value * count + 0.5f)), cVal, ry, 18, theme::TEXT, Align::Right);
        }
        mGfx.text(tr("Total"), rx + 20, Y + 52 + 6 * 30, 17, theme::MUTED);
        mGfx.text(std::to_string(int(total * count + 0.5f)), cVal, Y + 52 + 6 * 30, 18, theme::TEXT, Align::Right);
        int myBase = Y + 312;
        for (int i = 0; i < 4; i++)
        {
            float mp = easeOut((ts - 0.12f - 0.07f * i) / 0.32f);
            float o  = mGfx.opacity();
            mGfx.setOpacity(o * mp);
            int my = myBase + i * 60;
            int ox = int((1 - mp) * 40);
            pksm::Move move = pk->move(static_cast<u8>(i));
            if (move == pksm::Move::None)
            {
                mGfx.roundRect(rx - 8 + ox, my, rw + 16, 52, 12, theme::LINE, 1);
                mGfx.text("—", rx + rw / 2 + ox, my + (52 - mGfx.textHeight(18)) / 2, 18, theme::DIM, Align::Center);
                mGfx.setOpacity(o);
                continue;
            }
            MoveDetails info = moveDetails(move);
            Color tc = info.known ? typeColor(info.type) : theme::DIM;
            mGfx.fillRoundRect(rx - 8 + ox, my, rw + 16, 52, 12, lerp(theme::STAGE, tc, 0.32f));
            if (info.known)
                typeCircle(mGfx, info.type, rx + 14 + ox, my + 26, 15);
            int nameX = rx + 38 + ox;
            mGfx.text(i18n::move(LANG, move), nameX, my + 5, 19, theme::TEXT, Align::Left, rw - 140);
            char extra[64];
            if (info.known)
            {
                snprintf(extra, sizeof(extra), tr("Poder %s  ·  Prec. %s  ·  PP %u/%u"),
                    info.power ? std::to_string(info.power).c_str() : "—",
                    info.accuracy ? (std::to_string(info.accuracy) + "%").c_str() : "—", pk->PP(u8(i)),
                    pk->maxPP(u8(i)));
            }
            else
            {
                snprintf(extra, sizeof(extra), "PP %u/%u", pk->PP(u8(i)), pk->maxPP(u8(i)));
            }
            mGfx.text(extra, nameX, my + 29, 14, theme::MUTED, Align::Left, rw - 50);
            if (info.known)
            {
                const Color cc = CATEGORY_COLOR[info.category];
                int cw = mGfx.textWidth(tr(CATEGORY[info.category]), 14) + 16;
                int cx = rx + rw - cw + ox;
                mGfx.fillRoundRect(cx, my + 6, cw, 22, 11, cc.withAlpha(60));
                mGfx.text(tr(CATEGORY[info.category]), cx + cw / 2, my + 6 + (22 - mGfx.textHeight(14)) / 2, 14, cc,
                    Align::Center);
            }
            mGfx.setOpacity(o);
        }
        mGfx.resetLayer();

        drawFooter({{0, glyph::DLEFT, tr("Anterior / Próximo")}, {HidNpadButton_X, glyph::X, tr("Editar")},
            {HidNpadButton_B, glyph::B, tr("Fechar")},
            {HidNpadButton_Plus, glyph::PLUS, tr("Sair")}});
    }
}
