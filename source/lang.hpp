// The language of the app's own text (menus, messages). Text in the code is
// written in Portuguese and looked up in romfs:/lang/strings.tsv for the others.
#pragma once

#include <string>

#include <switch.h>

namespace monhex
{
    enum class UiLanguage : u8
    {
        English,
        Portuguese,
        Spanish,
        French,
        Count
    };

    // Also switches the language of Pokemon data (species, moves, items...)
    // where PKSM-Core has it; Portuguese uses English names there.
    void setUiLanguage(UiLanguage lang);
    UiLanguage uiLanguage(void);
    const char* uiLanguageName(UiLanguage lang);
    const char* uiLanguageCode(UiLanguage lang); // "en", "pt", "es", "fr"
    UiLanguage uiLanguageFromCode(const std::string& code);

    // The text in the current language (the Portuguese text itself is the key)
    const char* tr(const char* portuguese);
}
