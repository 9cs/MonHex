#include "lang.hpp"

#include <cstdio>
#include <unordered_map>
#include <vector>

#include "theme.hpp"
#include "utils/i18n.hpp"

namespace monhex
{
    namespace
    {
        UiLanguage current = UiLanguage::English;
        // Portuguese text -> text in each other language
        std::unordered_map<std::string, std::string> table[size_t(UiLanguage::Count)];
        bool loaded = false;

        std::string unescape(const std::string& s)
        {
            std::string out;
            for (size_t i = 0; i < s.size(); i++)
            {
                if (s[i] == '\\' && i + 1 < s.size())
                {
                    char n = s[++i];
                    out += n == 'n' ? '\n' : (n == 't' ? '\t' : n);
                }
                else
                {
                    out += s[i];
                }
            }
            return out;
        }

        // Columns: pt, en, es, fr (tab separated, one string per line)
        void load(void)
        {
            loaded  = true;
            FILE* f = fopen("romfs:/lang/strings.tsv", "r");
            if (!f)
                return;
            std::string line;
            int c;
            auto handle = [&](const std::string& l) {
                if (l.empty() || l[0] == '#')
                    return;
                std::vector<std::string> cols;
                size_t start = 0;
                while (true)
                {
                    size_t tab = l.find('\t', start);
                    cols.push_back(unescape(l.substr(start, tab == std::string::npos ? std::string::npos : tab - start)));
                    if (tab == std::string::npos)
                        break;
                    start = tab + 1;
                }
                if (cols.size() < 4)
                    return;
                table[size_t(UiLanguage::English)][cols[0]] = cols[1];
                table[size_t(UiLanguage::Spanish)][cols[0]] = cols[2];
                table[size_t(UiLanguage::French)][cols[0]]  = cols[3];
            };
            while ((c = fgetc(f)) != EOF)
            {
                if (c == '\n')
                {
                    if (!line.empty() && line.back() == '\r')
                        line.pop_back();
                    handle(line);
                    line.clear();
                }
                else
                {
                    line += char(c);
                }
            }
            handle(line);
            fclose(f);
        }
    }

    void setUiLanguage(UiLanguage lang)
    {
        // Loaded here, on the main thread, so lookups from worker threads only read
        if (!loaded)
            load();
        current = lang;
        pksm::Language data = lang == UiLanguage::Spanish  ? pksm::Language::SPA
                              : lang == UiLanguage::French ? pksm::Language::FRE
                                                           : pksm::Language::ENG;
        if (data != LANG)
        {
            LANG = data;
            i18n::init(LANG);
        }
    }

    UiLanguage uiLanguage(void) { return current; }

    const char* uiLanguageName(UiLanguage lang)
    {
        switch (lang)
        {
            case UiLanguage::English: return "English";
            case UiLanguage::Portuguese: return "Português (Brasil)";
            case UiLanguage::Spanish: return "Español";
            case UiLanguage::French: return "Français";
            default: return "?";
        }
    }

    const char* uiLanguageCode(UiLanguage lang)
    {
        static const char* CODES[] = {"en", "pt", "es", "fr"};
        return lang < UiLanguage::Count ? CODES[size_t(lang)] : "en";
    }

    UiLanguage uiLanguageFromCode(const std::string& code)
    {
        for (u8 i = 0; i < u8(UiLanguage::Count); i++)
        {
            if (code == uiLanguageCode(UiLanguage(i)))
                return UiLanguage(i);
        }
        return UiLanguage::English;
    }

    const char* tr(const char* portuguese)
    {
        if (current == UiLanguage::Portuguese || !portuguese)
            return portuguese;
        if (!loaded)
            load();
        const auto& t = table[size_t(current)];
        auto it       = t.find(portuguese);
        return it == t.end() || it->second.empty() ? portuguese : it->second.c_str();
    }
}
