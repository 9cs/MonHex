#include "ui/types.hpp"

#include "data/movedata.hpp"
#include "lang.hpp"
#include "theme.hpp"
#include "utils/i18n.hpp"

namespace monhex::ui
{
    namespace
    {
        constexpr Color COLORS[18] = {
            {150, 154, 158}, // Normal
            {232, 128, 34},  // Fighting
            {124, 176, 232}, // Flying
            {148, 82, 204},  // Poison
            {176, 118, 64},  // Ground
            {176, 168, 124}, // Rock
            {146, 164, 40},  // Bug
            {118, 80, 150},  // Ghost
            {96, 158, 182},  // Steel
            {230, 64, 56},   // Fire
            {52, 132, 236},  // Water
            {74, 168, 58},   // Grass
            {244, 196, 32},  // Electric
            {234, 82, 128},  // Psychic
            {82, 200, 232},  // Ice
            {90, 100, 222},  // Dragon
            {96, 80, 82},    // Dark
            {230, 120, 226}, // Fairy
        };
        constexpr const char* NAMES[18] = {"Normal", "Lutador", "Voador", "Venenoso", "Terrestre",
            "Pedra", "Inseto", "Fantasma", "Aço", "Fogo", "Água", "Planta", "Elétrico", "Psíquico",
            "Gelo", "Dragão", "Sombrio", "Fada"};
    }

    const char* natureName(u8 nature)
    {
        // Game order: Hardy, Lonely, Brave, Adamant, Naughty, Bold, Docile, Relaxed,
        // Impish, Lax, Timid, Hasty, Serious, Jolly, Naive, Modest, Mild, Quiet,
        // Bashful, Rash, Calm, Gentle, Sassy, Careful, Quirky
        static const char* NAMES[25] = {"Resistente", "Solitária", "Corajosa", "Firme", "Travessa", "Ousada",
            "Dócil", "Relaxada", "Brincalhona", "Descuidada", "Tímida", "Apressada", "Séria", "Alegre", "Ingênua",
            "Modesta", "Amena", "Quieta", "Acanhada", "Imprudente", "Calma", "Gentil", "Atrevida", "Cuidadosa",
            "Peculiar"};
        if (nature >= 25)
            return "?";
        if (uiLanguage() == UiLanguage::Portuguese)
            return NAMES[nature];
        return i18n::nature(LANG, pksm::Nature{nature}).c_str();
    }

    std::string natureEffect(u8 nature)
    {
        // Raised stat = nature / 5, lowered = nature % 5, in this order
        static const char* STATS[5] = {"Ataque", "Defesa", "Velocidade", "At. Esp.", "Def. Esp."};
        if (nature >= 25 || nature / 5 == nature % 5)
            return tr("neutra");
        return std::string("+") + tr(STATS[nature / 5]) + ", −" + tr(STATS[nature % 5]);
    }

    Color typeColor(u8 typeIndex) { return typeIndex < 18 ? COLORS[typeIndex] : Color{110, 112, 120}; }

    Color typeColor(pksm::Type type) { return typeColor(u8(type)); }

    const char* typeName(u8 typeIndex)
    {
        if (typeIndex >= 18)
            return "???";
        if (uiLanguage() == UiLanguage::Portuguese)
            return NAMES[typeIndex];
        return i18n::type(LANG, pksm::Type{typeIndex}).c_str();
    }

    Color textOn(Color bg)
    {
        float luminance = 0.299f * bg.r + 0.587f * bg.g + 0.114f * bg.b;
        return luminance > 170 ? Color{30, 30, 34} : Color{255, 255, 255};
    }

    MoveDetails moveDetails(pksm::Move move)
    {
        u16 id = u16(move);
        if (id == 0 || id >= data::MOVE_COUNT || data::MOVES[id].type == 255)
            return {false, 255, 0, 0, 0, 0};
        const auto& m = data::MOVES[id];
        return {true, m.type, m.category, m.power, m.pp, m.accuracy};
    }

    SDL_Texture* typeIcon(Gfx& gfx, u8 typeIndex)
    {
        if (typeIndex >= 18)
            return nullptr;
        char path[48];
        snprintf(path, sizeof(path), "romfs:/types/type_icon_%02u.png", typeIndex);
        return gfx.whiteMask(path);
    }

    int typeBadge(Gfx& gfx, u8 typeIndex, int x, int y, int height, int fontSize)
    {
        Color c          = typeColor(typeIndex);
        Color fg         = textOn(c);
        const char* name = typeName(typeIndex);
        SDL_Texture* icon = typeIcon(gfx, typeIndex);
        int iconSize      = icon ? height - 8 : 0;
        int w = gfx.textWidth(name, fontSize) + height * 2 / 3 + (icon ? iconSize + 6 : height / 3);
        gfx.fillRoundRect(x, y, w, height, height / 2, c);
        int tx = x + height / 3;
        if (icon)
        {
            gfx.drawMask(icon, x + height / 4, y + 4, iconSize, iconSize, fg);
            tx = x + height / 4 + iconSize + 6;
        }
        gfx.text(name, tx, y + (height - gfx.textHeight(fontSize)) / 2, fontSize, fg);
        return w;
    }

    int typeBadgeWidth(Gfx& gfx, u8 typeIndex, int height, int fontSize)
    {
        bool icon = typeIcon(gfx, typeIndex) != nullptr;
        return gfx.textWidth(typeName(typeIndex), fontSize) + height * 2 / 3 + (icon ? height - 8 + 6 : height / 3);
    }

    void typeBadges(Gfx& gfx, u8 type1, u8 type2, int x, int y, int height, int fontSize, int maxWidth)
    {
        bool two  = type2 != type1;
        int needs = typeBadgeWidth(gfx, type1, height, fontSize) +
                    (two ? 6 + typeBadgeWidth(gfx, type2, height, fontSize) : 0);
        if (needs <= maxWidth)
        {
            int next = x + typeBadge(gfx, type1, x, y, height, fontSize) + 6;
            if (two)
                typeBadge(gfx, type2, next, y, height, fontSize);
            return;
        }
        if (two)
        {
            // Full name for the first type if both still fit that way, else symbols only
            int first = typeBadgeWidth(gfx, type1, height, fontSize);
            if (first + 6 + height <= maxWidth)
            {
                typeBadge(gfx, type1, x, y, height, fontSize);
                typeCircle(gfx, type2, x + first + 6 + height / 2, y + height / 2, height / 2);
                return;
            }
            typeCircle(gfx, type1, x + height / 2, y + height / 2, height / 2);
            typeCircle(gfx, type2, x + height + 6 + height / 2, y + height / 2, height / 2);
            return;
        }
        typeCircle(gfx, type1, x + height / 2, y + height / 2, height / 2);
    }

    void typeCircle(Gfx& gfx, u8 typeIndex, int cx, int cy, int radius)
    {
        Color c = typeColor(typeIndex);
        gfx.fillCircle(cx, cy, radius, c);
        if (SDL_Texture* icon = typeIcon(gfx, typeIndex))
        {
            int s = radius * 3 / 2;
            gfx.drawMask(icon, cx - s / 2, cy - s / 2, s, s, textOn(c));
        }
    }
}
