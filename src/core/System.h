#pragma once
#include <string>

#include "core/Util.h"

// Intégration Windows : démarrage automatique, surveillance (relance après plantage), redémarrage.
namespace sys {

fs::path ExePath();

// Démarrage avec la session Windows : HKCU\Software\Microsoft\Windows\CurrentVersion\Run\RacecabFrontend
// (compte de l'utilisateur seulement, aucun droit administrateur). Lance le frontend sous surveillance.
bool AutoStartEnabled();
bool SetAutoStart(bool on, const fs::path& dataDir, std::string& err);

// Code de sortie demandant un redémarrage immédiat (ex. après une restauration).
constexpr int kRestartExitCode = 3;
bool UnderWatchdog();  // lancé par la surveillance (variable d'environnement RACECAB_WATCHDOG)
// Relance le programme avec les mêmes arguments (+ --restart, qui attend la fin de l'instance actuelle).
bool SpawnSelf(const std::wstring& extraArg, std::string& err);

// Surveillance (--watchdog) : lance le frontend et le relance s'il se ferme anormalement
// (plantage) ; s'arrête quand il se ferme normalement. Au-delà de 5 plantages en 10 minutes, abandonne.
int RunWatchdog();

}  // namespace sys
