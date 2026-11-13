// Editing and creating Pokemon, saving the changes to the console, and the
// dialogs and notices that go with it
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <random>

#include "app.hpp"
#include "lang.hpp"
#include "pkx/PK9.hpp"
#include "theme.hpp"
#include "ui/items.hpp"
#include "ui/sprites.hpp"
#include "ui/types.hpp"
#include "utils/DateTime.hpp"
#include "utils/i18n.hpp"

using namespace monhex::ui;
using namespace monhex::layout;

namespace monhex
{
    namespace
    {
        enum class Field
        {
            Species,
            Nickname,
            Level,
            Gender,
            Shiny,
            Nature,
            Ability,
            Item,
            Ball,
            Friendship,
            Move,
            Iv,
            Ev,
            OtName,
            OtGender,
            Tid,
            Sid,
            Version,
            MetLocation,
            MetLevel,
            MetDate,
            Hatched,
            EggLocation,
            EggDate,
            Fateful
        };

        struct EditRow
        {
            Field field;
            int index;           // which move or stat
            const char* section; // starts a new section when set
        };

        struct StatName
        {
            pksm::Stat stat;
            const char* label;
        };
        constexpr StatName STAT_NAMES[] = {{pksm::Stat::HP, "PS"}, {pksm::Stat::ATK, "Ataque"},
            {pksm::Stat::DEF, "Defesa"}, {pksm::Stat::SPATK, "At. Esp."}, {pksm::Stat::SPDEF, "Def. Esp."},
            {pksm::Stat::SPD, "Velocidade"}};

        constexpr int ROW_H = 34, SECTION_H = 34, PICK_ROW_H = 40, DESC_H = 80;

        std::mt19937& rng(void)
        {
            static std::mt19937 gen{static_cast<u32>(armGetSystemTick())};
            return gen;
        }

        int randomInt(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng()); }

        int totalEvs(const pksm::PKX& pk)
        {
            int total = 0;
            for (const auto& s : STAT_NAMES)
                total += pk.ev(s.stat);
            return total;
        }

        bool hatched(const pksm::PKX& pk) { return pk.eggLocation() != 0; }

        std::vector<EditRow> buildRows(const pksm::PKX& pk)
        {
            std::vector<EditRow> rows = {{Field::Species, 0, tr("Geral")}, {Field::Nickname, 0, nullptr},
                {Field::Level, 0, nullptr}, {Field::Gender, 0, nullptr}, {Field::Shiny, 0, nullptr},
                {Field::Nature, 0, nullptr}, {Field::Ability, 0, nullptr}, {Field::Item, 0, nullptr},
                {Field::Ball, 0, nullptr}, {Field::Friendship, 0, nullptr}};
            for (int i = 0; i < 4; i++)
                rows.push_back({Field::Move, i, i == 0 ? tr("Golpes") : nullptr});
            for (int i = 0; i < 6; i++)
                rows.push_back({Field::Iv, i, i == 0 ? tr("IVs (0 a 31)") : nullptr});
            for (int i = 0; i < 6; i++)
                rows.push_back({Field::Ev, i, i == 0 ? tr("EVs (até 252 cada, 510 no total)") : nullptr});
            rows.push_back({Field::OtName, 0, tr("Origem e encontro")});
            rows.push_back({Field::OtGender, 0, nullptr});
            rows.push_back({Field::Tid, 0, nullptr});
            rows.push_back({Field::Sid, 0, nullptr});
            rows.push_back({Field::Version, 0, nullptr});
            rows.push_back({Field::MetLocation, 0, nullptr});
            rows.push_back({Field::MetLevel, 0, nullptr});
            rows.push_back({Field::MetDate, 0, nullptr});
            rows.push_back({Field::Hatched, 0, nullptr});
            if (hatched(pk))
            {
                rows.push_back({Field::EggLocation, 0, nullptr});
                rows.push_back({Field::EggDate, 0, nullptr});
            }
            rows.push_back({Field::Fateful, 0, nullptr});
            return rows;
        }

        int rowY(const std::vector<EditRow>& rows, int row)
        {
            int y = 0;
            for (int r = 0; r <= row && r < int(rows.size()); r++)
            {
                if (rows[r].section)
                    y += SECTION_H;
                if (r < row)
                    y += ROW_H;
            }
            return y;
        }

        std::string dateText(const Date& d)
        {
            char buf[24];
            snprintf(buf, sizeof(buf), "%02u/%02u/%04u", d.day(), d.month(), d.year());
            return buf;
        }

        // Games that can appear as a Pokemon's origin, newest first
        constexpr u8 VERSIONS[] = {52, 50, 51, 47, 44, 45, 42, 43, 33, 32, 31, 30, 27, 26, 25, 24, 23, 22, 21,
            20, 7, 8, 12, 10, 11, 15, 3, 2, 1, 4, 5, 39, 35, 36, 37, 38, 41, 34};

        // Asks for text with the system keyboard; false if cancelled
        bool keyboard(const char* header, const std::string& initial, int maxLength, bool numbers, std::string& out)
        {
            SwkbdConfig kbd;
            if (R_FAILED(swkbdCreate(&kbd, 0)))
                return false;
            if (numbers)
                swkbdConfigMakePresetDefault(&kbd), swkbdConfigSetType(&kbd, SwkbdType_NumPad);
            else
                swkbdConfigMakePresetDefault(&kbd);
            swkbdConfigSetHeaderText(&kbd, header);
            swkbdConfigSetInitialText(&kbd, initial.c_str());
            swkbdConfigSetStringLenMax(&kbd, maxLength);
            char buf[128] = {};
            Result rc = swkbdShow(&kbd, buf, sizeof(buf));
            swkbdClose(&kbd);
            if (R_FAILED(rc))
                return false;
            out = buf;
            return true;
        }

        bool parseDate(const std::string& s, Date& out)
        {
            unsigned d = 0, m = 0, y = 0;
            if (sscanf(s.c_str(), "%u/%u/%u", &d, &m, &y) != 3 || d < 1 || d > 31 || m < 1 || m > 12 || y < 2000 ||
                y > 2099)
                return false;
            out = Date{u8(d), u8(m), y};
            return true;
        }

        // Gives the Pokemon a gender its species allows
        void fixGender(pksm::PKX& pk)
        {
            u8 ratio = pk.genderType();
            if (ratio == 255)
                pk.gender(pksm::Gender::Genderless);
            else if (ratio == 254)
                pk.gender(pksm::Gender::Female);
            else if (ratio == 0)
                pk.gender(pksm::Gender::Male);
            else if (pk.gender() == pksm::Gender::Genderless)
                pk.gender(randomInt(0, 1) ? pksm::Gender::Female : pksm::Gender::Male);
        }
    }

    // ------------------------------------------------------------------ editing

    void App::startEditing(void)
    {
        const pksm::PKX* pk = selectedPokemon();
        if (!pk || pk->egg())
            return;
        mEditPk      = pk->clone();
        mEditing     = true;
        mCreating    = false;
        mEditChanged = false;
        mEditRow     = 0;
        mEditScroll  = 0;
        mHighlight.snap();
    }

    void App::createPokemon(pksm::Species species)
    {
        const pksm::Sav& sav = *mSave.sav;
        auto pk = sav.emptyPkm();
        pk->species(species);
        pk->alternativeForm(0);
        pk->egg(false);
        pk->level(5);
        pk->PID(u32(rng()()));
        pk->encryptionConstant(u32(rng()()));
        pk->nature(pksm::Nature{u8(randomInt(0, 24))});
        pk->setAbility(0);
        pk->gender(pksm::Gender::Genderless);
        fixGender(*pk);
        for (const auto& s : STAT_NAMES)
        {
            pk->iv(s.stat, u8(randomInt(0, 31)));
            pk->ev(s.stat, 0);
        }
        pk->ball(pksm::Ball::Poke);
        pk->heldItem(u16(0));
        pk->move(0, pksm::Move::Tackle);
        for (u8 i = 1; i < 4; i++)
            pk->move(i, pksm::Move::None);
        pk->healPP();
        pk->otName(sav.otName());
        pk->otGender(sav.gender());
        pk->TID(sav.TID());
        pk->SID(sav.SID());
        pk->language(sav.language());
        pk->version(sav.version());
        pk->otFriendship(pk->baseFriendship());
        pk->metLevel(5);
        pk->metDate(Date::today());
        pk->metLocation(0);
        pk->eggLocation(0);
        pk->nickname(i18n::species(LANG, species));
        pk->nicknamed(false);
        if (sav.version() == pksm::GameVersion::SL || sav.version() == pksm::GameVersion::VL)
        {
            auto* pk9 = static_cast<pksm::PK9*>(pk.get());
            pk9->teraTypeOriginal(u8(pk->type1()));
            pk9->teraTypeOverride(19); // none: use the original
        }
        pk->refreshChecksum();

        mEditPk      = std::move(pk);
        mEditing     = true;
        mCreating    = true;
        mEditChanged = true;
        mEditRow     = 0;
        mEditScroll  = 0;
        if (!mDetail)
            openDetail();
        mDetailSwapTime = mGfx.time();
    }

    void App::finishEditing(bool keep)
    {
        bool placed = false;
        if (keep && mEditChanged && mEditPk)
        {
            if (mEditPk->isParty())
                mEditPk->updatePartyData();
            mEditPk->refreshChecksum();
            if (mPartyView)
            {
                // The party has no gaps: a new member goes after the last one
                int count = mSave.sav->partyCount();
                int slot  = mCreating ? std::min(count, 5) : mSlotCursor;
                mSave.sav->pkm(*mEditPk, static_cast<u8>(slot));
                if (mCreating && count < 6)
                    mSave.sav->partyCount(static_cast<u8>(count + 1));
                mSlotCursor = slot;
            }
            else
            {
                writeSlot(currentSlot(), *mEditPk);
            }
            loadSlots();
            placed = true;
            if (!mBankView)
                mDirty = true;
            showToast(mCreating ? tr("Pokémon criado. Aperte ZL nas caixas para gravar.")
                                : tr("Alterações aplicadas. Aperte ZL nas caixas para gravar."));
        }
        if (mCreating && !placed)
            mDetail = false; // the slot is still empty: nothing to show
        mEditing  = false;
        mCreating = false;
        mEditPk.reset();
        mSummaryKey = -1; // replay the summary animation with the new data
        mHighlight.snap();
    }

    void App::openPicker(PickerKind kind, int index)
    {
        const pksm::Sav& sav = *mSave.sav;
        mPicker       = {};
        mPicker.kind  = kind;
        mPicker.index = index;
        mPicker.time  = mGfx.time();
        mPicker.area  = editorArea();
        int current   = -1;
        switch (kind)
        {
            case PickerKind::Species:
            case PickerKind::CreateSpecies:
                mPicker.title = tr("Espécie");
                for (const auto& s : sav.availableSpecies())
                {
                    if (s == pksm::Species::None)
                        continue;
                    PickerItem entry{int(u16(s)), i18n::species(LANG, s), 255};
                    entry.iconSpecies = int(u16(s));
                    entry.badgeNumber = int(u16(s));
                    mPicker.items.push_back(entry);
                }
                if (mEditPk && kind == PickerKind::Species)
                    current = u16(mEditPk->species());
                break;
            case PickerKind::Move:
                mPicker.title = tr("Golpe ") + std::to_string(index + 1);
                mPicker.items.push_back({0, tr("(nenhum)"), 255});
                for (const auto& m : sav.availableMoves())
                {
                    if (m == pksm::Move::None)
                        continue;
                    MoveDetails info = moveDetails(m);
                    mPicker.items.push_back({int(u16(m)), i18n::move(LANG, m), info.known ? info.type : u8(255)});
                }
                current = u16(mEditPk->move(u8(index)));
                break;
            case PickerKind::Item:
                mPicker.title = tr("Item");
                mPicker.items.push_back({0, tr("(nenhum)"), 255});
                for (int id : sav.availableItems())
                {
                    const std::string& name = i18n::item(LANG, u16(id));
                    if (id > 0 && !name.empty() && name != "???" && name.find("(?)") == std::string::npos)
                        mPicker.items.push_back({id, name, 255, id});
                }
                current = mEditPk->heldItem();
                break;
            case PickerKind::Ball:
                mPicker.title = tr("Poké Ball");
                for (const auto& b : sav.availableBalls())
                {
                    PickerItem entry{int(u8(b)), i18n::ball(LANG, b), 255};
                    entry.iconBall = int(u8(b));
                    mPicker.items.push_back(entry);
                }
                current = u8(mEditPk->ball());
                break;
            case PickerKind::MetLocation:
            case PickerKind::EggLocation:
            {
                mPicker.title = kind == PickerKind::MetLocation ? tr("Local do encontro") : tr("Local do ovo");
                mPicker.allowNumber = true;
                for (const auto& [id, name] : i18n::rawLocations(LANG, sav.generation()))
                {
                    if (!name.empty() && name.find("——") == std::string::npos)
                        mPicker.items.push_back({int(id), name, 255});
                }
                current = kind == PickerKind::MetLocation ? mEditPk->metLocation() : mEditPk->eggLocation();
                break;
            }
            case PickerKind::ImportFile:
            case PickerKind::BagItem:
                break;
            case PickerKind::Version:
                mPicker.title = tr("Jogo de origem");
                for (u8 v : VERSIONS)
                {
                    const std::string& name = i18n::game(LANG, pksm::GameVersion{v});
                    if (!name.empty())
                        mPicker.items.push_back({v, name, 255});
                }
                current = u8(mEditPk->version());
                break;
        }
        filterPicker();
        for (size_t i = 0; i < mPicker.filtered.size(); i++)
        {
            if (mPicker.items[mPicker.filtered[i]].id == current)
                mPicker.cursor = int(i);
        }
        mPicker.open = true;
    }

    void App::filterPicker(void)
    {
        auto lower = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
            return s;
        };
        std::string q = lower(mPicker.query);
        mPicker.filtered.clear();
        for (size_t i = 0; i < mPicker.items.size(); i++)
        {
            const auto& it = mPicker.items[i];
            if (q.empty() || lower(it.name).find(q) != std::string::npos ||
                (it.badgeNumber >= 0 && std::to_string(it.badgeNumber).find(q) != std::string::npos))
                mPicker.filtered.push_back(int(i));
        }
        mPicker.cursor = 0;
        mPicker.scroll = 0;
    }

    void App::choosePicked(int id)
    {
        pksm::PKX* pk = mEditPk.get();
        switch (mPicker.kind)
        {
            case PickerKind::CreateSpecies:
                mPicker.open = false;
                createPokemon(pksm::Species{u16(id)});
                return;
            case PickerKind::ImportFile:
                mPicker.open = false;
                importFile(mImportFiles[size_t(id)]);
                return;
            case PickerKind::BagItem:
                mPicker.open = false;
                pickBagItem(id);
                return;
            case PickerKind::Species:
            {
                bool renamed = pk->nicknamed();
                pk->species(pksm::Species{u16(id)});
                pk->alternativeForm(0);
                pk->setAbility(pk->abilityNumber() == 4 ? 2 : (pk->abilityNumber() == 2 ? 1 : 0));
                fixGender(*pk);
                if (!renamed)
                    pk->nickname(i18n::species(LANG, pk->species()));
                break;
            }
            case PickerKind::Move:
                pk->move(u8(mPicker.index), pksm::Move{u16(id)});
                pk->healPP(u8(mPicker.index));
                break;
            case PickerKind::Item:
                pk->heldItem(u16(id));
                break;
            case PickerKind::Ball:
                pk->ball(pksm::Ball{u8(id)});
                break;
            case PickerKind::MetLocation:
                pk->metLocation(u16(id));
                break;
            case PickerKind::EggLocation:
                pk->eggLocation(u16(id));
                break;
            case PickerKind::Version:
                pk->version(pksm::GameVersion{u8(id)});
                break;
        }
        mEditChanged = true;
        mPicker.open = false;
    }

    void App::updatePicker(u64 pressed)
    {
        SDL_Rect area = mPicker.area;
        int listY     = area.y + 56;
        int visible   = (area.h - 56) / PICK_ROW_H;
        int count     = int(mPicker.filtered.size());
        if (pressed & HidNpadButton_B)
        {
            bool standalone = mPicker.kind == PickerKind::CreateSpecies || mPicker.kind == PickerKind::ImportFile;
            mPicker.open    = false;
            if (standalone)
                mDetail = false;
            return;
        }
        if ((pressed & HidNpadButton_Y) || mInput.tappedIn(area.x, area.y, area.w, 44))
        {
            std::string q;
            if (keyboard(tr("Buscar"), mPicker.query, 24, false, q))
            {
                mPicker.query = q;
                filterPicker();
            }
            return;
        }
        if ((pressed & HidNpadButton_X) && mPicker.allowNumber)
        {
            std::string n;
            if (keyboard(tr("Número do local"), "", 5, true, n) && !n.empty())
                choosePicked(std::atoi(n.c_str()));
            return;
        }
        if (pressed & KEY_DOWN)
            mPicker.cursor = std::min(mPicker.cursor + 1, count - 1);
        if (pressed & KEY_UP)
            mPicker.cursor = std::max(mPicker.cursor - 1, 0);
        if (pressed & (HidNpadButton_R | KEY_RIGHT))
            mPicker.cursor = std::min(mPicker.cursor + visible, count - 1);
        if (pressed & (HidNpadButton_L | KEY_LEFT))
            mPicker.cursor = std::max(mPicker.cursor - visible, 0);
        for (int i = 0; i < visible && mPicker.scroll + i < count; i++)
        {
            if (mInput.tappedIn(area.x, listY + i * PICK_ROW_H, area.w, PICK_ROW_H))
            {
                if (mPicker.scroll + i == mPicker.cursor)
                    pressed |= HidNpadButton_A;
                mPicker.cursor = mPicker.scroll + i;
            }
        }
        if (mPicker.cursor < mPicker.scroll)
            mPicker.scroll = mPicker.cursor;
        if (mPicker.cursor >= mPicker.scroll + visible)
            mPicker.scroll = mPicker.cursor - visible + 1;
        if ((pressed & HidNpadButton_A) && count > 0 && mPicker.cursor >= 0)
            choosePicked(mPicker.items[mPicker.filtered[mPicker.cursor]].id);
    }

    void App::drawPicker(void)
    {
        SDL_Rect area = mPicker.area;
        float in      = easeOut((mGfx.time() - mPicker.time) / 0.2f);
        float o       = mGfx.opacity();
        mGfx.setOpacity(o * in);
        mGfx.fillRoundRect(area.x - 10, area.y - 10, area.w + 20, area.h + 20, 16, Color{30, 32, 46, 252});
        // Search bar
        mGfx.fillRoundRect(area.x, area.y, area.w, 44, 22, Color{255, 255, 255, 20});
        std::string search = mPicker.query.empty() ? tr("Buscar ") + mPicker.title + "..." : mPicker.query;
        mGfx.buttonGlyph(glyph::Y, area.x + 14, area.y + 9, 24, theme::MUTED);
        mGfx.text(search, area.x + 50, area.y + (44 - mGfx.textHeight(20)) / 2, 20,
            mPicker.query.empty() ? theme::DIM : theme::TEXT, Align::Left, area.w - 170);
        mGfx.text(std::to_string(mPicker.filtered.size()) +
                      (mPicker.kind == PickerKind::Species || mPicker.kind == PickerKind::CreateSpecies ? tr(" espécies")
                                                                                                         : tr(" itens")), area.x + area.w - 18,
            area.y + (44 - mGfx.textHeight(16)) / 2, 16, theme::DIM, Align::Right);

        int listY   = area.y + 56;
        int visible = (area.h - 56) / PICK_ROW_H;
        int count   = int(mPicker.filtered.size());
        if (count == 0)
            mGfx.text(tr("Nada encontrado."), area.x + area.w / 2, listY + 40, 20, theme::DIM, Align::Center);
        for (int i = 0; i < visible && mPicker.scroll + i < count; i++)
        {
            int index = mPicker.scroll + i;
            const auto& item = mPicker.items[mPicker.filtered[index]];
            int y    = listY + i * PICK_ROW_H;
            bool sel = index == mPicker.cursor;
            mGfx.fillRoundRect(area.x, y + 2, area.w, PICK_ROW_H - 4, 10,
                sel ? Color{0, 190, 255, 60} : Color{255, 255, 255, u8(i % 2 ? 6 : 12)});
            int tx = area.x + 16;
            if (item.type < 18)
            {
                typeCircle(mGfx, item.type, area.x + 22, y + PICK_ROW_H / 2, 11);
                tx = area.x + 42;
            }
            else if (item.iconSpecies >= 0)
            {
                drawSprite(mGfx, speciesSprite(mGfx, u16(item.iconSpecies)), area.x + 4, y + 1, PICK_ROW_H + 4,
                    PICK_ROW_H - 2);
                tx = area.x + PICK_ROW_H + 14;
            }
            else if (item.iconBall >= 0)
            {
                int bw = 0, bh = 0;
                char path[48];
                snprintf(path, sizeof(path), "romfs:/sprites/_ball%d.png", item.iconBall);
                if (SDL_Texture* ball = mGfx.pixelTexture(path, bw, bh))
                    mGfx.draw(ball, area.x + 8, y + 5, PICK_ROW_H - 10, PICK_ROW_H - 10);
                tx = area.x + PICK_ROW_H + 8;
            }
            else if (item.iconItem >= 0)
            {
                mGfx.draw(itemIcon(mGfx, u16(item.iconItem), i18n::item(pksm::Language::ENG, u16(item.iconItem))),
                    area.x + 6, y + 3, PICK_ROW_H - 6, PICK_ROW_H - 6);
                tx = area.x + PICK_ROW_H + 8;
            }
            int nameRight = area.x + area.w - 16;
            if (item.badgeNumber >= 0)
            {
                char num[12];
                snprintf(num, sizeof(num), "#%04d", item.badgeNumber);
                int bw = mGfx.textWidth(num, 15) + 18;
                int bx = area.x + area.w - 12 - bw, by = y + (PICK_ROW_H - 24) / 2;
                mGfx.fillRoundRect(bx, by, bw, 24, 12, Color{255, 255, 255, u8(sel ? 40 : 22)});
                mGfx.text(num, bx + bw / 2, by + (24 - mGfx.textHeight(15)) / 2, 15, theme::MUTED, Align::Center);
                nameRight = bx - 10;
            }
            mGfx.text(item.name, tx, y + (PICK_ROW_H - mGfx.textHeight(19)) / 2, 19,
                sel ? theme::TEXT : theme::MUTED, Align::Left, nameRight - tx);
        }
        mGfx.setOpacity(o);
    }

    void App::editField(int rowIndex, int delta, bool toLimit, bool activate)
    {
        pksm::PKX& pk = *mEditPk;
        auto rows     = buildRows(pk);
        if (rowIndex < 0 || rowIndex >= int(rows.size()))
            return;
        const EditRow& row = rows[rowIndex];
        auto step = [&](int value, int lo, int hi) {
            if (toLimit)
                return delta > 0 ? hi : lo;
            return std::clamp(value + delta, lo, hi);
        };
        std::string text;
        switch (row.field)
        {
            case Field::Species:
                if (activate || delta)
                    openPicker(PickerKind::Species, 0);
                return;
            case Field::Nickname:
            {
                if (!keyboard(tr("Apelido (vazio = nome da espécie)"), pk.nickname(), 12, false, text))
                    return;
                std::string species = i18n::species(LANG, pk.species());
                bool custom         = !text.empty() && text != species;
                pk.nickname(custom ? text : species);
                pk.nicknamed(custom);
                break;
            }
            case Field::Level:
                pk.level(u8(step(pk.level(), 1, 100)));
                break;
            case Field::Gender:
            {
                u8 ratio = pk.genderType();
                if (ratio == 0 || ratio >= 254)
                    return; // fixed by the species
                pk.gender(pk.gender() == pksm::Gender::Male ? pksm::Gender::Female : pksm::Gender::Male);
                break;
            }
            case Field::Shiny:
                pk.shiny(!pk.shiny());
                break;
            case Field::Nature:
                if (activate)
                {
                    mNatureChart      = true;
                    mNatureCursor     = u8(pk.nature());
                    mNatureChartTime  = mGfx.time();
                    return;
                }
                pk.nature(pksm::Nature{u8((u8(pk.nature()) + (delta < 0 ? 24 : 1)) % 25)});
                break;
            case Field::Ability:
            {
                int slot = pk.abilityNumber() == 1 ? 0 : (pk.abilityNumber() == 2 ? 1 : 2);
                pk.setAbility(u8((slot + (delta < 0 ? 2 : 1)) % 3));
                break;
            }
            case Field::Item:
                openPicker(PickerKind::Item, 0);
                return;
            case Field::Ball:
            {
                if (activate)
                {
                    openPicker(PickerKind::Ball, 0);
                    return;
                }
                const auto& balls = mSave.sav->availableBalls();
                if (balls.empty())
                    return;
                std::vector<pksm::Ball> list(balls.begin(), balls.end());
                auto it   = std::find(list.begin(), list.end(), pk.ball());
                int index = it == list.end() ? 0 : int(it - list.begin());
                index     = (index + (delta < 0 ? int(list.size()) - 1 : 1)) % int(list.size());
                pk.ball(list[index]);
                break;
            }
            case Field::Friendship:
                pk.otFriendship(u8(step(pk.otFriendship(), 0, 255)));
                break;
            case Field::Move:
                openPicker(PickerKind::Move, row.index);
                return;
            case Field::Iv:
            {
                pksm::Stat s = STAT_NAMES[row.index].stat;
                pk.iv(s, u8(step(pk.iv(s), 0, 31)));
                break;
            }
            case Field::Ev:
            {
                pksm::Stat s = STAT_NAMES[row.index].stat;
                int hi       = std::min(252, 510 - (totalEvs(pk) - pk.ev(s)));
                pk.ev(s, u16(step(pk.ev(s), 0, std::max(0, hi))));
                break;
            }
            case Field::OtName:
                if (!keyboard(tr("Nome do treinador original"), pk.otName(), 12, false, text) || text.empty())
                    return;
                pk.otName(text);
                break;
            case Field::OtGender:
                pk.otGender(pk.otGender() == pksm::Gender::Male ? pksm::Gender::Female : pksm::Gender::Male);
                break;
            case Field::Tid:
            case Field::Sid:
            {
                bool tid = row.field == Field::Tid;
                if (activate || toLimit)
                {
                    if (!keyboard(tid ? tr("TID (0 a 65535)") : tr("SID (0 a 65535)"), std::to_string(tid ? pk.TID() : pk.SID()),
                            5, true, text) || text.empty())
                        return;
                    int v = std::clamp(std::atoi(text.c_str()), 0, 65535);
                    tid ? pk.TID(u16(v)) : pk.SID(u16(v));
                }
                else
                {
                    int v = std::clamp((tid ? pk.TID() : pk.SID()) + delta, 0, 65535);
                    tid ? pk.TID(u16(v)) : pk.SID(u16(v));
                }
                break;
            }
            case Field::Version:
                openPicker(PickerKind::Version, 0);
                return;
            case Field::MetLocation:
                openPicker(PickerKind::MetLocation, 0);
                return;
            case Field::EggLocation:
                openPicker(PickerKind::EggLocation, 0);
                return;
            case Field::MetLevel:
                pk.metLevel(u8(step(pk.metLevel(), 1, 100)));
                break;
            case Field::MetDate:
            case Field::EggDate:
            {
                bool met    = row.field == Field::MetDate;
                Date d = met ? pk.metDate() : pk.eggDate();
                if (!keyboard(met ? tr("Data do encontro (DD/MM/AAAA)") : tr("Data do ovo (DD/MM/AAAA)"), dateText(d), 10,
                        false, text) ||
                    !parseDate(text, d))
                    return;
                met ? pk.metDate(d) : pk.eggDate(d);
                break;
            }
            case Field::Hatched:
                if (hatched(pk))
                {
                    pk.eggLocation(0);
                    pk.eggDate(Date{0, 0, 2000});
                }
                else
                {
                    // Hatched at the nursery, the same day it was met
                    pk.eggLocation(60002);
                    pk.eggDate(pk.metDate());
                }
                break;
            case Field::Fateful:
                pk.fatefulEncounter(!pk.fatefulEncounter());
                break;
        }
        mEditChanged = true;
    }

    std::string App::fieldValue(int rowIndex) const
    {
        const pksm::PKX& pk = *mEditPk;
        auto rows           = buildRows(pk);
        const EditRow& row  = rows[rowIndex];
        auto locationName   = [&](u16 id) {
            const std::string& name = i18n::location(LANG, mSave.sav->generation(), id);
            return name.empty() ? "nº " + std::to_string(id) : name;
        };
        switch (row.field)
        {
            case Field::Species:
                return i18n::species(LANG, pk.species());
            case Field::Nickname:
                return pk.nicknamed() ? pk.nickname() : pk.nickname() + tr("  (nome da espécie)");
            case Field::Level:
                return std::to_string(pk.level());
            case Field::Gender:
                return pk.gender() == pksm::Gender::Male     ? tr("Macho")
                       : pk.gender() == pksm::Gender::Female ? tr("Fêmea")
                                                             : tr("Sem gênero");
            case Field::Shiny:
                return pk.shiny() ? tr("Sim") : tr("Não");
            case Field::Nature:
                return std::string(natureName(u8(pk.nature()))) + "  (" + natureEffect(u8(pk.nature())) + ")";
            case Field::Ability:
            {
                const char* which = pk.abilityNumber() == 1 ? "1ª" : (pk.abilityNumber() == 2 ? "2ª" : tr("oculta"));
                return i18n::ability(LANG, pk.ability()) + "  (" + which + ")";
            }
            case Field::Item:
                return pk.heldItem() ? i18n::item(LANG, pk.heldItem()) : tr("Nenhum");
            case Field::Ball:
                return i18n::ball(LANG, pk.ball());
            case Field::Friendship:
                return std::to_string(pk.otFriendship());
            case Field::Move:
            {
                pksm::Move m = pk.move(u8(row.index));
                return m == pksm::Move::None ? "—" : i18n::move(LANG, m);
            }
            case Field::Iv:
                return std::to_string(pk.iv(STAT_NAMES[row.index].stat));
            case Field::Ev:
                return std::to_string(pk.ev(STAT_NAMES[row.index].stat));
            case Field::OtName:
                return pk.otName();
            case Field::OtGender:
                return pk.otGender() == pksm::Gender::Female ? tr("Feminino") : tr("Masculino");
            case Field::Tid:
                return std::to_string(pk.TID()) + tr("   (ID exibido ") + std::to_string(pk.formatTID()) + ")";
            case Field::Sid:
                return std::to_string(pk.SID());
            case Field::Version:
            {
                const std::string& g = i18n::game(LANG, pk.version());
                return g.empty() ? "?" : g;
            }
            case Field::MetLocation:
                return locationName(pk.metLocation());
            case Field::MetLevel:
                return std::to_string(pk.metLevel());
            case Field::MetDate:
                return dateText(pk.metDate());
            case Field::Hatched:
                return hatched(pk) ? tr("Sim") : tr("Não");
            case Field::EggLocation:
                return locationName(pk.eggLocation());
            case Field::EggDate:
                return dateText(pk.eggDate());
            case Field::Fateful:
                return pk.fatefulEncounter() ? tr("Sim") : tr("Não");
        }
        return "";
    }

    // Natures as the usual 5x5 chart: the row is the stat raised, the column the
    // stat lowered, and the diagonal changes nothing
    namespace
    {
        constexpr int CHART_CELL_W = 146, CHART_CELL_H = 58, CHART_HEAD_W = 120, CHART_HEAD_H = 40;
        SDL_Rect chartRect(void)
        {
            int w = CHART_HEAD_W + 5 * CHART_CELL_W + 40, h = CHART_HEAD_H + 5 * CHART_CELL_H + 90;
            return {(SCREEN_W - w) / 2, (SCREEN_H - h) / 2 - 10, w, h};
        }
        SDL_Rect chartCell(int nature)
        {
            SDL_Rect r = chartRect();
            return {r.x + 20 + CHART_HEAD_W + (nature % 5) * CHART_CELL_W, r.y + 70 + CHART_HEAD_H + (nature / 5) * CHART_CELL_H,
                CHART_CELL_W - 6, CHART_CELL_H - 6};
        }
    }

    void App::updateNatureChart(u64 pressed)
    {
        if (pressed & HidNpadButton_B)
        {
            mNatureChart = false;
            return;
        }
        int row = mNatureCursor / 5, col = mNatureCursor % 5;
        if (pressed & KEY_RIGHT)
            col = (col + 1) % 5;
        if (pressed & KEY_LEFT)
            col = (col + 4) % 5;
        if (pressed & KEY_DOWN)
            row = (row + 1) % 5;
        if (pressed & KEY_UP)
            row = (row + 4) % 5;
        mNatureCursor = u8(row * 5 + col);
        for (int n = 0; n < 25; n++)
        {
            if (tappedRect(chartCell(n), mInput))
            {
                if (n == mNatureCursor)
                    pressed |= HidNpadButton_A;
                mNatureCursor = u8(n);
            }
        }
        if (pressed & HidNpadButton_A)
        {
            mEditPk->nature(pksm::Nature{mNatureCursor});
            mEditChanged = true;
            mNatureChart = false;
        }
    }

    void App::drawNatureChart(void)
    {
        static const char* STATS[5] = {"Ataque", "Defesa", "Velocidade", "At. Esp.", "Def. Esp."};
        float in   = easeOut((mGfx.time() - mNatureChartTime) / 0.2f);
        SDL_Rect r = chartRect();
        mGfx.fillRect(0, 0, SCREEN_W, SCREEN_H, Color{0, 0, 0, u8(120 * in)});
        mGfx.setOpacity(in);
        mGfx.setOrigin(0, int((1 - in) * 20));
        mGfx.shadow(r.x, r.y + 8, r.w, r.h, 22, 24, Color{0, 0, 0, 150});
        mGfx.fillRoundRect(r.x, r.y, r.w, r.h, 22, Color{40, 42, 58});
        mGfx.text(tr("Natureza"), r.x + 24, r.y + 20, 26, theme::TEXT);
        mGfx.text(tr("Linha: status que aumenta  ·  Coluna: status que diminui"), r.x + r.w - 24, r.y + 28, 16,
            theme::DIM, Align::Right);
        int top = r.y + 70, left = r.x + 20;
        for (int i = 0; i < 5; i++)
        {
            // Column headers (lowered) and row headers (raised)
            int cx = left + CHART_HEAD_W + i * CHART_CELL_W + (CHART_CELL_W - 6) / 2;
            mGfx.text(std::string("−") + tr(STATS[i]), cx, top + 8, 16, Color{130, 170, 255}, Align::Center,
                CHART_CELL_W - 10);
            int ry = top + CHART_HEAD_H + i * CHART_CELL_H + (CHART_CELL_H - 6 - mGfx.textHeight(16)) / 2;
            mGfx.text(std::string("+") + tr(STATS[i]), left + CHART_HEAD_W - 12, ry, 16, Color{255, 140, 120},
                Align::Right, CHART_HEAD_W - 12);
        }
        u8 current = u8(mEditPk->nature());
        for (int n = 0; n < 25; n++)
        {
            SDL_Rect c   = chartCell(n);
            bool neutral = n / 5 == n % 5;
            bool cursor  = n == mNatureCursor;
            Color bg     = neutral ? Color{255, 255, 255, 10} : Color{255, 255, 255, 22};
            if (n == current)
                bg = Color{0, 190, 255, 70};
            mGfx.fillRoundRect(c.x, c.y, c.w, c.h, 10, bg);
            if (cursor)
                mGfx.roundRect(c.x - 3, c.y - 3, c.w + 6, c.h + 6, 12, theme::CYAN_A, 3);
            mGfx.text(natureName(u8(n)), c.x + c.w / 2, c.y + (c.h - mGfx.textHeight(17)) / 2, 17,
                neutral ? theme::DIM : theme::TEXT, Align::Center, c.w - 8);
        }
        mGfx.resetLayer();
        mHintAreas.clear();
        drawFooter({{HidNpadButton_A, glyph::A, tr("Escolher")}, {HidNpadButton_B, glyph::B, tr("Voltar")}});
    }

    // Ability and item rows show a description strip under the list
    std::string App::editorDescription(void) const
    {
        if (!mEditPk)
            return "";
        auto rows = buildRows(*mEditPk);
        if (mEditRow < 0 || mEditRow >= int(rows.size()))
            return "";
        if (rows[mEditRow].field == Field::Ability)
            return abilityDescription(u16(mEditPk->ability()));
        if (rows[mEditRow].field == Field::Item && mEditPk->heldItem())
            return itemDescription(mEditPk->heldItem());
        return "";
    }

    void App::updateEditor(u64 pressed)
    {
        if (mPicker.open)
        {
            updatePicker(pressed);
            return;
        }
        if (mNatureChart)
        {
            updateNatureChart(pressed);
            return;
        }
        if (pressed & HidNpadButton_B)
        {
            finishEditing(true);
            return;
        }
        if (pressed & HidNpadButton_X)
        {
            bool creating = mCreating;
            finishEditing(false);
            showToast(creating ? tr("Criação cancelada.") : tr("Alterações descartadas."));
            return;
        }
        auto rows = buildRows(*mEditPk);
        int count = int(rows.size());
        if (pressed & KEY_DOWN)
            mEditRow = std::min(mEditRow + 1, count - 1);
        if (pressed & KEY_UP)
            mEditRow = std::max(mEditRow - 1, 0);
        mEditRow = std::min(mEditRow, count - 1);

        // Touch: pick a row; touching the chosen row again acts on it
        SDL_Rect area = editorArea();
        for (int r = 0; r < count; r++)
        {
            int y = area.y + rowY(rows, r) - mEditScroll;
            if (y < area.y || y + ROW_H > area.y + area.h)
                continue;
            if (mInput.tappedIn(area.x, y, area.w, ROW_H))
            {
                if (r == mEditRow)
                {
                    if (mInput.touchX() > area.x + area.w - 70)
                        pressed |= KEY_RIGHT;
                    else
                        pressed |= HidNpadButton_A;
                }
                mEditRow = r;
            }
        }

        if (pressed & HidNpadButton_A)
            editField(mEditRow, 1, false, true);
        else if (pressed & KEY_RIGHT)
            editField(mEditRow, 1, false, false);
        else if (pressed & KEY_LEFT)
            editField(mEditRow, -1, false, false);
        else if (pressed & HidNpadButton_R)
            editField(mEditRow, 10, false, false);
        else if (pressed & HidNpadButton_L)
            editField(mEditRow, -10, false, false);
        else if (pressed & HidNpadButton_ZR)
            editField(mEditRow, 1, true, false);
        else if (pressed & HidNpadButton_ZL)
            editField(mEditRow, -1, true, false);

        // Keep the chosen row in view
        rows  = buildRows(*mEditPk);
        count = int(rows.size());
        mEditRow = std::min(mEditRow, count - 1);
        int y     = rowY(rows, mEditRow);
        int viewH = area.h - (editorDescription().empty() ? 0 : DESC_H + 8);
        if (y - mEditScroll + ROW_H > viewH)
            mEditScroll = y + ROW_H - viewH;
        int top = y - (rows[mEditRow].section ? SECTION_H : 0);
        if (top < mEditScroll)
            mEditScroll = top;
    }

    SDL_Rect App::editorArea(void) const
    {
        // The middle and right columns of the detail card
        return {64 + 420, 56 + 28, SCREEN_W - 128 - 420 - 28, 566 - 56};
    }

    void App::drawEditor(void)
    {
        const pksm::PKX& pk = *mEditPk;
        auto rows     = buildRows(pk);
        SDL_Rect area = editorArea();
        SDL_RenderSetClipRect(mGfx.renderer(), &area);
        int valueX = area.x + area.w - 16;

        for (int r = 0; r < int(rows.size()); r++)
        {
            const EditRow& row = rows[r];
            int y = area.y + rowY(rows, r) - mEditScroll;
            if (row.section)
            {
                std::string title = row.section;
                if (row.field == Field::Ev)
                    title += tr("  ·  usados: ") + std::to_string(totalEvs(pk));
                mGfx.text(title, area.x + 4, y - SECTION_H + 8, 16, theme::DIM);
            }
            if (y + ROW_H < area.y || y > area.y + area.h)
                continue;
            bool sel = r == mEditRow;
            mGfx.fillRoundRect(area.x, y + 2, area.w, ROW_H - 4, 10,
                sel ? Color{255, 255, 255, 30} : Color{255, 255, 255, u8(r % 2 ? 6 : 12)});

            std::string label;
            float bar      = -1;
            Color barColor = theme::IV_BAR;
            switch (row.field)
            {
                case Field::Species: label = tr("Espécie"); break;
                case Field::Nickname: label = tr("Apelido"); break;
                case Field::Level:
                    label = tr("Nível"), bar = pk.level() / 100.0f, barColor = theme::CYAN_B;
                    break;
                case Field::Gender: label = tr("Gênero"); break;
                case Field::Shiny: label = tr("Shiny"); break;
                case Field::Nature: label = tr("Natureza"); break;
                case Field::Ability: label = tr("Habilidade"); break;
                case Field::Item: label = tr("Item"); break;
                case Field::Ball: label = tr("Poké Ball"); break;
                case Field::Friendship:
                    label = tr("Amizade"), bar = pk.otFriendship() / 255.0f, barColor = theme::FEMALE;
                    break;
                case Field::Move: label = tr("Golpe ") + std::to_string(row.index + 1); break;
                case Field::Iv:
                {
                    u8 iv = pk.iv(STAT_NAMES[row.index].stat);
                    label = tr(STAT_NAMES[row.index].label), bar = iv / 31.0f;
                    barColor = iv == 31 ? theme::GOLD : theme::IV_BAR;
                    break;
                }
                case Field::Ev:
                    label    = tr(STAT_NAMES[row.index].label);
                    bar      = pk.ev(STAT_NAMES[row.index].stat) / 252.0f;
                    barColor = theme::EV_BAR;
                    break;
                case Field::OtName: label = tr("Treinador original"); break;
                case Field::OtGender: label = tr("Gênero do treinador"); break;
                case Field::Tid: label = "TID"; break;
                case Field::Sid: label = "SID"; break;
                case Field::Version: label = tr("Jogo de origem"); break;
                case Field::MetLocation: label = tr("Local do encontro"); break;
                case Field::MetLevel: label = tr("Nível no encontro"); break;
                case Field::MetDate: label = tr("Data do encontro"); break;
                case Field::Hatched: label = tr("Chocado de um ovo"); break;
                case Field::EggLocation: label = tr("Local do ovo"); break;
                case Field::EggDate: label = tr("Data do ovo"); break;
                case Field::Fateful: label = tr("Encontro fatídico (evento)"); break;
            }
            std::string value = fieldValue(r);
            int ty = y + (ROW_H - mGfx.textHeight(18)) / 2;
            mGfx.text(label, area.x + 16, ty, 18, sel ? theme::TEXT : theme::MUTED);
            int valueRight = valueX;
            if (row.field == Field::Item && pk.heldItem())
            {
                int vw = std::min(380, mGfx.textWidth(value, 18));
                mGfx.draw(itemIcon(mGfx, pk.heldItem(), i18n::item(pksm::Language::ENG, pk.heldItem())),
                    valueX - vw - 34, y + 2, 30, 30);
            }
            if (row.field == Field::Ball)
            {
                int vw = std::min(380, mGfx.textWidth(value, 18)), bw = 0, bh = 0;
                char path[48];
                snprintf(path, sizeof(path), "romfs:/sprites/_ball%u.png", unsigned(u8(pk.ball())));
                if (SDL_Texture* ball = mGfx.pixelTexture(path, bw, bh))
                    mGfx.draw(ball, valueX - vw - 32, y + 4, 26, 26);
            }
            if (row.field == Field::Move)
            {
                MoveDetails info = moveDetails(pk.move(u8(row.index)));
                if (info.known)
                    typeCircle(mGfx, info.type, valueX - 10, y + ROW_H / 2, 10), valueRight = valueX - 28;
            }
            if (bar >= 0)
            {
                int bw = 180, bx = valueX - 60 - bw;
                mGfx.fillRoundRect(bx, y + ROW_H / 2 - 4, bw, 8, 4, theme::BAR_BG);
                if (bar > 0)
                    mGfx.fillRoundRect(bx, y + ROW_H / 2 - 4, std::max(8, int(bw * bar)), 8, 4, barColor);
            }
            mGfx.text(value, valueRight, ty, 18, theme::TEXT, Align::Right, 380);
        }
        SDL_RenderSetClipRect(mGfx.renderer(), nullptr);

        std::string desc = editorDescription();
        if (!desc.empty())
        {
            int dy = area.y + area.h - DESC_H;
            mGfx.fillRoundRect(area.x, dy, area.w, DESC_H, 12, Color{24, 25, 36, 245});
            mGfx.roundRect(area.x, dy, area.w, DESC_H, 12, Color{255, 255, 255, 30}, 1);
            mGfx.textWrapped(desc, area.x + 16, dy + 10, 16, theme::MUTED, Align::Left, area.w - 32, 3, 21);
        }

        // Scroll bar
        int contentH = rowY(rows, int(rows.size()) - 1) + ROW_H;
        if (contentH > area.h)
        {
            int trackH = area.h - 20;
            int thumbH = std::max(30, trackH * area.h / contentH);
            int thumbY = area.y + 10 + (trackH - thumbH) * mEditScroll / std::max(1, contentH - area.h);
            mGfx.fillRoundRect(area.x + area.w + 8, thumbY, 4, thumbH, 2, Color{255, 255, 255, 60});
        }

        if (mPicker.open)
            drawPicker();
    }

    // ------------------------------------------------------------------ saving

    void App::saveChanges(DialogAction after)
    {
        if (!mSave.sav)
            return;
        std::string error;
        bool ok = false;
        AccountUid uid       = mUsers[mUser].uid;
        std::string userName = mUsers[mUser].name;
        int maxBackups       = mConfig.maxBackups;
        GameEntry game       = mOpenGame;
        bool gameDirty = mDirty;
        runWithLoading(tr("Gravando"), [&] {
            ok = commitBank(true, error);
            if (ok && gameDirty)
                ok = writeGameSave(game, uid, userName, maxBackups, mSave, error);
            if (ok)
                ok = commitBank(false, error);
        });
        if (!ok)
        {
            showDialog(tr("Não foi possível gravar"), error, {{HidNpadButton_B, glyph::B, "OK", DialogAction::None}});
            return;
        }
        mDirty = false;
        showToast(gameDirty ? tr("Gravado! Backup do save anterior em ") + mSave.backupPath : tr("Banco gravado no cartão SD."));
        runDialogAction(after);
    }

    void App::runDialogAction(DialogAction action)
    {
        switch (action)
        {
            case DialogAction::LeaveBoxes:
                mBankPending.clear();
                mBagView = false;
                mHeld.clear();
                mMarked.clear();
                mBankView = false;
                mSearch.open = false;
                mDirty   = false;
                mDetail  = false;
                mEditing = false;
                mSlots.clear();
                mPrevSlots.clear();
                mSave   = {};
                mScreen = Screen::Games;
                break;
            case DialogAction::Quit:
                mQuit = true;
                break;
            case DialogAction::SaveThenLeave:
                saveChanges(DialogAction::LeaveBoxes);
                break;
            case DialogAction::SaveThenQuit:
                saveChanges(DialogAction::Quit);
                break;
            case DialogAction::RestoreBackup:
                restoreSelectedBackup();
                break;
            case DialogAction::Release:
                releaseSelected();
                break;
            default:
                break;
        }
    }

    void App::askUnsaved(DialogAction leave, DialogAction saveThenLeave)
    {
        showDialog(tr("Alterações não salvas"),
            tr("Você editou Pokémon que ainda não foram gravados no jogo.\nO que deseja fazer?"),
            {{HidNpadButton_A, glyph::A, tr("Gravar"), saveThenLeave},
                {HidNpadButton_X, glyph::X, tr("Descartar"), leave},
                {HidNpadButton_B, glyph::B, tr("Cancelar"), DialogAction::None}});
    }

    // ------------------------------------------------------------------ dialogs and notices

    void App::showDialog(const std::string& title, const std::string& message, std::vector<DialogOption> options)
    {
        mDialog.open    = true;
        mDialog.title   = title;
        mDialog.message = message;
        mDialog.options = std::move(options);
        mDialog.time    = mGfx.time();
    }

    bool App::updateDialog(u64 pressed)
    {
        if (!mDialog.open)
            return false;
        for (size_t i = 0; i < mDialog.options.size(); i++)
        {
            const auto& o = mDialog.options[i];
            bool tapped   = i < mDialog.areas.size() && tappedRect(mDialog.areas[i], mInput);
            if ((pressed & o.button) || tapped)
            {
                DialogAction action = o.action;
                mDialog.open        = false;
                runDialogAction(action);
                break;
            }
        }
        return true;
    }

    void App::drawDialog(void)
    {
        if (!mDialog.open)
            return;
        float in = easeOut((mGfx.time() - mDialog.time) / 0.22f);
        mGfx.fillRect(0, 0, SCREEN_W, SCREEN_H, Color{0, 0, 0, u8(140 * in)});
        int w = 720, h = 300, x = (SCREEN_W - w) / 2, y = (SCREEN_H - h) / 2;
        mGfx.setOpacity(in);
        mGfx.setOrigin(0, int((1 - in) * 20));
        mGfx.shadow(x, y + 8, w, h, 22, 24, Color{0, 0, 0, 150});
        mGfx.fillRoundRect(x, y, w, h, 22, Color{40, 42, 58});
        mGfx.text(mDialog.title, SCREEN_W / 2, y + 30, 28, theme::TEXT, Align::Center, w - 60);
        int ly = y + 84;
        size_t start = 0;
        while (start <= mDialog.message.size())
        {
            size_t end = mDialog.message.find('\n', start);
            std::string line = mDialog.message.substr(start, end == std::string::npos ? std::string::npos : end - start);
            mGfx.text(line, SCREEN_W / 2, ly, 20, theme::MUTED, Align::Center, w - 60);
            ly += 30;
            if (end == std::string::npos)
                break;
            start = end + 1;
        }
        // Buttons along the bottom
        int n  = int(mDialog.options.size());
        int bw = (w - 40 - (n - 1) * 12) / std::max(1, n);
        mDialog.areas.clear();
        for (int i = 0; i < n; i++)
        {
            const auto& o = mDialog.options[i];
            int bx = x + 20 + i * (bw + 12), by = y + h - 76;
            mGfx.fillRoundRect(bx, by, bw, 56, 14, i == 0 ? Color{0, 150, 200, 90} : Color{255, 255, 255, 18});
            int gw = mGfx.buttonGlyphWidth(o.glyph, 24);
            int lw = mGfx.textWidth(o.label, 22);
            int cx = bx + (bw - gw - 8 - lw) / 2;
            mGfx.buttonGlyph(o.glyph, cx, by + 13, 24, theme::TEXT);
            mGfx.text(o.label, cx + gw + 8, by + 15, 22, theme::TEXT);
            mDialog.areas.push_back({bx, by, bw, 56});
        }
        mGfx.resetLayer();
    }

    void App::showToast(const std::string& text)
    {
        mToast     = text;
        mToastTime = mGfx.time();
    }

    void App::drawToast(void)
    {
        float t = mGfx.time() - mToastTime;
        if (mToast.empty() || t > 3.5f)
            return;
        float a = easeOut(t / 0.25f) * clamp01((3.5f - t) / 0.4f);
        int w   = std::min(1100, mGfx.textWidth(mToast, 20) + 60);
        int x   = (SCREEN_W - w) / 2, y = 14 + int((1 - a) * -20);
        mGfx.setOpacity(a);
        mGfx.shadow(x, y + 4, w, 50, 25, 14, Color{0, 0, 0, 140});
        mGfx.fillRoundRect(x, y, w, 50, 25, Color{30, 120, 90});
        mGfx.text(mToast, SCREEN_W / 2, y + (50 - mGfx.textHeight(20)) / 2, 20, theme::TEXT, Align::Center, w - 40);
        mGfx.resetLayer();
    }
}
