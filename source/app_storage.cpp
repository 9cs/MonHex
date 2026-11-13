// Organising Pokemon: the slot menu, moving/swapping/cloning (one or many),
// releasing, and exporting/importing Pokemon files
#include <algorithm>
#include <cmath>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

#include "app.hpp"
#include "lang.hpp"
#include "pkx/PA9.hpp"
#include "theme.hpp"
#include "ui/sprites.hpp"
#include "utils/i18n.hpp"

using namespace monhex::ui;
using namespace monhex::layout;

namespace monhex
{
    namespace
    {
        constexpr const char* PKM_DIR = "sdmc:/switch/monhex/pkm";

        // File extension -> generation, for importing
        struct Extension
        {
            const char* ext;
            pksm::Generation gen;
        };
        const Extension EXTENSIONS[] = {{"pk9", pksm::Generation::NINE}, {"pa9", pksm::Generation::NINE},
            {"pk8", pksm::Generation::EIGHT}, {"pa8", pksm::Generation::EIGHT},
            {"pb7", pksm::Generation::LGPE}, {"pk7", pksm::Generation::SEVEN}, {"pk6", pksm::Generation::SIX},
            {"pk5", pksm::Generation::FIVE}, {"pk4", pksm::Generation::FOUR}, {"pk3", pksm::Generation::THREE},
            {"pk2", pksm::Generation::TWO}, {"pk1", pksm::Generation::ONE}};

        std::string lowerExt(const std::string& name)
        {
            size_t dot = name.rfind('.');
            if (dot == std::string::npos)
                return "";
            std::string ext = name.substr(dot + 1);
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
            return ext;
        }

        const char* transferProblem(pksm::Sav::BadTransferReason r)
        {
            switch (r)
            {
                case pksm::Sav::BadTransferReason::MOVE:
                    return tr("tem um golpe que não existe neste jogo");
                case pksm::Sav::BadTransferReason::SPECIES:
                    return tr("é de uma espécie que não existe neste jogo");
                case pksm::Sav::BadTransferReason::FORM:
                    return tr("está numa forma que não existe neste jogo");
                case pksm::Sav::BadTransferReason::ABILITY:
                    return tr("tem uma habilidade que não existe neste jogo");
                case pksm::Sav::BadTransferReason::ITEM:
                    return tr("segura um item que não existe neste jogo");
                case pksm::Sav::BadTransferReason::BALL:
                    return tr("está numa Poké Ball que não existe neste jogo");
                default:
                    return nullptr;
            }
        }
    }

    // ------------------------------------------------------------------ slots

    App::SlotRef App::currentSlot(void) const
    {
        return {mPartyView, mBankView ? mBankBox : mBox, mSlotCursor, mBankView};
    }

    std::unique_ptr<pksm::PKX> App::readSlot(const SlotRef& ref) const
    {
        if (ref.bank)
            return readBankSlot(ref.box, ref.slot);
        const pksm::Sav& sav = *mSave.sav;
        std::unique_ptr<pksm::PKX> pk;
        if (ref.party)
            pk = ref.slot < sav.partyCount() ? sav.pkm(u8(ref.slot)) : nullptr;
        else
            pk = sav.pkm(u8(ref.box), u8(ref.slot));
        if (pk && pk->species() == pksm::Species::None)
            pk.reset();
        return pk;
    }

    bool App::fitsGame(const pksm::PKX& pk, std::string& why) const
    {
        // Same format as the game: nothing to convert
        if (pk.extension() == mSave.sav->emptyPkm()->extension())
            return true;
        if (const char* problem = transferProblem(mSave.sav->invalidTransferReason(pk)))
        {
            why = i18n::species(LANG, pk.species()) + " " + problem + ".";
            return false;
        }
        return true;
    }

    bool App::writeSlot(const SlotRef& ref, const pksm::PKX& pk)
    {
        if (ref.bank)
            return writeBankSlot(ref.box, ref.slot, pk); // the bank keeps every format as it is
        std::unique_ptr<pksm::PKX> converted;
        const pksm::PKX* toWrite = &pk;
        if (pk.extension() != mSave.sav->emptyPkm()->extension())
        {
            converted = mSave.sav->transfer(pk);
            if (!converted)
                return false;
            converted->refreshChecksum();
            toWrite = converted.get();
        }
        if (ref.party)
            mSave.sav->pkm(*toWrite, u8(ref.slot));
        else
            mSave.sav->pkm(*toWrite, u8(ref.box), u8(ref.slot), false);
        return true;
    }

    void App::clearSlot(const SlotRef& ref)
    {
        if (ref.bank)
        {
            clearBankSlot(ref.box, ref.slot);
            return;
        }
        auto empty = mSave.sav->emptyPkm();
        writeSlot(ref, *empty);
    }

    bool App::isMarked(const SlotRef& ref) const
    {
        return std::any_of(mMarked.begin(), mMarked.end(), [&](const SlotRef& m) { return (m == ref); });
    }

    void App::toggleMark(void)
    {
        SlotRef ref = currentSlot();
        if (!selectedPokemon())
            return;
        auto it = std::find_if(mMarked.begin(), mMarked.end(), [&](const SlotRef& m) { return (m == ref); });
        if (it != mMarked.end())
            mMarked.erase(it);
        else
            mMarked.push_back(ref);
    }

    // The slots an action applies to: the marked ones, or else the one under the cursor
    std::vector<App::SlotRef> App::actionSlots(void) const
    {
        if (!mMarked.empty())
            return mMarked;
        if (selectedPokemon())
            return {currentSlot()};
        return {};
    }

    int App::partyLeftAfterRemoving(const std::vector<SlotRef>& refs) const
    {
        int removed = int(std::count_if(refs.begin(), refs.end(), [](const SlotRef& r) { return r.party; }));
        return mSave.sav->partyCount() - removed;
    }

    void App::afterStorageChange(bool gameChanged)
    {
        if (gameChanged)
        {
            mSave.sav->fixParty();
            mDirty = true;
        }
        mMarked.clear();
        loadSlots();
        mSummaryKey = -1;
    }

    // ------------------------------------------------------------------ holding

    void App::pickUp(bool clone)
    {
        std::vector<SlotRef> refs = actionSlots();
        if (refs.empty())
            return;
        mHeld.clear();
        for (const auto& ref : refs)
        {
            if (auto pk = readSlot(ref))
                mHeld.push_back({std::move(pk), ref});
        }
        mHeldClone = clone;
        mMarked.clear();
        mHoldTime  = mGfx.time();
    }

    bool App::isHeldOrigin(const SlotRef& ref) const
    {
        if (mHeldClone)
            return false;
        return std::any_of(mHeld.begin(), mHeld.end(), [&](const Held& h) { return (h.from == ref); });
    }

    void App::dropHeld(void)
    {
        if (mHeld.empty())
            return;
        pksm::Sav& sav = *mSave.sav;
        SlotRef target = currentSlot();
        auto occupant  = readSlot(target);
        std::string why;

        // One Pokemon onto another: they swap places (or, cloning, it's refused)
        if (mHeld.size() == 1 && occupant)
        {
            const SlotRef from = mHeld[0].from;
            if (!mHeldClone && (from == target))
            {
                mHeld.clear(); // dropped back where it was
                return;
            }
            if (mHeldClone)
            {
                showToast(tr("Escolha um espaço vazio para a cópia."));
                return;
            }
            // Each must be able to live where the other was
            if ((!target.bank && !fitsGame(*mHeld[0].pk, why)) || (!from.bank && !fitsGame(*occupant, why)))
            {
                showDialog(tr("Não dá para trocar"), why, {{HidNpadButton_B, glyph::B, "OK", DialogAction::None}});
                return;
            }
            writeSlot(from, *occupant);
            writeSlot(target, *mHeld[0].pk);
            mHeld.clear();
            afterStorageChange(!from.bank || !target.bank);
            showToast(tr("Pokémon trocados de lugar."));
            return;
        }

        if (!target.bank)
        {
            for (const auto& h : mHeld)
            {
                if (!fitsGame(*h.pk, why))
                {
                    showDialog(tr("Não dá para colocar neste jogo"), why,
                        {{HidNpadButton_B, glyph::B, "OK", DialogAction::None}});
                    return;
                }
            }
        }

        // Otherwise they go into empty spaces from the cursor onwards
        std::vector<SlotRef> spaces;
        if (target.party)
        {
            for (int i = sav.partyCount(); i < 6 && spaces.size() < mHeld.size(); i++)
                spaces.push_back({true, 0, i});
        }
        else
        {
            int boxes = target.bank ? BANK_BOXES : sav.maxBoxes();
            for (int box = target.box; box < boxes && spaces.size() < mHeld.size(); box++)
            {
                for (int slot = box == target.box ? target.slot : 0; slot < BOX_SLOTS && spaces.size() < mHeld.size();
                     slot++)
                {
                    SlotRef ref{false, box, slot, target.bank};
                    if (!readSlot(ref))
                        spaces.push_back(ref);
                }
            }
        }
        if (spaces.size() < mHeld.size())
        {
            showToast(target.party ? tr("Não cabe na equipe (máximo de 6).") : tr("Não há espaços vazios suficientes daqui em diante."));
            return;
        }
        if (!mHeldClone)
        {
            std::vector<SlotRef> origins;
            for (const auto& h : mHeld)
                origins.push_back(h.from);
            int left = partyLeftAfterRemoving(origins) + int(target.party ? mHeld.size() : 0);
            if (left < 1)
            {
                showToast(tr("A equipe precisa de pelo menos um Pokémon."));
                return;
            }
        }
        // Party spaces are taken in order, so write them before touching origins
        bool gameChanged = !target.bank;
        for (size_t i = 0; i < mHeld.size(); i++)
        {
            if (!writeSlot(spaces[i], *mHeld[i].pk))
            {
                showToast(tr("Falha ao colocar um dos Pokémon. Nada mais foi alterado."));
                afterStorageChange(gameChanged);
                mHeld.clear();
                return;
            }
            if (spaces[i].party)
                sav.partyCount(u8(std::max<int>(sav.partyCount(), spaces[i].slot + 1)));
        }
        if (!mHeldClone)
        {
            for (const auto& h : mHeld)
            {
                clearSlot(h.from);
                gameChanged |= !h.from.bank;
            }
        }
        size_t n = mHeld.size();
        bool clone = mHeldClone;
        mHeld.clear();
        afterStorageChange(gameChanged);
        showToast(std::to_string(n) + (clone ? (n == 1 ? tr(" cópia criada.") : tr(" cópias criadas."))
                                             : (n == 1 ? tr(" Pokémon movido.") : tr(" Pokémon movidos."))));
    }

    void App::releaseSelected(void)
    {
        std::vector<SlotRef> refs = actionSlots();
        if (refs.empty())
            return;
        if (partyLeftAfterRemoving(refs) < 1)
        {
            showToast(tr("A equipe precisa de pelo menos um Pokémon."));
            return;
        }
        bool gameChanged = false;
        for (const auto& ref : refs)
        {
            clearSlot(ref);
            gameChanged |= !ref.bank;
        }
        afterStorageChange(gameChanged);
        showToast(refs.size() == 1 ? tr("Pokémon solto.") : std::to_string(refs.size()) + tr(" Pokémon soltos."));
    }

    void App::askRelease(void)
    {
        std::vector<SlotRef> refs = actionSlots();
        if (refs.empty())
            return;
        std::string what = refs.size() == 1 ? tr("este Pokémon") : std::to_string(refs.size()) + tr(" Pokémon");
        showDialog(tr("Soltar ") + what + "?",
            tr("Eles somem deste save quando você gravar.\nSe mudar de ideia antes, saia sem gravar."),
            {{HidNpadButton_A, glyph::A, tr("Soltar"), DialogAction::Release},
                {HidNpadButton_B, glyph::B, tr("Cancelar"), DialogAction::None}});
    }

    // ------------------------------------------------------------------ files

    void App::exportSelected(void)
    {
        std::vector<SlotRef> refs = actionSlots();
        mkdir("sdmc:/switch/monhex", 0777);
        mkdir(PKM_DIR, 0777);
        int saved = 0;
        for (const auto& ref : refs)
        {
            auto pk = readSlot(ref);
            if (!pk)
                continue;
            // "0025 - Pikachu - 1A2B3C4D.pk9", with the nickname in plain letters
            std::string name;
            for (unsigned char c : pk->nickname())
                if (std::isalnum(c) || c == ' ' || c == '-')
                    name += char(c);
            if (name.empty())
                name = "pokemon";
            char file[160];
            snprintf(file, sizeof(file), "%s/%04u - %s - %08X.%s", PKM_DIR, u16(pk->species()), name.c_str(),
                pk->PID(), std::string(pk->extension()).c_str());
            pk->refreshChecksum();
            auto data = pk->rawData();
            if (FILE* f = fopen(file, "wb"))
            {
                if (fwrite(data.data(), 1, data.size(), f) == data.size())
                    saved++;
                fclose(f);
            }
        }
        mMarked.clear();
        if (saved == 0)
            showToast(tr("Não foi possível exportar."));
        else
            showToast(std::to_string(saved) + (saved == 1 ? tr(" arquivo salvo") : tr(" arquivos salvos")) + " em " + PKM_DIR);
    }

    void App::openImport(void)
    {
        mImportFiles.clear();
        for (const char* dir : {PKM_DIR, "sdmc:/pkm"})
        {
            DIR* d = opendir(dir);
            if (!d)
                continue;
            while (dirent* e = readdir(d))
            {
                std::string name = e->d_name;
                std::string ext  = lowerExt(name);
                if (e->d_type != DT_DIR &&
                    std::any_of(std::begin(EXTENSIONS), std::end(EXTENSIONS), [&](const Extension& x) { return ext == x.ext; }))
                    mImportFiles.push_back(std::string(dir) + "/" + name);
            }
            closedir(d);
        }
        std::sort(mImportFiles.begin(), mImportFiles.end());
        if (mImportFiles.empty())
        {
            showDialog(tr("Nenhum arquivo para importar"),
                tr("Coloque arquivos .pk9, .pa9, .pk8, .pa8, .pb7... em\nsdmc:/switch/monhex/pkm (ou sdmc:/pkm) e tente de novo."),
                {{HidNpadButton_B, glyph::B, "OK", DialogAction::None}});
            return;
        }
        if (!mDetail)
            openDetail();
        mPicker       = {};
        mPicker.kind  = PickerKind::ImportFile;
        mPicker.title = tr("arquivo");
        mPicker.time  = mGfx.time();
        mPicker.area  = editorArea();
        for (size_t i = 0; i < mImportFiles.size(); i++)
        {
            std::string shown = mImportFiles[i].substr(mImportFiles[i].rfind('/') + 1);
            mPicker.items.push_back({int(i), shown, 255});
        }
        filterPicker();
        mPicker.open = true;
    }

    void App::importFile(const std::string& path)
    {
        mDetail = false;
        std::string ext = lowerExt(path);
        auto it = std::find_if(std::begin(EXTENSIONS), std::end(EXTENSIONS), [&](const Extension& x) { return ext == x.ext; });
        FILE* f = fopen(path.c_str(), "rb");
        std::vector<u8> data;
        if (f)
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
        std::unique_ptr<pksm::PKX> pk;
        // PA9 (Legends: Z-A) shares generation 9 with PK9 but is its own format.
        // BDSP's PB8 isn't supported by PKSM-Core and isn't offered.
        if (!data.empty() && ext == "pa9")
            pk = pksm::PKX::getPKM<pksm::PA9>(data.data(), data.size());
        else if (!data.empty() && it != std::end(EXTENSIONS))
            pk = pksm::PKX::getPKM(it->gen, data.data(), data.size());
        if (!pk || pk->species() == pksm::Species::None)
        {
            showDialog(tr("Arquivo inválido"), tr("Esse arquivo não parece ser um Pokémon válido."),
                {{HidNpadButton_B, glyph::B, "OK", DialogAction::None}});
            return;
        }
        SlotRef target = currentSlot();
        if (target.bank)
        {
            writeBankSlot(target.box, target.slot, *pk);
            afterStorageChange(false);
            showToast(tr("Pokémon importado para o banco. Aperte ZL para gravar."));
            return;
        }
        if (const char* problem = transferProblem(mSave.sav->invalidTransferReason(*pk)))
        {
            showDialog(tr("Não dá para importar"), std::string(tr("Esse Pokémon ")) + problem + ".",
                {{HidNpadButton_B, glyph::B, "OK", DialogAction::None}});
            return;
        }
        auto converted = mSave.sav->transfer(*pk);
        if (!converted)
        {
            showDialog(tr("Não dá para importar"), tr("Não foi possível converter esse Pokémon para este jogo."),
                {{HidNpadButton_B, glyph::B, "OK", DialogAction::None}});
            return;
        }
        if (target.party)
            target.slot = mSave.sav->partyCount();
        if (target.party && target.slot >= 6)
        {
            showToast(tr("A equipe está cheia."));
            return;
        }
        converted->refreshChecksum();
        writeSlot(target, *converted);
        if (target.party)
            mSave.sav->partyCount(u8(target.slot + 1));
        afterStorageChange(true);
        showToast(tr("Pokémon importado. Aperte ZL para gravar no jogo."));
    }

    // ------------------------------------------------------------------ slot menu

    void App::openSlotMenu(void)
    {
        mMenu         = {};
        mMenu.time    = mGfx.time();
        SDL_Rect r    = mPartyView ? partyCard(mSlotCursor) : gridCell(mSlotCursor);
        mMenu.anchor  = r;
        bool hasPk    = selectedPokemon() != nullptr;
        bool canAdd   = !mPartyView || mSave.sav->partyCount() < 6;
        if (!mMarked.empty())
        {
            std::string n = std::to_string(mMarked.size());
            mMenu.options = {{tr("Mover ") + n + tr(" marcados"), MenuAction::Move}, {tr("Clonar ") + n + tr(" marcados"), MenuAction::Clone},
                {tr("Exportar ") + n + tr(" marcados"), MenuAction::Export}, {tr("Soltar ") + n + tr(" marcados"), MenuAction::Release},
                {tr("Desmarcar todos"), MenuAction::ClearMarks}};
        }
        else if (hasPk)
        {
            mMenu.options = {{tr("Ver detalhes"), MenuAction::Details}, {tr("Editar"), MenuAction::Edit},
                {tr("Mover"), MenuAction::Move}, {tr("Clonar"), MenuAction::Clone}, {tr("Exportar arquivo"), MenuAction::Export},
                {tr("Soltar"), MenuAction::Release}};
        }
        else if (canAdd)
        {
            mMenu.options = {{tr("Criar Pokémon"), MenuAction::Create}, {tr("Importar arquivo"), MenuAction::Import}};
        }
        else
        {
            return;
        }
        mMenu.open = true;
    }

    void App::runMenuAction(MenuAction action)
    {
        mMenu.open = false;
        switch (action)
        {
            case MenuAction::Details:
                openDetail();
                break;
            case MenuAction::Edit:
                openDetail();
                startEditing();
                break;
            case MenuAction::Move:
                pickUp(false);
                break;
            case MenuAction::Clone:
                pickUp(true);
                break;
            case MenuAction::Export:
                exportSelected();
                break;
            case MenuAction::Release:
                askRelease();
                break;
            case MenuAction::ClearMarks:
                mMarked.clear();
                break;
            case MenuAction::Create:
                openDetail();
                openPicker(PickerKind::CreateSpecies, 0);
                break;
            case MenuAction::Import:
                openImport();
                break;
        }
    }

    SDL_Rect App::menuRect(void) const
    {
        constexpr int W = 290, ROW = 46;
        int h = int(mMenu.options.size()) * ROW + 16;
        int x = mMenu.anchor.x + mMenu.anchor.w + 10;
        if (x + W > SCREEN_W - 20)
            x = mMenu.anchor.x - W - 10;
        int y = std::clamp(mMenu.anchor.y, CONTENT_Y, FOOTER_Y - h - 10);
        return {x, y, W, h};
    }

    void App::updateMenu(u64 pressed)
    {
        int n = int(mMenu.options.size());
        if (pressed & HidNpadButton_B)
        {
            mMenu.open = false;
            return;
        }
        if (pressed & KEY_DOWN)
            mMenu.cursor = (mMenu.cursor + 1) % n;
        if (pressed & KEY_UP)
            mMenu.cursor = (mMenu.cursor + n - 1) % n;
        SDL_Rect r = menuRect();
        for (int i = 0; i < n; i++)
        {
            if (mInput.tappedIn(r.x, r.y + 8 + i * 46, r.w, 46))
            {
                mMenu.cursor = i;
                pressed |= HidNpadButton_A;
            }
        }
        if (mInput.tapped() && !mInput.tappedIn(r.x, r.y, r.w, r.h))
        {
            mMenu.open = false; // tapping elsewhere closes it
            return;
        }
        if (pressed & HidNpadButton_A)
            runMenuAction(mMenu.options[mMenu.cursor].action);
    }

    void App::drawMenu(void)
    {
        SDL_Rect r = menuRect();
        float in   = easeOut((mGfx.time() - mMenu.time) / 0.16f);
        mGfx.setOpacity(in);
        mGfx.setOrigin(int((1 - in) * (r.x > mMenu.anchor.x ? -12 : 12)), 0);
        mGfx.shadow(r.x, r.y + 6, r.w, r.h, 16, 18, Color{0, 0, 0, 150});
        mGfx.fillRoundRect(r.x, r.y, r.w, r.h, 16, Color{44, 47, 64, 250});
        for (int i = 0; i < int(mMenu.options.size()); i++)
        {
            int y    = r.y + 8 + i * 46;
            bool sel = i == mMenu.cursor;
            bool danger = mMenu.options[i].action == MenuAction::Release;
            if (sel)
                mGfx.fillRoundRect(r.x + 8, y, r.w - 16, 42, 10, danger ? Color{230, 70, 80, 70} : Color{0, 190, 255, 60});
            mGfx.text(mMenu.options[i].label, r.x + 24, y + (42 - mGfx.textHeight(20)) / 2, 20,
                danger ? Color{255, 150, 150} : theme::TEXT, Align::Left, r.w - 40);
        }
        mGfx.resetLayer();
    }

    // ------------------------------------------------------------------ held Pokemon drawing

    void App::drawHeld(int cx, int bottomY)
    {
        // The held Pokemon float above the pointer; several are stacked with a count
        float bob = std::sin((mGfx.time() - mHoldTime) * 5.0f) * 4.0f;
        int shown = std::min<int>(3, int(mHeld.size()));
        for (int i = shown - 1; i >= 0; i--)
        {
            Sprite s = boxSprite(mGfx, *mHeld[i].pk);
            int dw = int(s.w * BOX_SPRITE_SCALE), dh = int(s.h * BOX_SPRITE_SCALE);
            int x = cx - dw / 2 + i * 8, y = bottomY - 34 - dh + int(bob) - i * 6;
            mGfx.draw(s.tex, x, y, dw, dh, u8(255 - i * 60));
        }
        if (mHeld.size() > 1)
        {
            std::string n = std::to_string(mHeld.size());
            int w = mGfx.textWidth(n, 16) + 16;
            mGfx.fillRoundRect(cx + 26, bottomY - 92 + int(bob), w, 24, 12, theme::RED);
            mGfx.text(n, cx + 26 + w / 2, bottomY - 92 + int(bob) + (24 - mGfx.textHeight(16)) / 2, 16, theme::TEXT,
                Align::Center);
        }
        if (mHeldClone)
            mGfx.text(tr("cópia"), cx, bottomY - 22 + int(bob), 14, theme::CYAN_A, Align::Center);
    }
}
