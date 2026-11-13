// User settings, kept in sdmc:/switch/monhex/config.ini
#pragma once

#include <string>

namespace monhex
{
    struct Config
    {
        // Backups kept per game and user; older ones are deleted. 0 keeps all.
        int maxBackups = 2;
        // Download cries for the detail view
        bool downloadMedia = true;
        // Language of the menus: "en", "pt", "es" or "fr"
        std::string language = "en";

        static constexpr int MAX_BACKUPS_LIMIT = 20;

        void load(void);
        bool save(void) const;
    };
}
