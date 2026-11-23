#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "library/Model.h"

struct LaunchResult {
    int64_t gameId = 0;
    bool ok = false;
    std::string error;    // message lisible si ok == false
    int64_t seconds = 0;  // durée de jeu mesurée
    uint32_t exitCode = 0;
};

// Lancement et supervision d'un jeu (.bat / .exe) dans un thread dédié.
//
// Stratégie (voir docs/Lancement.md) :
//  - le processus est créé suspendu, attaché à un Job Object, puis relancé : tous ses descendants
//    (exe lancé par le .bat, loaders...) appartiennent automatiquement au Job ;
//  - la fin réelle du jeu = notification JOB_OBJECT_MSG_ACTIVE_PROCESS_ZERO (plus aucun processus) ;
//  - option « processus à surveiller » pour les lanceurs qui sortent du Job ;
//  - le frontend ne tue JAMAIS de processus : le Job est créé sans KILL_ON_JOB_CLOSE.
class Launcher {
public:
    enum class Phase { Idle, PreCommand, Delay, Starting, Running, Watching, PostCommand, Done };

    ~Launcher();
    bool Busy() const { return phase_ != Phase::Idle; }
    Phase CurrentPhase() const { return phase_; }
    void Start(const Game& g);
    std::optional<LaunchResult> Poll();  // résultat disponible une fois le jeu terminé
    // Processus du jeu (principal + descendants du Job) et processus à surveiller : servent à
    // repérer la vraie fenêtre du jeu pendant l'écran de chargement.
    std::vector<uint32_t> Pids() const;
    std::wstring WatchName() const;

private:
    void Run(Game g);
    static bool RunCommandAndWait(const std::string& cmd, const std::string& workDir, uint32_t timeoutMs);

    std::atomic<Phase> phase_{Phase::Idle};
    std::thread thread_;
    std::mutex m_;
    std::optional<LaunchResult> result_;
    mutable std::mutex pm_;
    std::vector<uint32_t> pids_;
    std::wstring watch_;
};

const char* PhaseLabel(Launcher::Phase p);
