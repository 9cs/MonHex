#include "savedata.hpp"

#include "lang.hpp"

#include "utils/crypto.hpp"

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <sys/stat.h>

namespace monhex
{
    namespace
    {
        constexpr const char* MOUNT_NAME  = "nxsave";
        constexpr const char* MOUNT_ROOT  = "nxsave:/";
        constexpr const char* BACKUP_ROOT = "sdmc:/switch/monhex/backups";

        // Games PKSM-Core can edit. Other games whose name contains "Pok" are still
        // listed, and loading them is attempted, since newer releases may also work.
        constexpr u64 SUPPORTED_IDS[] = {
            0x0100ABF008968000, // Pokemon Sword
            0x01008DB008C2C000, // Pokemon Shield
            0x010003F003A34000, // Pokemon: Let's Go, Pikachu!
            0x0100187003A36000, // Pokemon: Let's Go, Eevee!
            0x01001F5010DFA000, // Pokemon Legends: Arceus
            0x0100A3D008C5C000, // Pokemon Scarlet
            0x01008F6008C5E000, // Pokemon Violet
        };

        // The biggest save PKSM-Core handles is a few MB; skip anything far larger
        constexpr size_t MAX_SAVE_FILE = 32 * 1024 * 1024;

        // Appends a line to sdmc:/switch/monhex/log.txt
        void logLine(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
        void logLine(const char* fmt, ...)
        {
            FILE* f = fopen("sdmc:/switch/monhex/log.txt", "a");
            if (!f)
                return;
            time_t now = time(nullptr);
            char stamp[32];
            strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
            fprintf(f, "[%s] ", stamp);
            va_list args;
            va_start(args, fmt);
            vfprintf(f, fmt, args);
            va_end(args);
            fputc('\n', f);
            fclose(f);
        }

        constexpr size_t GBA_SAVE = 0x20000;
        constexpr u32 GBA_SIGNATURE = 0x08012025; // at 0xFF8 of every Gen 3 save sector

        u32 readU32(const u8* p) { return u32(p[0]) | u32(p[1]) << 8 | u32(p[2]) << 16 | u32(p[3]) << 24; }

        // Looks for a 128 KB Gen 3 save inside a larger file, as the GBA releases on
        // Switch and emulators may wrap it. Returns the save and where it starts.
        std::unique_ptr<pksm::Sav> findEmbeddedGba(const std::shared_ptr<u8[]>& file, size_t size, size_t& offset)
        {
            if (size <= GBA_SAVE)
                return nullptr;
            for (size_t pos = 0; pos + GBA_SAVE <= size; pos++)
            {
                // Two sectors in a row carrying the signature is a strong hint
                if (readU32(&file[pos + 0xFF8]) != GBA_SIGNATURE || readU32(&file[pos + 0x1FF8]) != GBA_SIGNATURE)
                    continue;
                auto copy = std::shared_ptr<u8[]>(new u8[GBA_SAVE]);
                memcpy(copy.get(), &file[pos], GBA_SAVE);
                std::unique_ptr<pksm::Sav> sav;
                try
                {
                    sav = pksm::Sav::getSave(copy, GBA_SAVE);
                }
                catch (...)
                {
                    sav = nullptr;
                }
                if (sav)
                {
                    offset = pos;
                    return sav;
                }
            }
            return nullptr;
        }

        // Switch Online style header: "SRAM", ROM hash, build string, optional
        // clock, then the SHA-1 of the save as 40 lowercase hex characters
        bool sramHashOffset(const u8* file, size_t size, size_t& hashAt, size_t& dataAt)
        {
            if (size < 0x40 || memcmp(file, "SRAM", 4) != 0)
                return false;
            size_t ofs = 0x34 + file[0x30];
            if (ofs >= size)
                return false;
            bool rtc = file[ofs++] != 0;
            if (rtc)
                ofs += 0x20;
            hashAt = ofs;
            dataAt = ofs + 40;
            return dataAt <= size;
        }

        void writeSramHash(u8* file, size_t hashAt, const u8* data, size_t length)
        {
            auto hash = pksm::crypto::sha1({data, length});
            static const char* HEX = "0123456789abcdef";
            for (int i = 0; i < 20; i++)
            {
                file[hashAt + i * 2]     = HEX[hash[i] >> 4];
                file[hashAt + i * 2 + 1] = HEX[hash[i] & 0xF];
            }
        }

        bool isSupportedId(u64 id)
        {
            return std::find(std::begin(SUPPORTED_IDS), std::end(SUPPORTED_IDS), id) !=
                   std::end(SUPPORTED_IDS);
        }

        bool looksLikePokemon(const std::string& name)
        {
            std::string lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                [](unsigned char c) { return std::tolower(c); });
            return lower.find("pok") != std::string::npos;
        }

        // Reads the game's name (in the console's language) and its icon
        void applicationInfo(u64 applicationId, std::string& name, std::vector<u8>& icon)
        {
            auto control = std::make_unique<NsApplicationControlData>();
            u64 actualSize = 0;
            if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage,
                    applicationId, control.get(), sizeof(NsApplicationControlData),
                    &actualSize)))
            {
                NacpLanguageEntry* entry = nullptr;
                if (R_SUCCEEDED(nacpGetLanguageEntry(&control->nacp, &entry)) && entry)
                {
                    name = entry->name;
                }
                if (actualSize > sizeof(NacpStruct))
                {
                    size_t iconSize = actualSize - sizeof(NacpStruct);
                    icon.assign(control->icon, control->icon + iconSize);
                }
            }
            if (name.empty())
            {
                char buf[32];
                snprintf(buf, sizeof(buf), "%016lX", applicationId);
                name = buf;
            }
        }

        bool makeDirs(const std::string& path)
        {
            for (size_t pos = path.find('/', strlen("sdmc:/")); pos != std::string::npos;
                 pos = path.find('/', pos + 1))
            {
                mkdir(path.substr(0, pos).c_str(), 0777);
            }
            mkdir(path.c_str(), 0777);
            struct stat st;
            return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
        }

        bool readFile(const std::string& path, std::shared_ptr<u8[]>& data, size_t& size)
        {
            FILE* f = fopen(path.c_str(), "rb");
            if (!f)
            {
                return false;
            }
            fseek(f, 0, SEEK_END);
            long len = ftell(f);
            rewind(f);
            if (len <= 0 || static_cast<size_t>(len) > MAX_SAVE_FILE)
            {
                fclose(f);
                return false;
            }
            size = static_cast<size_t>(len);
            data = std::shared_ptr<u8[]>(new u8[size]);
            bool ok = fread(data.get(), 1, size, f) == size;
            fclose(f);
            return ok;
        }

        bool copyFile(const std::string& from, const std::string& to)
        {
            FILE* in = fopen(from.c_str(), "rb");
            if (!in)
            {
                return false;
            }
            FILE* out = fopen(to.c_str(), "wb");
            if (!out)
            {
                fclose(in);
                return false;
            }
            static u8 buf[0x40000];
            bool ok = true;
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
            {
                if (fwrite(buf, 1, n, out) != n)
                {
                    ok = false;
                    break;
                }
            }
            fclose(in);
            fclose(out);
            return ok;
        }

        // Collects every regular file under dir, as paths relative to the mount root
        void listFiles(const std::string& dir, const std::string& rel,
            std::vector<std::string>& out)
        {
            DIR* d = opendir(dir.c_str());
            if (!d)
            {
                return;
            }
            while (dirent* e = readdir(d))
            {
                std::string name = e->d_name;
                if (name == "." || name == "..")
                {
                    continue;
                }
                std::string full = dir + name;
                std::string relPath = rel + name;
                if (e->d_type == DT_DIR)
                {
                    listFiles(full + "/", relPath + "/", out);
                }
                else
                {
                    out.push_back(relPath);
                }
            }
            closedir(d);
        }

        // Copies all save files to sdmc:/switch/monhex/backups/<title id>/<date-time>/
        // Folder name for a user: their nickname in plain letters plus part of the
        // account id, so two users with the same nickname don't share backups
        std::string userFolder(AccountUid uid, const std::string& nickname)
        {
            std::string name;
            for (unsigned char c : nickname)
            {
                if (std::isalnum(c) || c == '-' || c == '_')
                    name += static_cast<char>(c);
            }
            if (name.empty())
                name = "usuario";
            char id[12];
            snprintf(id, sizeof(id), "_%08X", static_cast<u32>(uid.uid[0]));
            return name + id;
        }

        void removeTree(const std::string& path)
        {
            if (DIR* d = opendir(path.c_str()))
            {
                while (dirent* e = readdir(d))
                {
                    std::string name = e->d_name;
                    if (name == "." || name == "..")
                        continue;
                    std::string full = path + "/" + name;
                    if (e->d_type == DT_DIR)
                        removeTree(full);
                    else
                        remove(full.c_str());
                }
                closedir(d);
            }
            rmdir(path.c_str());
        }

        // Deletes the oldest backups in a folder until at most `keep` remain.
        // Backup folders are named by date and time, so name order is age order.
        void pruneBackups(const std::string& folder, int keep)
        {
            if (keep <= 0)
                return;
            std::vector<std::string> backups;
            if (DIR* d = opendir(folder.c_str()))
            {
                while (dirent* e = readdir(d))
                {
                    std::string name = e->d_name;
                    if (e->d_type == DT_DIR && name.size() == 15 && name[8] == '-')
                        backups.push_back(name);
                }
                closedir(d);
            }
            std::sort(backups.begin(), backups.end());
            for (size_t i = 0; i + keep < backups.size(); i++)
            {
                removeTree(folder + "/" + backups[i]);
            }
        }

        bool backupFiles(const std::string& folder, const std::vector<std::string>& files,
            std::string& backupPath)
        {
            time_t now = time(nullptr);
            struct tm* t = localtime(&now);
            char stamp[32];
            strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", t);

            backupPath = folder + "/" + stamp;
            if (!makeDirs(backupPath))
            {
                return false;
            }
            for (const auto& rel : files)
            {
                std::string dest = backupPath + "/" + rel;
                size_t slash     = dest.rfind('/');
                makeDirs(dest.substr(0, slash));
                if (!copyFile(MOUNT_ROOT + rel, dest))
                {
                    return false;
                }
            }
            return true;
        }
    }

    std::vector<GameEntry> listPokemonGames(AccountUid uid)
    {
        std::vector<GameEntry> games;
        FsSaveDataInfoReader reader;
        if (R_FAILED(fsOpenSaveDataInfoReader(&reader, FsSaveDataSpaceId_User)))
        {
            return games;
        }

        FsSaveDataInfo info;
        s64 count = 0;
        while (R_SUCCEEDED(fsSaveDataInfoReaderRead(&reader, &info, 1, &count)) && count > 0)
        {
            if (info.save_data_type != FsSaveDataType_Account ||
                memcmp(&info.uid, &uid, sizeof(AccountUid)) != 0)
            {
                continue;
            }
            bool known = isSupportedId(info.application_id);
            std::string name;
            std::vector<u8> icon;
            applicationInfo(info.application_id, name, icon);
            if (known || looksLikePokemon(name))
            {
                games.push_back({info.application_id, name, known, std::move(icon)});
            }
        }
        fsSaveDataInfoReaderClose(&reader);

        std::sort(games.begin(), games.end(),
            [](const GameEntry& a, const GameEntry& b) { return a.name < b.name; });
        return games;
    }

    LoadedSave loadGameSave(const GameEntry& game, AccountUid uid, const std::string& userName,
        int maxBackups, std::string& error)
    {
        LoadedSave result;
        char idHex[20];
        snprintf(idHex, sizeof(idHex), "%016lX", game.applicationId);
        std::string backupFolder =
            std::string(BACKUP_ROOT) + "/" + idHex + "/" + userFolder(uid, userName);

        FsFileSystem fs;
        if (R_FAILED(fsOpen_SaveData(&fs, game.applicationId, uid)))
        {
            error = tr("Não foi possível abrir o save deste jogo.");
            return result;
        }
        if (fsdevMountDevice(MOUNT_NAME, fs) == -1)
        {
            fsFsClose(&fs);
            error = tr("Não foi possível montar o save deste jogo.");
            return result;
        }

        std::vector<std::string> files;
        listFiles(MOUNT_ROOT, "", files);

        if (files.empty())
        {
            error = tr("O save deste jogo está vazio.");
        }
        else if (!backupFiles(backupFolder, files, result.backupPath))
        {
            error = tr("Falha ao criar o backup no cartão SD. Nada foi carregado.");
        }
        else
        {
            pruneBackups(backupFolder, maxBackups);
            logLine("load %016lX: %zu file(s)", game.applicationId, files.size());
            // Some games keep more than one copy of the save; the one called
            // "main" is what they load, and the others are written alongside it
            std::vector<std::pair<std::string, size_t>> recognized;
            for (const auto& rel : files)
            {
                std::shared_ptr<u8[]> data;
                size_t size = 0;
                if (!readFile(MOUNT_ROOT + rel, data, size))
                {
                    logLine("  %s: not read", rel.c_str());
                    continue;
                }
                std::unique_ptr<pksm::Sav> sav;
                try
                {
                    sav = pksm::Sav::getSave(data, size);
                }
                catch (...)
                {
                    sav = nullptr;
                }
                size_t offset = 0;
                bool embedded = false;
                if (!sav)
                {
                    sav      = findEmbeddedGba(data, size, offset);
                    embedded = sav != nullptr;
                }
                logLine("  %s: %zu bytes, %s", rel.c_str(), size,
                    !sav ? "not a save" : (embedded ? "GBA save inside" : "recognized"));
                if (embedded)
                    logLine("    at offset 0x%zX%s", offset, memcmp(data.get(), "SRAM", 4) == 0 ? " (SRAM header)" : "");
                if (!sav)
                    continue;
                if (!embedded)
                    recognized.emplace_back(rel, size);
                bool isMain = rel == "main" || (rel.size() > 5 && rel.compare(rel.size() - 5, 5, "/main") == 0);
                if (!result.sav || isMain)
                {
                    result.sav        = std::move(sav);
                    result.fileName   = rel;
                    result.embedded   = embedded;
                    result.offset     = offset;
                    result.sramHeader = embedded && memcmp(data.get(), "SRAM", 4) == 0;
                }
            }
            for (const auto& [rel, size] : recognized)
            {
                if (rel != result.fileName && result.sav && size == result.sav->getEntireLengthIncludingFooter())
                    result.mirrors.push_back(rel);
            }
            if (result.sav)
                logLine("  using %s (%zu other copies)", result.fileName.c_str(), result.mirrors.size());
            if (!result.sav)
            {
                error = tr("Nenhum arquivo deste save foi reconhecido pela PKSM-Core.");
            }
        }

        fsdevUnmountDevice(MOUNT_NAME);
        return result;
    }
}

namespace monhex
{
    GameRunState gameRunState(u64 applicationId)
    {
        // A game left suspended keeps its save open and would overwrite our changes.
        // When this app was opened through a game (holding R), that "game" is us.
        u64 pid = 0, programId = 0;
        bool known = false;
        if (R_SUCCEEDED(pmdmntInitialize()))
        {
            if (R_SUCCEEDED(pmdmntGetApplicationProcessId(&pid)))
            {
                u64 self = 0;
                svcGetProcessId(&self, CUR_PROCESS_HANDLE);
                if (pid == self)
                {
                    pmdmntExit();
                    return GameRunState::NotRunning;
                }
                if (R_SUCCEEDED(pminfoInitialize()))
                {
                    known = R_SUCCEEDED(pminfoGetProgramId(&programId, pid));
                    pminfoExit();
                }
                pmdmntExit();
                if (!known)
                    return GameRunState::Unknown;
                return programId == applicationId ? GameRunState::Running : GameRunState::NotRunning;
            }
            pmdmntExit();
            return GameRunState::NotRunning; // no game open at all
        }
        return GameRunState::Unknown;
    }

    bool writeGameSave(const GameEntry& game, AccountUid uid, const std::string& userName, int maxBackups,
        LoadedSave& save, std::string& error)
    {
        if (!save.sav || save.fileName.empty())
        {
            error = tr("Nenhum save aberto.");
            return false;
        }
        if (gameRunState(game.applicationId) == GameRunState::Running)
        {
            error = tr("O jogo está aberto em segundo plano.\nFeche o jogo (Home > X) e tente de novo.");
            return false;
        }

        char idHex[20];
        snprintf(idHex, sizeof(idHex), "%016lX", game.applicationId);
        std::string backupFolder = std::string(BACKUP_ROOT) + "/" + idHex + "/" + userFolder(uid, userName);

        FsFileSystem fs;
        if (R_FAILED(fsOpen_SaveData(&fs, game.applicationId, uid)))
        {
            error = tr("Não foi possível abrir o save deste jogo para gravar.");
            return false;
        }
        if (fsdevMountDevice(MOUNT_NAME, fs) == -1)
        {
            fsFsClose(&fs);
            error = tr("Não foi possível montar o save deste jogo.");
            return false;
        }

        bool ok = false;
        std::vector<std::string> files;
        listFiles(MOUNT_ROOT, "", files);
        std::string backupPath;
        // What is on the console right now is copied first, so it can always come back
        if (!backupFiles(backupFolder, files, backupPath))
        {
            error = tr("Falha ao criar o backup no cartão SD. Nada foi gravado.");
        }
        else
        {
            pruneBackups(backupFolder, maxBackups);
            save.backupPath = backupPath;

            // Checksums, encryption and signature are redone by PKSM-Core
            save.sav->finishEditing();
            std::shared_ptr<u8[]> data = save.sav->rawData();
            u32 length                 = save.sav->getEntireLengthIncludingFooter();
            if (save.embedded)
            {
                // Rebuild the whole file: the wrapper as it is, the save in place
                std::shared_ptr<u8[]> file;
                size_t fileSize = 0;
                if (readFile(MOUNT_ROOT + save.fileName, file, fileSize) && save.offset + length <= fileSize)
                {
                    memcpy(&file[save.offset], data.get(), length);
                    size_t hashAt, dataAt;
                    if (save.sramHeader && sramHashOffset(file.get(), fileSize, hashAt, dataAt))
                    {
                        writeSramHash(file.get(), hashAt, &file[dataAt], fileSize - dataAt);
                        logLine("  refreshed SRAM header hash");
                    }
                    data   = file;
                    length = u32(fileSize);
                }
            }
            std::vector<std::string> targets{save.fileName};
            targets.insert(targets.end(), save.mirrors.begin(), save.mirrors.end());
            bool written = true;
            for (const auto& rel : targets)
            {
                std::string path = MOUNT_ROOT + rel;
                FILE* f          = fopen(path.c_str(), "r+b");
                size_t n         = 0;
                if (f)
                {
                    n = fwrite(data.get(), 1, length, f);
                    if (fflush(f) != 0)
                        n = 0;
                    fclose(f);
                }
                logLine("save %016lX: wrote %zu/%u bytes to %s", game.applicationId, n, length, rel.c_str());
                if (n != length)
                {
                    written = false;
                    break;
                }
            }
            Result commit = written ? fsdevCommitDevice(MOUNT_NAME) : 0;
            logLine("  commit: 0x%X", commit);
            if (!written)
                error = tr("Falha ao escrever o save. O backup está em:\n") + backupPath;
            else if (R_FAILED(commit))
                error = tr("O console recusou a gravação. O backup está em:\n") + backupPath;
            else
            {
                // Read the file back to be sure the console kept what was written
                std::shared_ptr<u8[]> check;
                size_t checkSize = 0;
                bool same = readFile(MOUNT_ROOT + save.fileName, check, checkSize) && checkSize == length &&
                            memcmp(check.get(), data.get(), length) == 0;
                logLine("  read back: %s", same ? "identical" : "DIFFERENT");
                if (same)
                    ok = true;
                else
                    error = tr("O save gravado não confere ao ler de volta. O backup está em:\n") + backupPath;
            }
            save.sav->beginEditing();
        }
        fsdevUnmountDevice(MOUNT_NAME);
        return ok;
    }
}

namespace monhex
{
    std::vector<std::string> listBackups(const GameEntry& game, AccountUid uid, const std::string& userName)
    {
        char idHex[20];
        snprintf(idHex, sizeof(idHex), "%016lX", game.applicationId);
        std::string folder = std::string(BACKUP_ROOT) + "/" + idHex + "/" + userFolder(uid, userName);
        std::vector<std::string> stamps;
        if (DIR* d = opendir(folder.c_str()))
        {
            while (dirent* e = readdir(d))
            {
                std::string name = e->d_name;
                if (e->d_type == DT_DIR && name.size() == 15 && name[8] == '-')
                    stamps.push_back(name);
            }
            closedir(d);
        }
        std::sort(stamps.rbegin(), stamps.rend()); // newest first
        return stamps;
    }

    bool restoreBackup(const GameEntry& game, AccountUid uid, const std::string& userName, const std::string& stamp,
        std::string& error)
    {
        if (gameRunState(game.applicationId) == GameRunState::Running)
        {
            error = tr("O jogo está aberto em segundo plano.\nFeche o jogo (Home > X) e tente de novo.");
            return false;
        }
        char idHex[20];
        snprintf(idHex, sizeof(idHex), "%016lX", game.applicationId);
        std::string folder = std::string(BACKUP_ROOT) + "/" + idHex + "/" + userFolder(uid, userName);
        std::string source = folder + "/" + stamp;

        std::vector<std::string> files;
        listFiles(source + "/", "", files);
        if (files.empty())
        {
            error = tr("Esse backup está vazio.");
            return false;
        }

        FsFileSystem fs;
        if (R_FAILED(fsOpen_SaveData(&fs, game.applicationId, uid)))
        {
            error = tr("Não foi possível abrir o save deste jogo.");
            return false;
        }
        if (fsdevMountDevice(MOUNT_NAME, fs) == -1)
        {
            fsFsClose(&fs);
            error = tr("Não foi possível montar o save deste jogo.");
            return false;
        }

        // Keep what is there now as one more backup (not pruned here, so the
        // backup being restored can't be deleted on the way)
        std::vector<std::string> current;
        listFiles(MOUNT_ROOT, "", current);
        std::string safety;
        bool ok = current.empty() || backupFiles(folder, current, safety);
        if (!ok)
        {
            error = tr("Falha ao copiar o save atual antes de restaurar. Nada foi alterado.");
        }
        else
        {
            for (const auto& rel : files)
            {
                std::string dest = MOUNT_ROOT + rel;
                size_t slash     = rel.rfind('/');
                if (slash != std::string::npos)
                    mkdir((MOUNT_ROOT + rel.substr(0, slash)).c_str(), 0777);
                if (!copyFile(source + "/" + rel, dest))
                {
                    ok    = false;
                    error = tr("Falha ao copiar ") + rel + tr(" para o console.");
                    break;
                }
            }
            if (ok && R_FAILED(fsdevCommitDevice(MOUNT_NAME)))
            {
                ok    = false;
                error = tr("O console recusou a gravação.");
            }
        }
        fsdevUnmountDevice(MOUNT_NAME);
        return ok;
    }
}
