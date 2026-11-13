// The bag: pouches on the left, their items in the middle, the chosen item on
// the right. Counts change with the D-pad; items are added or swapped through
// the searchable picker.
#include <algorithm>
#include <cmath>

#include "app.hpp"
#include "lang.hpp"
#include "sav/Item.hpp"
#include "theme.hpp"
#include "ui/items.hpp"
#include "ui/types.hpp"
#include "utils/i18n.hpp"

using namespace monhex::ui;
using namespace monhex::layout;

namespace monhex
{
    namespace
    {
        constexpr int POUCH_W = 210, ROW_H = 46;
        using Pouch = pksm::Sav::Pouch;

        // Pouch rows shrink when a game has many pouches, so all of them fit
        int pouchRowH(int count) { return std::min(ROW_H, (BOX_H - 90) / std::max(1, count)); }

        const char* pouchName(Pouch p)
        {
            switch (p)
            {
                case Pouch::NormalItem: return tr("Itens");
                case Pouch::KeyItem: return tr("Itens-chave");
                case Pouch::TM: return tr("MTs");
                case Pouch::Mail: return tr("Cartas");
                case Pouch::Medicine: return tr("Remédios");
                case Pouch::Berry: return tr("Frutas");
                case Pouch::Ball: return tr("Poké Bolas");
                case Pouch::Battle: return tr("Batalha");
                case Pouch::Candy: return tr("Doces e materiais");
                case Pouch::ZCrystals: return tr("Cristais Z");
                case Pouch::Treasure: return tr("Tesouros");
                case Pouch::Ingredient: return tr("Ingredientes");
                case Pouch::PCItem: return "PC";
                case Pouch::RotomPower: return tr("Poderes Rotom");
                case Pouch::CatchingItem: return tr("Captura");
                case Pouch::MegaStones: return tr("Megapedras");
                case Pouch::Recipe: return tr("Receitas");
                case Pouch::Donut: return tr("Donuts");
            }
            return "?";
        }

        std::string englishName(u16 id) { return i18n::item(pksm::Language::ENG, id); }
    }

    // Pouches shown together: battle items go in with the medicine
    std::vector<App::BagGroup> App::bagGroups(void) const
    {
        auto pouches = mSave.sav->pouches();
        std::vector<BagGroup> groups;
        int medicine = -1, battle = -1;
        for (int i = 0; i < int(pouches.size()); i++)
        {
            if (pouches[i].first == Pouch::Medicine)
                medicine = i;
            if (pouches[i].first == Pouch::Battle)
                battle = i;
        }
        for (int i = 0; i < int(pouches.size()); i++)
        {
            if (i == battle && medicine >= 0)
                continue;
            BagGroup g{pouchName(pouches[i].first), {i}};
            if (i == medicine && battle >= 0)
                g.pouches.push_back(battle);
            groups.push_back(g);
        }
        return groups;
    }

    u16 App::bagMax(int pouch, const pksm::Item& item) const
    {
        auto p = mSave.sav->pouches()[pouch].first;
        return std::max<u16>(1, std::min(mSave.sav->maxCount(p), item.maxCount()));
    }

    std::vector<App::BagRow> App::bagRows(void) const
    {
        std::vector<BagRow> rows;
        auto groups  = bagGroups();
        auto pouches = mSave.sav->pouches();
        if (mBagPouch >= int(groups.size()))
            return rows;
        bool room = false;
        for (int pouch : groups[mBagPouch].pouches)
        {
            auto [p, size] = pouches[pouch];
            for (int slot = 0; slot < size; slot++)
            {
                auto item = mSave.sav->item(p, u16(slot));
                if (!item || item->id() == 0 || item->count() == 0)
                {
                    room = true;
                    continue;
                }
                rows.push_back({pouch, slot, item->id(), item->count(), bagMax(pouch, *item)});
            }
        }
        // Last row adds a new item, when there is room
        if (room)
            rows.push_back({-1, -1, 0, 0, 0});
        return rows;
    }

    void App::setBagCount(const BagRow& row, int count)
    {
        auto pouches = mSave.sav->pouches();
        auto [pouch, size] = pouches[row.pouch];
        auto item = mSave.sav->item(pouch, u16(row.slot));
        if (!item)
            return;
        count = std::clamp(count, 0, int(row.max));
        if (count == item->count())
            return; // already there: nothing changes, nothing to save
        if (count == 0 && !mSave.sav->pouchIndexedByItem(pouch))
        {
            // Packed pouches have no gaps: later items move up one slot
            for (int s = row.slot; s + 1 < size; s++)
            {
                auto next = mSave.sav->item(pouch, u16(s + 1));
                mSave.sav->item(*next, pouch, u16(s));
                if (next->id() == 0)
                    break;
            }
            auto last = mSave.sav->item(pouch, u16(size - 1));
            last->id(0);
            last->count(0);
            mSave.sav->item(*last, pouch, u16(size - 1));
        }
        else
        {
            item->count(u16(count));
            mSave.sav->item(*item, pouch, u16(row.slot));
        }
        mDirty = true;
    }

    void App::openBagPicker(const BagRow& row)
    {
        auto groups  = bagGroups();
        auto pouches = mSave.sav->pouches();
        auto valid   = mSave.sav->validItems();
        mBagPickPouch = row.pouch;
        mBagPickSlot  = row.slot;
        mPicker       = {};
        mPicker.kind  = PickerKind::BagItem;
        mPicker.title = groups[mBagPouch].name;
        mPicker.time  = mGfx.time();
        mPicker.area  = {PANEL_X + 16, PANEL_Y + 16, PANEL_W - 32, PANEL_H - 32};
        // Replacing offers the item's own pouch; adding offers the whole group.
        // Items already in the bag aren't offered again.
        std::vector<int> owned;
        for (const auto& r : bagRows())
            if (r.slot >= 0)
                owned.push_back(r.id);
        std::vector<int> wanted = row.slot >= 0 ? std::vector<int>{row.pouch} : groups[mBagPouch].pouches;
        for (int pouch : wanted)
        {
            for (const auto& [p, ids] : valid)
            {
                if (p != pouches[pouch].first)
                    continue;
                for (int id : ids)
                {
                    const std::string& name = i18n::item(LANG, u16(id));
                    if (id <= 0 || name.empty() || std::find(owned.begin(), owned.end(), id) != owned.end())
                        continue;
                    std::string shown = name;
                    u8 type = 255;
                    pksm::Move m = machineMove(mSave.sav->version(), englishName(u16(id)));
                    if (m != pksm::Move::None)
                    {
                        shown += " · " + i18n::move(LANG, m);
                        MoveDetails info = moveDetails(m);
                        type = info.known ? info.type : 255;
                    }
                    mPicker.items.push_back({id, shown, type, type == 255 ? id : -1});
                }
            }
        }
        std::sort(mPicker.items.begin(), mPicker.items.end(),
            [](const PickerItem& a, const PickerItem& b) { return a.name < b.name; });
        filterPicker();
        mPicker.open = true;
    }

    void App::pickBagItem(int id)
    {
        auto groups  = bagGroups();
        auto pouches = mSave.sav->pouches();
        auto valid   = mSave.sav->validItems();
        int pouch    = mBagPickPouch;
        int slot     = mBagPickSlot;
        if (slot < 0)
        {
            // Adding: the item belongs to whichever pouch of the group lists it
            pouch = -1;
            for (int candidate : groups[mBagPouch].pouches)
                for (const auto& [p, ids] : valid)
                    if (p == pouches[candidate].first && std::find(ids.begin(), ids.end(), id) != ids.end())
                        pouch = candidate;
        }
        if (pouch < 0)
            return;
        auto [p, size] = pouches[pouch];
        if (mSave.sav->pouchIndexedByItem(p))
        {
            // Every item has its own slot: find it
            slot = -1;
            for (const auto& [vp, ids] : valid)
                if (vp == p)
                    for (size_t i = 0; i < ids.size(); i++)
                        if (ids[i] == id)
                            slot = int(i);
        }
        else if (slot < 0)
        {
            for (int s = 0; s < size; s++)
            {
                auto item = mSave.sav->item(p, u16(s));
                if (item && (item->id() == 0 || item->count() == 0))
                {
                    slot = s;
                    break;
                }
            }
        }
        if (slot < 0 || slot >= size)
        {
            showToast(tr("Não há espaço para esse item."));
            return;
        }
        auto item = mSave.sav->item(p, u16(slot));
        item->id(u16(id));
        if (item->count() == 0)
            item->count(1);
        mSave.sav->item(*item, p, u16(slot));
        mDirty = true;
        // Put the cursor on the item just added or changed
        auto rows = bagRows();
        for (size_t i = 0; i < rows.size(); i++)
            if (rows[i].pouch == pouch && rows[i].slot == slot)
                mBagCursor = int(i);
        mBagFocus = 1;
    }

    void App::updateBag(u64 pressed)
    {
        if (mPicker.open)
        {
            updatePicker(pressed);
            return;
        }
        auto groups    = bagGroups();
        int groupCount = int(groups.size());
        auto rows      = bagRows();
        int count      = int(rows.size());
        int listX = BOX_X + POUCH_W + 24, listW = BOX_W - POUCH_W - 40;
        int listY = BOX_Y + 70, visible = (BOX_H - 90) / ROW_H;

        // Touch
        int ph = pouchRowH(groupCount);
        for (int i = 0; i < groupCount; i++)
        {
            if (mInput.tappedIn(BOX_X + 12, BOX_Y + 70 + i * ph, POUCH_W, ph))
            {
                mBagPouch = i, mBagCursor = 0, mBagScroll = 0, mBagFocus = 0;
                return;
            }
        }
        for (int i = 0; i < visible && mBagScroll + i < count; i++)
        {
            if (mInput.tappedIn(listX, listY + i * ROW_H, listW, ROW_H))
            {
                if (mBagFocus == 1 && mBagCursor == mBagScroll + i)
                    pressed |= HidNpadButton_A;
                mBagFocus  = 1;
                mBagCursor = mBagScroll + i;
            }
        }

        if (mBagFocus == 0)
        {
            if (pressed & KEY_DOWN)
                mBagPouch = std::min(mBagPouch + 1, groupCount - 1), mBagCursor = 0, mBagScroll = 0;
            if (pressed & KEY_UP)
                mBagPouch = std::max(mBagPouch - 1, 0), mBagCursor = 0, mBagScroll = 0;
            if ((pressed & (KEY_RIGHT | HidNpadButton_A)) && count > 0)
                mBagFocus = 1;
            return;
        }

        if (pressed & KEY_DOWN)
            mBagCursor = std::min(mBagCursor + 1, count - 1);
        if (pressed & KEY_UP)
            mBagCursor = std::max(mBagCursor - 1, 0);
        mBagCursor = std::clamp(mBagCursor, 0, std::max(0, count - 1));
        if (mBagCursor < mBagScroll)
            mBagScroll = mBagCursor;
        if (mBagCursor >= mBagScroll + visible)
            mBagScroll = mBagCursor - visible + 1;
        if (count == 0)
            return;

        const BagRow row = rows[mBagCursor];
        if (row.slot < 0)
        {
            if (pressed & HidNpadButton_A)
                openBagPicker(row);
            if (pressed & KEY_LEFT)
                mBagFocus = 0;
            return;
        }
        if (pressed & HidNpadButton_A)
            openBagPicker(row);
        else if (pressed & KEY_LEFT)
        {
            if (row.max <= 1 || row.count <= 1)
                mBagFocus = 0; // nothing lower than one: left goes back to the pouches (ZL removes)
            else
                setBagCount(row, row.count - 1);
        }
        else if (pressed & KEY_RIGHT)
            setBagCount(row, row.count + 1);
        else if (pressed & HidNpadButton_R)
            setBagCount(row, row.count + 10);
        else if (pressed & HidNpadButton_L)
            setBagCount(row, std::max(1, row.count - 10));
        else if (pressed & HidNpadButton_ZR)
            setBagCount(row, row.max);
        else if (pressed & HidNpadButton_ZL)
            setBagCount(row, 0); // removes it
    }

    void App::drawBag(void)
    {
        auto groups = bagGroups();
        auto rows   = bagRows();
        int count   = int(rows.size());
        pksm::GameVersion game = mSave.sav->version();

        // Left panel: pouches and the item list
        mGfx.shadow(BOX_X, BOX_Y + 4, BOX_W, BOX_H, 20, 16, theme::SHADOW);
        mGfx.fillRoundRect(BOX_X, BOX_Y, BOX_W, BOX_H, 20, lerp(theme::BG, Color{60, 110, 90}, 0.55f));
        if (mWallpapers[1])
            mGfx.draw(mWallpapers[1], BOX_X, BOX_Y, BOX_W, BOX_H, 28);
        mGfx.text(tr("Mochila"), BOX_X + 24, BOX_Y + 20, 26, theme::TEXT);

        int ph = pouchRowH(int(groups.size()));
        for (int i = 0; i < int(groups.size()); i++)
        {
            int y    = BOX_Y + 70 + i * ph;
            bool sel = i == mBagPouch;
            if (sel)
                mGfx.fillRoundRect(BOX_X + 12, y + 2, POUCH_W, ph - 4, 12,
                    mBagFocus == 0 ? Color{0, 190, 255, 70} : Color{255, 255, 255, 40});
            mGfx.text(groups[i].name, BOX_X + 28, y + (ph - mGfx.textHeight(19)) / 2, 19,
                sel ? theme::TEXT : theme::MUTED, Align::Left, POUCH_W - 24);
        }

        int listX = BOX_X + POUCH_W + 24, listW = BOX_W - POUCH_W - 40;
        int listY = BOX_Y + 70, visible = (BOX_H - 90) / ROW_H;
        mGfx.fillRoundRect(listX - 8, BOX_Y + 62, listW + 16, BOX_H - 76, 14, Color{0, 0, 0, 70});
        if (count == 0)
            mGfx.text(tr("Bolso vazio."), listX + listW / 2, listY + 40, 20, theme::DIM, Align::Center);
        for (int i = 0; i < visible && mBagScroll + i < count; i++)
        {
            int index = mBagScroll + i;
            const BagRow& row = rows[index];
            int y    = listY + i * ROW_H;
            bool sel = mBagFocus == 1 && index == mBagCursor;
            mGfx.fillRoundRect(listX, y + 2, listW, ROW_H - 4, 10,
                sel ? Color{0, 190, 255, 60} : Color{255, 255, 255, u8(i % 2 ? 6 : 14)});
            int ty = y + (ROW_H - mGfx.textHeight(18)) / 2;
            if (row.slot < 0)
            {
                mGfx.text(tr("+  Adicionar item"), listX + 16, ty, 18, theme::CYAN_A);
                continue;
            }
            std::string eng = englishName(row.id);
            mGfx.draw(itemIcon(mGfx, row.id, eng), listX + 6, y + 5, 36, 36);
            std::string name = i18n::item(LANG, row.id);
            int right        = listX + listW - 14;
            if (row.max > 1)
                right -= 8 + mGfx.text("x" + std::to_string(row.count), right, ty, 18, theme::MUTED, Align::Right);
            pksm::Move m = machineMove(game, eng);
            if (m != pksm::Move::None)
            {
                MoveDetails info = moveDetails(m);
                if (info.known)
                {
                    typeCircle(mGfx, info.type, right - 11, y + ROW_H / 2, 11);
                    right -= 30;
                }
                name += " · " + i18n::move(LANG, m);
            }
            mGfx.text(name, listX + 50, ty, 18, theme::TEXT, Align::Left, right - listX - 56);
        }
        if (count > visible)
        {
            int trackH = visible * ROW_H - 10;
            int thumbH = std::max(30, trackH * visible / count);
            int thumbY = listY + 5 + (trackH - thumbH) * mBagScroll / std::max(1, count - visible);
            mGfx.fillRoundRect(listX + listW + 2, thumbY, 4, thumbH, 2, Color{255, 255, 255, 60});
        }

        // Right panel: the chosen item's details
        const int x = PANEL_X, y = PANEL_Y, w = PANEL_W;
        mGfx.shadow(x, y + 4, w, PANEL_H, 18, 16, theme::SHADOW);
        mGfx.fillRoundRect(x, y, w, PANEL_H, 18, theme::CARD);
        if (mPicker.open)
        {
            drawPicker();
        }
        else if (mBagFocus == 1 && mBagCursor < count && rows[mBagCursor].slot >= 0)
        {
            const BagRow& row = rows[mBagCursor];
            std::string eng   = englishName(row.id);
            auto pouch        = mSave.sav->pouches()[row.pouch].first;
            mGfx.fillRoundRect(x + 24, y + 24, 112, 112, 18, theme::STAGE);
            mGfx.draw(itemIcon(mGfx, row.id, eng), x + 36, y + 36, 88, 88);
            mGfx.text(pouchName(pouch), x + 156, y + 30, 16, theme::DIM);
            mGfx.textWrapped(i18n::item(LANG, row.id), x + 156, y + 52, 28, theme::TEXT, Align::Left, w - 180, 2, 34);

            int dy = y + 156;
            pksm::Move m = machineMove(game, eng);
            if (m != pksm::Move::None)
            {
                // What the machine teaches
                MoveDetails info = moveDetails(m);
                mGfx.fillRoundRect(x + 24, dy, w - 48, 52, 12,
                    lerp(theme::STAGE, info.known ? typeColor(info.type) : theme::DIM, 0.35f));
                if (info.known)
                    typeCircle(mGfx, info.type, x + 48, dy + 26, 15);
                mGfx.text(i18n::move(LANG, m), x + 72, dy + 6, 20, theme::TEXT, Align::Left, w - 120);
                if (info.known)
                {
                    static const char* CAT[] = {"Estado", "Físico", "Especial"};
                    std::string extra = std::string(tr(CAT[info.category])) + tr("  ·  Poder ") +
                                        (info.power ? std::to_string(info.power) : "—") + tr("  ·  Prec. ") +
                                        (info.accuracy ? std::to_string(info.accuracy) + "%" : "—");
                    mGfx.text(extra, x + 72, dy + 30, 15, theme::MUTED, Align::Left, w - 120);
                }
                dy += 66;
            }
            const std::string& desc = itemDescription(row.id);
            mGfx.textWrapped(desc.empty() ? tr("Sem descrição.") : desc, x + 24, dy, 17, theme::MUTED, Align::Left, w - 48,
                5, 24);

            // Quantity, only when it can change
            int qy = y + PANEL_H - 150;
            drawDivider(x + 24, qy - 12, w - 48);
            if (row.max > 1)
            {
                mGfx.text(tr("Quantidade"), x + w / 2, qy, 16, theme::DIM, Align::Center);
                mGfx.arrow(x + w / 2 - 100, qy + 52, 16, false, theme::CYAN_A);
                mGfx.text(std::to_string(row.count), x + w / 2, qy + 26, 46, theme::TEXT, Align::Center);
                mGfx.arrow(x + w / 2 + 100, qy + 52, 16, true, theme::CYAN_A);
                mGfx.text(tr("máximo ") + std::to_string(row.max), x + w / 2, qy + 92, 15, theme::DIM, Align::Center);
            }
            else
            {
                mGfx.textWrapped(tr("Só é possível ter um deste item. ZL remove, A troca por outro."), x + w / 2, qy + 30, 17,
                    theme::DIM, Align::Center, w - 60, 2, 24);
            }
        }
        else
        {
            mGfx.textWrapped(mBagFocus == 0 ? tr("Escolha um bolso e aperte para a direita para ver os itens.")
                                            : tr("Aperte A para escolher um item e colocá-lo neste bolso."),
                x + w / 2, y + PANEL_H / 2 - 30, 20, theme::DIM, Align::Center, w - 80, 3, 30);
        }

        std::vector<Hint> hints;
        if (mPicker.open)
        {
            hints = {{HidNpadButton_Y, glyph::Y, tr("Buscar")}, {HidNpadButton_A, glyph::A, tr("Escolher")},
                {HidNpadButton_B, glyph::B, tr("Voltar")}};
        }
        else
        {
            if (mBagFocus == 1 && mBagCursor < count && rows[mBagCursor].slot >= 0)
            {
                hints.push_back({HidNpadButton_A, glyph::A, tr("Trocar item")});
                hints.push_back({HidNpadButton_ZL, glyph::ZL, tr("Remover")});
            }
            hints.push_back({HidNpadButton_B, glyph::B, mBagFocus == 1 ? tr("Bolsos") : tr("Caixas")});
        }
        drawFooter(hints);
    }
}
