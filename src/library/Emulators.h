#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "library/Scanner.h"

// Émulateurs et ROMs (Administration > Jeux > « Émulateurs et ROMs »).
//
// 1. Detect : cherche dans le dossier des émulateurs les programmes connus (catalogue ci-dessous).
// 2. ScanRoms : liste les ROMs du dossier des ROMs (et du dossier propre de chaque émulateur) ; le système
//    (Model 2, Model 3, Naomi…) vient du nom du jeu (tables intégrées, Games.xml de Supermodel) ou du nom du
//    dossier (« model2 », « naomi »…). Chaque ROM reçoit la liste des émulateurs capables de la lancer, le
//    conseillé en premier ; l'utilisateur peut en choisir un autre dans la liste des jeux trouvés.
// 3. TeknoParrot : chaque profil configuré (UserProfiles\*.xml) devient un jeu (« TeknoParrotUi.exe --profile=… »).
// Rien n'est modifié sur le disque pendant la recherche.
namespace emu {

struct Def {
    std::string id, name;
    std::vector<std::string> exeNames;  // en minuscules
    std::string marker;                 // fichier qui doit être à côté de l'exe (ex. EMULATOR.INI) ; vide = aucun
    std::string args;                   // {rom} nom court, {romfile} chemin complet, {romdir}, {emudir}, {demulsys}
    std::vector<std::string> exts;      // extensions acceptées (minuscules, avec le point)
    std::vector<std::string> systems;   // systèmes pris en charge ; "*" = tous (MAME)
    std::string note;                   // remarque affichée à côté du lanceur
};

struct Found {
    const Def* def = nullptr;
    fs::path exe;
    std::string label;  // « sm2-emu (prebuilt\sm2-emu-v0.9.34-windows-x86_64) »
};

const std::vector<Def>& Catalog();

// Émulateurs connus sous root (profondeur 5). Plusieurs exécutables du même émulateur : le plus récent d'abord.
std::vector<Found> Detect(const fs::path& root, const std::atomic<bool>& cancel);

// ROMs de romRoot (profondeur 3) et des dossiers des émulateurs trouvés. Une ROM sans émulateur possible
// est quand même listée (« aucun émulateur trouvé pour ce système »), jamais cochée.
std::vector<ScanCandidate> ScanRoms(const fs::path& romRoot, const fs::path& emuRoot, const std::vector<Found>& emus,
                                    const std::atomic<bool>& cancel,
                                    const std::function<void(const std::string&)>& progress);

// Jeux configurés dans TeknoParrot (profils de UserProfiles).
std::vector<ScanCandidate> ScanTeknoParrot(const Found& tp);

// Applique le choix d'émulateur n° i (lanceur, arguments, dossier de travail).
void Choose(ScanCandidate& c, int i);

// Système d'une ROM d'après son nom court (« daytona » -> « model2 ») ; vide si inconnu.
std::string SystemOf(const std::string& romName);
// Titre connu d'une ROM (« scud » -> « Scud Race ») ; vide si inconnu.
std::string TitleOf(const std::string& romName);

}  // namespace emu
