#include <windows.h>

#include <SDL3/SDL_main.h>

#include <cstring>
#include <string>

#include "app/App.h"
#include "core/System.h"

int main(int argc, char** argv) {
    bool watchdog = false, restart = false, single = true;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--watchdog") == 0) watchdog = true;
        if (std::strcmp(argv[i], "--restart") == 0) restart = true;
        if (std::strcmp(argv[i], "--no-single-instance") == 0) single = false;  // tests (endurance) seulement
    }
    // Surveillance : processus léger, sans fenêtre, qui relance le frontend après un plantage.
    if (watchdog) return sys::RunWatchdog();

    // Une seule instance (borne en mode kiosque : évite un double lancement au démarrage de Windows).
    // Après un redémarrage demandé, on attend que l'instance précédente ait fini de se fermer.
    HANDLE mutex = nullptr;
    for (int tries = 0; single; ++tries) {
        mutex = CreateMutexW(nullptr, TRUE, L"Local\\RacecabFrontend.SingleInstance");
        if (!(mutex && GetLastError() == ERROR_ALREADY_EXISTS)) break;
        CloseHandle(mutex);
        mutex = nullptr;
        if (!restart || tries >= 100) return 0;
        Sleep(100);
    }

    int code;
    bool wantRestart = false;
    {
        App app;
        code = app.Run(argc, argv);
        wantRestart = app.RestartRequested();
    }
    if (mutex) CloseHandle(mutex);
    if (wantRestart) {
        if (sys::UnderWatchdog()) return sys::kRestartExitCode;  // la surveillance relance aussitôt
        std::string err;
        sys::SpawnSelf(L"--restart", err);
    }
    return code;
}
