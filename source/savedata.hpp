// Finding the user's Pokemon games, mounting their save data, backing it up and
// loading it with PKSM-Core.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include <switch.h>

#include "sav/Sav.hpp"

namespace monhex
{
    struct GameEntry
    {
        u64 applicationId;
        std::string name;
        bool knownSupported;  // PKSM-Core has a save class for this game
        std::vector<u8> icon; // JPEG from the game's control data, may be empty
    };

    struct LoadedSave
    {
        std::unique_ptr<pksm::Sav> sav;
        std::string fileName;   // file inside the save filesystem that held the save
        std::vector<std::string> mirrors; // other copies of the same save, written alongside
        std::string backupPath; // folder on the SD card holding the backup
        // A GBA save found inside a bigger file (an emulator's or Switch Online's
        // wrapper): where it starts, and whether the wrapper is the "SRAM" kind
        // that carries a hash of the save to refresh on writing
        bool embedded  = false;
        size_t offset  = 0;
        bool sramHeader = false;
    };

    // Lists the Pokemon games that have save data for this user on the console
    std::vector<GameEntry> listPokemonGames(AccountUid uid);

    // Mounts the game's save data, backs up every file in it to
    // sdmc:/switch/monhex/backups/<title id>/<user>/<date-time>/ (keeping at most
    // maxBackups there, 0 = all), then looks for a file PKSM-Core recognizes.
    // On failure, returns an empty sav and sets error to a message for the user.
    // The save data is unmounted afterwards.
    LoadedSave loadGameSave(const GameEntry& game, AccountUid uid, const std::string& userName,
        int maxBackups, std::string& error);

    enum class GameRunState
    {
        NotRunning,
        Running, // this very game is open (suspended) and holds its save
        Unknown  // the system wouldn't say
    };
    GameRunState gameRunState(u64 applicationId);

    // Writes the edited save back to the console: backs up what is there first
    // (same folder and limit as loadGameSave), then replaces the save file and
    // commits. Refuses while the game itself is open. On failure, sets error.
    bool writeGameSave(const GameEntry& game, AccountUid uid, const std::string& userName, int maxBackups,
        LoadedSave& save, std::string& error);

    // Backup folder names (date-time) for this game and user, newest first
    std::vector<std::string> listBackups(const GameEntry& game, AccountUid uid, const std::string& userName);
    // Copies a backup back onto the console, after keeping the current save as
    // one more backup. Refuses while the game itself is open.
    bool restoreBackup(const GameEntry& game, AccountUid uid, const std::string& userName, const std::string& stamp,
        std::string& error);
}
