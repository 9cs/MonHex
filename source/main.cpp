// MonHex - Pokemon save editor for Nintendo Switch, built on PKSM-Core.
// Milestone 2: graphical interface, still read-only.
#include <cstdio>
#include <string>

#include <netinet/in.h>
#include <switch.h>

#include "app.hpp"

int main(int argc, char* argv[])
{
    // Sockets for downloads; when sent with `nxlink -s`, printf output also
    // shows up on the PC
    bool sockets = R_SUCCEEDED(socketInitializeDefault());
    if (sockets && __nxlink_host.s_addr != 0)
    {
        nxlinkStdio();
    }

    monhex::App app;
    std::string error;
    if (app.init(error))
    {
        while (app.frame())
        {
        }
    }
    else
    {
        // The screen couldn't start, so fall back to the text console to say why
        consoleInit(NULL);
        printf("MonHex nao conseguiu iniciar:\n\n%s\n\nAperte + para sair.\n", error.c_str());
        PadState pad;
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        padInitializeDefault(&pad);
        while (appletMainLoop())
        {
            padUpdate(&pad);
            if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
                break;
            consoleUpdate(NULL);
        }
        consoleExit(NULL);
    }
    app.exit();
    if (sockets)
    {
        socketExit();
    }
    return 0;
}
