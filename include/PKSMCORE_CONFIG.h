// Configuration consumed by PKSM-Core (https://github.com/FlagBrew/PKSM-Core).
// The language strings and species data are copied into romfs by the Makefile.
#pragma once

#ifndef _PKSMCORE_GETLINE_FUNC
// devkitA64's newlib only exposes getline as __getline
#define _PKSMCORE_GETLINE_FUNC __getline
#endif

#ifndef _PKSMCORE_LANG_FOLDER
#define _PKSMCORE_LANG_FOLDER "romfs:/i18n/"
#endif

#ifndef _PKSMCORE_PERSONAL_FOLDER
#define _PKSMCORE_PERSONAL_FOLDER "romfs:/personal/"
#endif
