#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "core/Util.h"
#include "library/Model.h"

// Recherche automatique de jeux dans un dossier (Administration > Jeux > « Scanner un dossier »).
// Rien n'est modifié sur le disque : on ne fait que lire.
//
// Ce qui est reconnu comme jeu :
//  - un dossier de jeu : son lanceur principal (.bat nommé run / launch / start / play…, sinon le plus gros .exe) ;
//  - un dossier « de lanceurs » (au moins deux .bat, ex. H:\windowsloader) : chaque .bat est un jeu ;
//  - un jeu LinuxLoader : dossier avec un ELF « game » + linuxloader.ini, ou un fichier .elf (disk0\…\*.elf),
//    lancé directement par linuxloader.exe (sans .bat, console masquée).
// Ignorés : installateurs, désinstalleurs, redistribuables, outils de compilation, dossiers de sauvegarde…
struct ScanCandidate {
    std::string name;          // nom proposé (nom du dossier nettoyé, ou du .bat)
    std::string rawName;       // nom d'origine, avant abréviation développée (« id4 »)
    std::string launchPath, args, workDir;
    RunMode runMode = RunMode::Auto;
    std::string kind;          // "exe", "bat", "linuxloader"
    std::string note;          // précision affichée (pause en fin de .bat, loader trouvé ailleurs…)
    // Reconnaissance (base LaunchBox locale), remplie après le scan
    std::string lbName, lbPublisher, lbGenre, lbPlatform, lbOverview;
    int lbYear = 0;
    bool existing = false;     // déjà dans la bibliothèque (même fichier et mêmes arguments, ou même nom)
    std::string existingName;  // nom du jeu déjà présent
    std::string dupOf;         // doublon d'un autre résultat de la liste (meilleur lanceur gardé)
    // ROM d'émulateur : émulateurs possibles (le conseillé en premier) et choix en cours (-1 = à choisir)
    struct EmuChoice {
        std::string label, launchPath, args, workDir, note, watch;
        RunMode runMode = RunMode::Auto;
        bool recommended = false;
        bool custom = false;   // « Custom » : lanceur saisi à la main (champs vides au départ)
    };
    std::vector<EmuChoice> emuChoices;
    int emuChoice = -1;
    std::string system;        // système de la ROM (« Model 2 »…), pour l'affichage
    std::string watchProcess;  // processus à surveiller (lanceur qui passe la main au jeu)
    bool needsSetup = false;   // ne peut pas être ajouté tel quel (ex. profil TeknoParrot sans chemin de jeu)
};

namespace scanner {

// Parcourt root (profondeur 6 au plus). progress : dossier en cours. cancel : arrêt demandé.
std::vector<ScanCandidate> Scan(const fs::path& root, const std::atomic<bool>& cancel,
                                const std::function<void(const std::string&)>& progress);

// « Initial D Arcade Stage 8 Infinity (2014) (Japan) [Sega RingEdge] [TP] » -> « Initial D Arcade Stage 8 Infinity »
std::string CleanName(const std::string& raw);

// Abréviations courantes des jeux d'arcade (« id4 », « twd », « swdc »…) -> titre complet ; vide si inconnue.
std::string Alias(const std::string& name);

}  // namespace scanner
