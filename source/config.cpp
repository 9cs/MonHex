#include "config.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

namespace monhex
{
    namespace
    {
        constexpr const char* DIR  = "sdmc:/switch/monhex";
        constexpr const char* PATH = "sdmc:/switch/monhex/config.ini";
    }

    void Config::load(void)
    {
        FILE* f = fopen(PATH, "r");
        if (!f)
            return;
        char line[128];
        while (fgets(line, sizeof(line), f))
        {
            int value;
            if (sscanf(line, "max_backups=%d", &value) == 1 && value >= 0 &&
                value <= MAX_BACKUPS_LIMIT)
            {
                maxBackups = value;
            }
            char code[8];
            if (sscanf(line, "language=%7s", code) == 1)
            {
                language = code;
            }
            if (sscanf(line, "download_media=%d", &value) == 1)
            {
                downloadMedia = value != 0;
            }
        }
        fclose(f);
    }

    bool Config::save(void) const
    {
        mkdir("sdmc:/switch", 0777);
        mkdir(DIR, 0777);
        FILE* f = fopen(PATH, "w");
        if (!f)
            return false;
        fprintf(f, "# Configuracoes do MonHex\n");
        fprintf(f, "# Backups mantidos por jogo e usuario (0 = sem limite)\n");
        fprintf(f, "max_backups=%d\n", maxBackups);
        fprintf(f, "# Baixar os cries dos Pokemon (1 = sim, 0 = nao)\n");
        fprintf(f, "download_media=%d\n", downloadMedia ? 1 : 0);
        fprintf(f, "# Idioma: en, pt, es, fr\n");
        fprintf(f, "language=%s\n", language.c_str());
        fclose(f);
        return true;
    }
}
