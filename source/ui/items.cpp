#include "ui/items.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

#include "data/tmdata.hpp"
#include "lang.hpp"

namespace monhex::ui
{
    namespace
    {
        // "TM12", "TM 12", "TR05", "TM100" -> kind and number
        bool parseMachine(const std::string& name, char& kind, int& number)
        {
            if (name.size() < 3 || name[0] != 'T' || (name[1] != 'M' && name[1] != 'R'))
                return false;
            size_t i = 2;
            while (i < name.size() && name[i] == ' ')
                i++;
            if (i >= name.size() || name[i] < '0' || name[i] > '9')
                return false;
            kind   = name[1];
            number = std::atoi(name.c_str() + i);
            return true;
        }

        template <size_t N>
        u16 lookup(const u16 (&table)[N], int number)
        {
            return number >= 0 && size_t(number) < N ? table[number] : 0;
        }
    }

    SDL_Texture* itemIcon(Gfx& gfx, u16 id, const std::string& englishName)
    {
        char path[64];
        snprintf(path, sizeof(path), "romfs:/items/aitem_%u.png", id);
        if (SDL_Texture* t = gfx.texture(path))
            return t;
        snprintf(path, sizeof(path), "romfs:/items/bitem_%u.png", id);
        if (SDL_Texture* t = gfx.texture(path, true))
            return t;
        char kind;
        int number;
        if (parseMachine(englishName, kind, number))
        {
            if (SDL_Texture* t = gfx.texture(kind == 'R' ? "romfs:/items/bitem_tr.png" : "romfs:/items/aitem_tm.png"))
                return t;
        }
        return gfx.texture("romfs:/items/bitem_unk.png", true);
    }

    pksm::Move machineMove(pksm::GameVersion game, const std::string& englishName)
    {
        char kind;
        int number;
        if (!parseMachine(englishName, kind, number))
            return pksm::Move::None;
        u16 move = 0;
        if (game == pksm::GameVersion::GP || game == pksm::GameVersion::GE)
            move = kind == 'M' ? lookup(data::LGPE_TM, number) : 0;
        else if (game == pksm::GameVersion::SW || game == pksm::GameVersion::SH)
            move = kind == 'M' ? lookup(data::SWSH_TM, number) : lookup(data::SWSH_TR, number);
        else if (game == pksm::GameVersion::SL || game == pksm::GameVersion::VL)
            move = kind == 'M' ? lookup(data::SV_TM, number) : 0;
        else if (game == pksm::GameVersion::ZA)
            move = kind == 'M' ? lookup(data::ZA_TM, number) : 0;
        return pksm::Move{move};
    }

    namespace
    {
        // "romfs:/<kind>/desc_<lang>.txt": lines of "id|text", loaded once per file
        const std::unordered_map<u16, std::string>& descriptions(const std::string& path)
        {
            static std::unordered_map<std::string, std::unordered_map<u16, std::string>> files;
            auto it = files.find(path);
            if (it != files.end())
                return it->second;
            auto& texts = files[path];
            if (FILE* f = fopen(path.c_str(), "r"))
            {
                char line[1024];
                while (fgets(line, sizeof(line), f))
                {
                    char* bar = strchr(line, '|');
                    if (!bar)
                        continue;
                    std::string text = bar + 1;
                    while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
                        text.pop_back();
                    texts[u16(std::atoi(line))] = text;
                }
                fclose(f);
            }
            return texts;
        }

        // In the menu language where there is one (Portuguese uses English)
        const std::string& describe(const char* kind, u16 id)
        {
            static const std::string empty;
            const char* code = uiLanguage() == UiLanguage::Spanish ? "es"
                               : uiLanguage() == UiLanguage::French ? "fr"
                                                                    : "en";
            for (const char* c : {code, "en"})
            {
                const auto& texts = descriptions(std::string("romfs:/") + kind + "/desc_" + c + ".txt");
                auto it = texts.find(id);
                if (it != texts.end())
                    return it->second;
            }
            return empty;
        }
    }

    const std::string& itemDescription(u16 id) { return describe("items", id); }

    const std::string& abilityDescription(u16 id) { return describe("abilities", id); }
}
