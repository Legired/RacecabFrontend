#pragma once
#include <cstdint>
#include <string>
#include <vector>

enum class RunMode : int { Auto = 0, Normal = 1, Minimized = 2, Hidden = 3 };

struct Game {
    int64_t id = 0;
    // Indispensable
    std::string name;
    std::string launchPath;  // .bat ou .exe
    // Lancement
    std::string args;
    std::string workDir;      // vide = dossier du fichier lancé
    RunMode runMode = RunMode::Auto;
    int launchDelayMs = 0;
    std::string preCmd;       // commande exécutée avant le jeu (attendue)
    std::string postCmd;      // commande exécutée après le jeu (attendue)
    std::string watchProcess; // ex. "game.exe" : à surveiller si le jeu sort du Job (lanceurs qui délèguent)
    // Métadonnées (facultatives)
    std::string altName, description, publisher, genre, platform;
    int year = 0;
    // Médias (facultatifs ; sinon convention <données>/media/<id>/logo.png ...)
    std::string logoPath, fanartPath, videoPath;
    // Vidéo YouTube choisie à la main (gardée pour l'export / import et pour la retélécharger)
    std::string videoUrl;
    int videoStart = 0;      // début de l'extrait (s)
    int videoDuration = -1;  // durée (s) ; 0 = jusqu'à la fin ; -1 = durée des réglages
    // État
    bool enabled = true;
    bool favorite = false;
    int playCount = 0;
    int64_t playSeconds = 0;
    int64_t lastPlayed = 0;
    int64_t addedAt = 0;
    std::vector<int64_t> categories;
};

struct Category {
    int64_t id = 0;
    int64_t parentId = 0;  // 0 = racine
    std::string name;
    int sort = 0;
};
