#include "ui/sprites.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace monhex::ui
{
    namespace
    {
        constexpr const char* BOX_DIR  = "romfs:/sprites/";
        constexpr const char* HOME_DIR = "romfs:/home/";

        // Naming follows PKHeX's SpriteName.cs:
        // <a|b>_<species>[-<form>][p][f][s].png
        constexpr u16 DEFAULT_FORM_SPRITE[] = {414, 664, 665, 744, 778, 854, 855, 892, 982, 1012, 1013};
        constexpr u16 GENDERED_SPRITE[]     = {449, 450, 521, 592, 593, 668};
        constexpr u16 PIKACHU = 25, EEVEE = 133, ALCREMIE = 869;

        template <size_t N>
        bool contains(const u16 (&list)[N], u16 v)
        {
            return std::find(std::begin(list), std::end(list), v) != std::end(list);
        }

        u16 displayForm(const pksm::PKX& pk)
        {
            u16 species = u16(pk.species());
            return contains(DEFAULT_FORM_SPRITE, species) ? 0 : pk.alternativeForm();
        }

        std::string boxName(u16 species, u16 form, bool female, bool shiny)
        {
            std::string name = std::to_string(species);
            if (form != 0 || species == ALCREMIE)
            {
                name += "-" + std::to_string(form);
                if ((species == PIKACHU && form == 8) || (species == EEVEE && form == 1))
                    name += "p";
            }
            if (species == ALCREMIE)
                name += "-0"; // sweet decoration; the plain one is close enough
            if (female && contains(GENDERED_SPRITE, species))
                name += "f";
            if (shiny)
                name += "s";
            return name;
        }

        Sprite load(Gfx& gfx, const std::string& path, bool pixelArt)
        {
            Sprite s;
            if (pixelArt)
            {
                s.tex      = gfx.pixelTexture(path, s.w, s.h);
                s.pixelArt = s.tex != nullptr;
                return s;
            }
            s.tex = gfx.texture(path, false);
            if (s.tex)
                SDL_QueryTexture(s.tex, nullptr, nullptr, &s.w, &s.h);
            return s;
        }
    }

    Sprite boxSprite(Gfx& gfx, const pksm::PKX& pk)
    {
        if (pk.egg())
        {
            Sprite s = load(gfx, std::string(BOX_DIR) + "b_egg.png", true);
            return s.tex ? s : load(gfx, std::string(BOX_DIR) + "a_egg.png", false);
        }
        u16 species = u16(pk.species());
        u16 form    = displayForm(pk);
        bool female = pk.gender() == pksm::Gender::Female;
        bool shiny  = pk.shiny();

        // Pixel art ("b_") first; the newest species only exist as artwork ("a_").
        // Try shiny before normal colours, and the exact form before the base one.
        std::vector<bool> colours = shiny ? std::vector<bool>{true, false} : std::vector<bool>{false};
        std::vector<u16> forms    = form ? std::vector<u16>{form, 0} : std::vector<u16>{0};
        for (bool s : colours)
        {
            for (u16 f : forms)
            {
                std::string n = boxName(species, f, female, s);
                if (Sprite sp = load(gfx, BOX_DIR + ("b_" + n) + ".png", true); sp.tex)
                    return sp;
                if (Sprite sp = load(gfx, BOX_DIR + ("a_" + n) + ".png", false); sp.tex)
                    return sp;
            }
        }
        return load(gfx, std::string(BOX_DIR) + "b_unknown.png", true);
    }

    Sprite speciesSprite(Gfx& gfx, u16 species)
    {
        std::string n = std::to_string(species);
        if (Sprite sp = load(gfx, BOX_DIR + ("b_" + n) + ".png", true); sp.tex)
            return sp;
        if (Sprite sp = load(gfx, BOX_DIR + ("a_" + n) + ".png", false); sp.tex)
            return sp;
        return load(gfx, std::string(BOX_DIR) + "b_unknown.png", true);
    }

    Sprite homeSprite(Gfx& gfx, const pksm::PKX& pk)
    {
        if (!pk.egg() && displayForm(pk) == 0)
        {
            std::string base = HOME_DIR + std::to_string(u16(pk.species()));
            std::string s    = pk.shiny() ? "s" : "";
            if (pk.gender() == pksm::Gender::Female)
            {
                if (Sprite sp = load(gfx, base + "f" + s + ".webp", false); sp.tex)
                    return sp;
            }
            if (Sprite sp = load(gfx, base + s + ".webp", false); sp.tex)
                return sp;
        }
        return boxSprite(gfx, pk);
    }

    void drawSprite(Gfx& gfx, const Sprite& s, int x, int y, int w, int h, float maxScale)
    {
        if (!s.tex || s.w <= 0 || s.h <= 0)
            return;
        float scale = std::min(float(w) / s.w, float(h) / s.h);
        if (maxScale > 0)
            scale = std::min(scale, maxScale);
        int dw = int(s.w * scale + 0.5f), dh = int(s.h * scale + 0.5f);
        gfx.draw(s.tex, x + (w - dw) / 2, y + (h - dh) / 2, dw, dh);
    }

    SDL_Texture* ballSprite(Gfx& gfx, const pksm::PKX& pk)
    {
        return gfx.texture(std::string(BOX_DIR) + "_ball" + std::to_string(u8(pk.ball())) + ".png", true);
    }

}
