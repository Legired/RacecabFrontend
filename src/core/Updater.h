#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include "core/Util.h"

// Mises à jour du frontend depuis les « releases » GitHub (dépôt public, API sans compte) :
//   1. GET https://api.github.com/repos/<dépôt>/releases/latest -> version (tag « v0.6.1 »), notes, paquet .zip ;
//   2. téléchargement du paquet dans <données>\updates, contrôle SHA-256 si la release fournit un « <zip>.sha256 » ;
//   3. extraction, puis script de mise à jour lancé en arrière-plan : il attend la fermeture du frontend,
//      exécute install.ps1 (même dossier, mêmes réglages : type de borne, démarrage, kiosque) et relance le frontend.
// Une copie de développement (sans datadir.txt à côté du programme) peut vérifier, mais jamais s'installer.
struct ReleaseInfo {
    std::string tag, version, name, notes, published;
    std::string assetUrl, assetName, shaUrl;
    uint64_t assetSize = 0;
};

class Updater {
public:
    enum class State { Idle, Checking, UpToDate, Available, Downloading, Ready, Error };

    ~Updater();
    // repo : « propriétaire/dépôt ». current : version du programme (« 0.6.0 »).
    void Check(const std::string& repo, const std::string& current);
    // Télécharge et prépare la release trouvée par Check() (état Available).
    void Download(const fs::path& dataDir);
    // Lance le script de mise à jour (état Ready). Le frontend doit ensuite se fermer (code 0).
    bool LaunchInstall(const fs::path& installDir, const fs::path& dataDir, bool autoStart, bool kiosk, std::string& err);

    State GetState();
    std::string Message();       // état lisible (erreur, progression…)
    ReleaseInfo Release();       // dernière release trouvée
    float Progress();            // 0..1 pendant le téléchargement
    bool Busy() { State s = GetState(); return s == State::Checking || s == State::Downloading; }

    static int CompareVersions(const std::string& a, const std::string& b);  // <0, 0, >0
    static bool CanInstall(const fs::path& exeDir);  // copie installée (datadir.txt présent)

private:
    void Set(State s, const std::string& msg);
    void Join();

    std::mutex m_;
    State state_ = State::Idle;
    std::string message_;
    ReleaseInfo rel_;
    fs::path extracted_;
    std::atomic<float> progress_{0};
    std::atomic<bool> cancel_{false};
    std::thread worker_;
};
