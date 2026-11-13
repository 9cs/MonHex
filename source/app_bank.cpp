// The bank (boxes kept on the SD card, shared by every game) and searching
#include <algorithm>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

#include "app.hpp"
#include "lang.hpp"
#include "pkx/PA9.hpp"
#include "theme.hpp"
#include "ui/sprites.hpp"
#include "ui/types.hpp"
#include "utils/i18n.hpp"

using namespace monhex::ui;
using namespace monhex::layout;

namespace monhex
{
    namespace
    {
        // Each Pokemon is its own file, bank/box_007/12.pk9: readable by PKHeX and
        // easy to copy around
        constexpr const char* BANK_DIR = "sdmc:/switch/monhex/bank";

        std::string boxDir(int box)
        {
            char buf[64];
            snprintf(buf, sizeof(buf), "%s/box_%03d", BANK_DIR, box + 1);
            return buf;
        }

        // The file holding a bank slot, or "" when it's empty
        std::string findFile(int box, int slot)
        {
            std::string dir = boxDir(box);
            char prefix[16];
            snprintf(prefix, sizeof(prefix), "%02d.", slot + 1);
            std::string found;
            if (DIR* d = opendir(dir.c_str()))
            {
                while (dirent* e = readdir(d))
                {
                    if (strncmp(e->d_name, prefix, 3) == 0)
                    {
                        found = dir + "/" + e->d_name;
                        break;
                    }
                }
                closedir(d);
            }
            return found;
        }

        std::unique_ptr<pksm::PKX> loadFile(const std::string& path)
        {
            size_t dot = path.rfind('.');
            if (dot == std::string::npos)
                return nullptr;
            std::string ext = path.substr(dot + 1);
            std::vector<u8> data;
            if (FILE* f = fopen(path.c_str(), "rb"))
            {
                fseek(f, 0, SEEK_END);
                long size = ftell(f);
                rewind(f);
                if (size > 0 && size < 0x1000)
                {
                    data.resize(size_t(size));
                    if (fread(data.data(), 1, data.size(), f) != data.size())
                        data.clear();
                }
                fclose(f);
            }
            if (data.empty())
                return nullptr;
            static const std::pair<const char*, pksm::Generation> GENS[] = {{"pk9", pksm::Generation::NINE},
                {"pk8", pksm::Generation::EIGHT}, {"pa8", pksm::Generation::EIGHT}, {"pb7", pksm::Generation::LGPE},
                {"pk7", pksm::Generation::SEVEN}, {"pk6", pksm::Generation::SIX}, {"pk5", pksm::Generation::FIVE},
                {"pk4", pksm::Generation::FOUR}, {"pk3", pksm::Generation::THREE}, {"pk2", pksm::Generation::TWO},
                {"pk1", pksm::Generation::ONE}};
            std::unique_ptr<pksm::PKX> pk;
            if (ext == "pa9")
                pk = pksm::PKX::getPKM<pksm::PA9>(data.data(), data.size());
            else
            {
                for (const auto& [e, gen] : GENS)
                {
                    if (ext == e)
                        pk = pksm::PKX::getPKM(gen, data.data(), data.size());
                }
            }
            if (pk && pk->species() == pksm::Species::None)
                pk.reset();
            return pk;
        }
    }

    // Bank changes wait in memory (mBankPending) like the game's do, and reach the
    // SD card only when the user saves; discarding just forgets them.

    namespace
    {
        bool writeFile(int box, int slot, const pksm::PKX& pk)
        {
            mkdir("sdmc:/switch/monhex", 0777);
            mkdir(BANK_DIR, 0777);
            mkdir(boxDir(box).c_str(), 0777);
            std::string old = findFile(box, slot);
            if (!old.empty())
                remove(old.c_str());
            char path[96];
            snprintf(path, sizeof(path), "%s/%02d.%s", boxDir(box).c_str(), slot + 1, std::string(pk.extension()).c_str());
            auto copy = pk.clone();
            copy->refreshChecksum();
            auto data = copy->rawData();
            FILE* f   = fopen(path, "wb");
            if (!f)
                return false;
            bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
            fclose(f);
            return ok;
        }

        // What is on the SD card for a box, slot by slot
        std::vector<std::unique_ptr<pksm::PKX>> readBoxFiles(int box)
        {
            std::vector<std::unique_ptr<pksm::PKX>> slots(BOX_SLOTS);
            std::string dir = boxDir(box);
            if (DIR* d = opendir(dir.c_str()))
            {
                while (dirent* e = readdir(d))
                {
                    int slot = std::atoi(e->d_name) - 1;
                    if (slot >= 0 && slot < BOX_SLOTS && e->d_name[0] != '.')
                        slots[slot] = loadFile(dir + "/" + e->d_name);
                }
                closedir(d);
            }
            return slots;
        }
    }

    std::unique_ptr<pksm::PKX> App::readBankSlot(int box, int slot) const
    {
        auto it = mBankPending.find({box, slot});
        if (it != mBankPending.end())
            return it->second ? it->second->clone() : nullptr;
        std::string path = findFile(box, slot);
        return path.empty() ? nullptr : loadFile(path);
    }

    bool App::writeBankSlot(int box, int slot, const pksm::PKX& pk)
    {
        mBankPending[{box, slot}] = pk.clone();
        return true;
    }

    void App::clearBankSlot(int box, int slot) { mBankPending[{box, slot}] = nullptr; }

    std::vector<std::unique_ptr<pksm::PKX>> App::bankBox(int box) const
    {
        auto slots = readBoxFiles(box);
        for (const auto& [key, pk] : mBankPending)
        {
            if (key.first == box)
                slots[key.second] = pk ? pk->clone() : nullptr;
        }
        return slots;
    }

    bool App::hasUnsaved(void) const { return mDirty || !mBankPending.empty(); }

    bool App::commitBank(bool additions, std::string& error)
    {
        // Additions first and removals last, so a failure in between can only
        // leave an extra copy, never lose a Pokemon
        for (auto it = mBankPending.begin(); it != mBankPending.end();)
        {
            bool isAddition = it->second != nullptr;
            if (isAddition != additions)
            {
                ++it;
                continue;
            }
            bool ok = true;
            if (isAddition)
                ok = writeFile(it->first.first, it->first.second, *it->second);
            else
            {
                std::string path = findFile(it->first.first, it->first.second);
                if (!path.empty())
                    ok = remove(path.c_str()) == 0;
            }
            if (!ok)
            {
                error = tr("Falha ao gravar o banco no cartão SD.");
                return false;
            }
            it = mBankPending.erase(it);
        }
        return true;
    }

    void App::loadBankSlots(void) { mSlots = bankBox(mBankBox); }

    // ------------------------------------------------------------------ search

    void App::openSearch(void)
    {
        mSearch      = {};
        mSearch.open = true;
        mSearch.time = mGfx.time();
        runWithLoading(tr("Indexando Pokémon"), [&] { buildSearchIndex(); });
        filterSearch();
    }

    void App::buildSearchIndex(void)
    {
        // A light copy of what matters for filtering, so typing stays fast
        auto add = [&](const pksm::PKX& pk, SlotRef ref) {
            SearchEntry e;
            e.ref     = ref;
            e.name    = pk.egg() ? tr("Ovo") : pk.nickname();
            e.speciesName = i18n::species(LANG, pk.species());
            e.shiny   = pk.shiny() && !pk.egg();
            e.level   = pk.level();
            e.type1   = u8(pk.type1());
            e.type2   = u8(pk.type2());
            mSearch.index.push_back(std::move(e));
        };
        const pksm::Sav& sav = *mSave.sav;
        for (int i = 0; i < sav.partyCount(); i++)
            if (auto pk = sav.pkm(u8(i)); pk && pk->species() != pksm::Species::None)
                add(*pk, {true, 0, i});
        for (int box = 0; box < sav.maxBoxes(); box++)
            for (int slot = 0; slot < BOX_SLOTS; slot++)
                if (auto pk = sav.pkm(u8(box), u8(slot)); pk && pk->species() != pksm::Species::None)
                    add(*pk, {false, box, slot});
        for (int box = 0; box < BANK_BOXES; box++)
        {
            auto slots = bankBox(box);
            for (int slot = 0; slot < BOX_SLOTS; slot++)
                if (slots[slot])
                    add(*slots[slot], {false, box, slot, true});
        }
    }

    void App::filterSearch(void)
    {
        auto lower = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
            return s;
        };
        std::string q = lower(mSearch.text);
        mSearch.results.clear();
        for (size_t i = 0; i < mSearch.index.size(); i++)
        {
            const auto& e = mSearch.index[i];
            if (e.ref.bank && !mSearch.includeBank)
                continue;
            if (!q.empty() && lower(e.name).find(q) == std::string::npos &&
                lower(e.speciesName).find(q) == std::string::npos)
                continue;
            if (mSearch.shiny == 1 && !e.shiny)
                continue;
            if (mSearch.shiny == 2 && e.shiny)
                continue;
            if (mSearch.type >= 0 && e.type1 != mSearch.type && e.type2 != mSearch.type)
                continue;
            if (e.level < mSearch.minLevel || e.level > mSearch.maxLevel)
                continue;
            mSearch.results.push_back(int(i));
        }
        mSearch.cursor = std::min(mSearch.cursor, std::max(0, int(mSearch.results.size()) - 1));
        mSearch.scroll = 0;
    }

    void App::jumpTo(const SlotRef& ref)
    {
        mSearch.open = false;
        mBankView    = ref.bank;
        mPartyView   = ref.party;
        if (ref.bank)
            mBankBox = ref.box;
        else if (!ref.party)
            mBox = ref.box;
        mSlotCursor  = ref.slot;
        mBoxAnimTime = -10;
        mPrevSlots.clear();
        mHighlight.snap();
        loadSlots();
    }

    namespace
    {
        constexpr int SEARCH_FILTERS = 6, FILTER_ROW_H = 52, RESULT_ROW_H = 46;
    }

    void App::updateSearch(u64 pressed)
    {
        if (pressed & HidNpadButton_B)
        {
            mSearch.open = false;
            return;
        }
        // L/R or touch moves between filters and results
        if (pressed & (HidNpadButton_L | HidNpadButton_ZL))
            mSearch.onResults = false;
        if (pressed & (HidNpadButton_R | HidNpadButton_ZR))
            mSearch.onResults = !mSearch.results.empty();

        constexpr int X = 64, Y = 56, W = SCREEN_W - 128;
        int fx = X + 28, fw = 400, rx = X + 460, rw = W - 460 - 28;
        int listY = Y + 80, visible = (566 - 110) / RESULT_ROW_H;
        int count = int(mSearch.results.size());

        for (int i = 0; i < SEARCH_FILTERS; i++)
        {
            if (mInput.tappedIn(fx, Y + 80 + i * FILTER_ROW_H, fw, FILTER_ROW_H - 6))
            {
                if (!mSearch.onResults && mSearch.row == i)
                    pressed |= HidNpadButton_A;
                mSearch.onResults = false;
                mSearch.row       = i;
            }
        }
        for (int i = 0; i < visible && mSearch.scroll + i < count; i++)
        {
            if (mInput.tappedIn(rx, listY + i * RESULT_ROW_H, rw, RESULT_ROW_H))
            {
                if (mSearch.onResults && mSearch.cursor == mSearch.scroll + i)
                    pressed |= HidNpadButton_A;
                mSearch.onResults = true;
                mSearch.cursor    = mSearch.scroll + i;
            }
        }

        if (mSearch.onResults)
        {
            if (pressed & KEY_DOWN)
                mSearch.cursor = std::min(mSearch.cursor + 1, count - 1);
            if (pressed & KEY_UP)
                mSearch.cursor = std::max(mSearch.cursor - 1, 0);
            if (pressed & KEY_LEFT)
                mSearch.onResults = false;
            if (mSearch.cursor < mSearch.scroll)
                mSearch.scroll = mSearch.cursor;
            if (mSearch.cursor >= mSearch.scroll + visible)
                mSearch.scroll = mSearch.cursor - visible + 1;
            if ((pressed & HidNpadButton_A) && count > 0)
                jumpTo(mSearch.index[mSearch.results[mSearch.cursor]].ref);
            return;
        }

        if (pressed & KEY_DOWN)
            mSearch.row = std::min(mSearch.row + 1, SEARCH_FILTERS - 1);
        if (pressed & KEY_UP)
            mSearch.row = std::max(mSearch.row - 1, 0);
        int delta = (pressed & KEY_RIGHT) ? 1 : ((pressed & KEY_LEFT) ? -1 : 0);
        if ((pressed & HidNpadButton_A) && mSearch.row != 0)
            delta = 1;
        bool changed = false;
        switch (mSearch.row)
        {
            case 0: // name or species
                if (pressed & (HidNpadButton_A | KEY_RIGHT))
                {
                    SwkbdConfig kbd;
                    if (R_SUCCEEDED(swkbdCreate(&kbd, 0)))
                    {
                        swkbdConfigMakePresetDefault(&kbd);
                        swkbdConfigSetHeaderText(&kbd, tr("Nome ou espécie"));
                        swkbdConfigSetInitialText(&kbd, mSearch.text.c_str());
                        swkbdConfigSetStringLenMax(&kbd, 20);
                        char out[64] = {};
                        if (R_SUCCEEDED(swkbdShow(&kbd, out, sizeof(out))))
                        {
                            mSearch.text = out;
                            changed      = true;
                        }
                        swkbdClose(&kbd);
                    }
                }
                if (pressed & KEY_LEFT && !mSearch.text.empty())
                    mSearch.text.clear(), changed = true;
                break;
            case 1:
                if (delta)
                    mSearch.shiny = (mSearch.shiny + (delta > 0 ? 1 : 2)) % 3, changed = true;
                break;
            case 2:
                if (delta)
                    mSearch.type = (mSearch.type + 1 + (delta > 0 ? 1 : 18)) % 19 - 1, changed = true;
                break;
            case 3:
                if (delta)
                    mSearch.minLevel = std::clamp(mSearch.minLevel + delta * ((pressed & HidNpadButton_A) ? 10 : 1), 1,
                                           mSearch.maxLevel),
                    changed = true;
                break;
            case 4:
                if (delta)
                    mSearch.maxLevel = std::clamp(mSearch.maxLevel + delta * ((pressed & HidNpadButton_A) ? 10 : 1),
                                           mSearch.minLevel, 100),
                    changed = true;
                break;
            case 5:
                if (delta)
                    mSearch.includeBank = !mSearch.includeBank, changed = true;
                break;
        }
        if (changed)
            filterSearch();
    }

    void App::drawSearch(void)
    {
        float in = easeOut((mGfx.time() - mSearch.time) / 0.25f);
        mGfx.fillRect(0, 0, SCREEN_W, SCREEN_H, Color{8, 9, 16, u8(170 * in)});
        constexpr int X = 64, Y = 56, W = SCREEN_W - 128, H = 566;
        mGfx.setOpacity(in);
        mGfx.setOrigin(0, int((1 - in) * 30));
        mGfx.shadow(X, Y + 8, W, H, 24, 28, Color{0, 0, 0, 160});
        mGfx.fillRoundRect(X, Y, W, H, 24, Color{34, 36, 50, 250});
        mGfx.text(tr("Buscar Pokémon"), X + 28, Y + 24, 28, theme::TEXT);

        // Filters on the left
        int fx = X + 28, fw = 400;
        static const char* SHINY[] = {"Qualquer", "Só shiny", "Sem shiny"};
        std::string values[SEARCH_FILTERS] = {mSearch.text.empty() ? tr("(qualquer)") : mSearch.text, tr(SHINY[mSearch.shiny]),
            mSearch.type < 0 ? tr("Qualquer") : typeName(u8(mSearch.type)), std::to_string(mSearch.minLevel),
            std::to_string(mSearch.maxLevel), mSearch.includeBank ? tr("Sim") : tr("Não")};
        static const char* LABELS[SEARCH_FILTERS] = {"Nome ou espécie", "Shiny", "Tipo", "Nível mínimo",
            "Nível máximo", "Incluir o banco"};
        for (int i = 0; i < SEARCH_FILTERS; i++)
        {
            int y    = Y + 80 + i * FILTER_ROW_H;
            bool sel = !mSearch.onResults && mSearch.row == i;
            mGfx.fillRoundRect(fx, y, fw, FILTER_ROW_H - 6, 12, sel ? Color{0, 190, 255, 55} : Color{255, 255, 255, 12});
            mGfx.text(tr(LABELS[i]), fx + 16, y + (FILTER_ROW_H - 6 - mGfx.textHeight(18)) / 2, 18,
                sel ? theme::TEXT : theme::MUTED);
            int vx = fx + fw - 16;
            if (i == 2 && mSearch.type >= 0)
            {
                typeBadge(mGfx, u8(mSearch.type), vx - 120, y + 10, 26, 14);
                continue;
            }
            mGfx.text(values[i], vx, y + (FILTER_ROW_H - 6 - mGfx.textHeight(18)) / 2, 18, theme::TEXT, Align::Right,
                200);
        }
        mGfx.text(std::to_string(mSearch.results.size()) + tr(" encontrados"), fx, Y + 80 + SEARCH_FILTERS * FILTER_ROW_H + 10,
            18, theme::DIM);

        // Results on the right
        int rx = X + 460, rw = W - 460 - 28, listY = Y + 80;
        int visible = (H - 110) / RESULT_ROW_H;
        int count   = int(mSearch.results.size());
        mGfx.text(tr("Resultados"), rx, Y + 50, 16, theme::DIM);
        if (count == 0)
            mGfx.text(tr("Nenhum Pokémon com esses filtros."), rx + rw / 2, listY + 60, 20, theme::DIM, Align::Center);
        for (int i = 0; i < visible && mSearch.scroll + i < count; i++)
        {
            int index         = mSearch.scroll + i;
            const auto& e     = mSearch.index[mSearch.results[index]];
            int y             = listY + i * RESULT_ROW_H;
            bool sel          = mSearch.onResults && index == mSearch.cursor;
            mGfx.fillRoundRect(rx, y + 2, rw, RESULT_ROW_H - 4, 10,
                sel ? Color{0, 190, 255, 55} : Color{255, 255, 255, u8(i % 2 ? 6 : 12)});
            typeCircle(mGfx, e.type1, rx + 22, y + RESULT_ROW_H / 2, 11);
            std::string name = e.name == e.speciesName ? e.name : e.name + " (" + e.speciesName + ")";
            int nw = mGfx.text(name, rx + 44, y + (RESULT_ROW_H - mGfx.textHeight(19)) / 2, 19, theme::TEXT, Align::Left,
                rw - 330);
            if (e.shiny)
                drawShinyMark(rx + 44 + nw + 16, y + RESULT_ROW_H / 2, 16);
            std::string where = e.ref.party ? tr("Equipe")
                                : e.ref.bank ? tr("Banco ") + std::to_string(e.ref.box + 1)
                                             : boxTitle(e.ref.box);
            where += " · " + std::to_string(e.ref.slot + 1);
            mGfx.text("Nv. " + std::to_string(e.level), rx + rw - 200, y + (RESULT_ROW_H - mGfx.textHeight(17)) / 2, 17,
                theme::MUTED, Align::Right);
            mGfx.text(where, rx + rw - 14, y + (RESULT_ROW_H - mGfx.textHeight(17)) / 2, 17, theme::MUTED, Align::Right,
                170);
        }
        mGfx.resetLayer();
        mHintAreas.clear();
        drawFooter({{0, glyph::L, tr("Filtros")}, {0, glyph::R, tr("Resultados")}, {HidNpadButton_A, glyph::A,
                       mSearch.onResults ? tr("Ir até ele") : tr("Mudar")},
            {HidNpadButton_B, glyph::B, tr("Fechar")}});
    }
}
