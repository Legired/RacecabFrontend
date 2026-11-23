#pragma once
#include <cstdint>
#include <string>

#include "core/Util.h"

struct InputTiming {
    int repeatDelayMs = 350;   // délai avant répétition d'un appui maintenu
    int repeatRateMs = 70;     // intervalle entre deux répétitions
    int debounceMs = 40;       // anti-rebond : appui ignoré s'il suit un relâchement de moins de X ms
    int selectGuardMs = 700;   // anti double validation
    float axisThreshold = 0.6f;
    int holdMs = 1200;         // appui long (ex. maintenir Retour = menu de la borne)
    int accelAfterMs = 1500;   // défilement accéléré après X ms de maintien (0 = désactivé)
    int returnGuardMs = 800;   // entrées ignorées juste après le retour d'un jeu
};

// Mode pistolet (borne GunCab) : les pistolets (Sinden, Gun4IR, AimTrak...) se présentent comme une souris.
struct GunSettings {
    bool crosshair = true;          // viseur dessiné à la position visée
    std::string crosshairColor = "white";  // "white", "yellow", "green", "red" ou "theme" (couleur d'accent du thème)
    int crosshairSize = 100;        // taille en % (60 à 200)
    int borderPercent = 0;          // bordure blanche autour de l'écran (pistolets Sinden), en % de la hauteur ; 0 = aucune
    int borderColor[3] = {255, 255, 255};
    int launchGuardMs = 700;        // un tir sur le jeu sélectionné ne le lance qu'après ce délai depuis sa sélection
    bool arcadeLayout = true;       // disposition pistolet : grande image + bandes flèches (sinon : roue classique)
    std::string view = "single";    // "single" (grande image) ou "mosaic" (dernière vue utilisée)
    int mosaicCols = 4, mosaicRows = 3;
};

// Paramètres globaux : <données>/config/settings.json
struct Settings {
    // Type de borne : "racecab" (volant, boutons) ou "guncab" (pistolet). Choisi à l'installation ;
    // il détermine le profil de démarrage, les profils proposés et les commandes à la souris / au pistolet.
    std::string cabMode = "racecab";
    bool kiosk = false;            // borne publique : plein écran imposé, « Quitter » retiré du menu de la borne
    bool autoBackup = true;        // sauvegarde automatique de la configuration (une fois par jour au démarrage)
    int backupKeep = 10;           // nombre de sauvegardes automatiques conservées
    std::string updateRepo = "Legired/RacecabFrontend";  // dépôt GitHub public des mises à jour (releases)
    bool updateAutoCheck = true;   // vérifier au démarrage (une requête, sans rien télécharger)
    GunSettings gun;
    std::string theme = "default";
    bool fullscreen = true;
    int windowW = 1280;
    int windowH = 720;
    bool vsync = true;
    bool hideCursor = true;
    bool minimizeDuringGame = true;
    int launchScreenMaxSec = 60;    // écran de chargement gardé au plus X s si aucune fenêtre de jeu n'est repérée
    int textureCacheMB = 256;
    int maxImageSide = 1920;  // les images plus grandes sont réduites au chargement
    std::string profile = "Racecab";  // profil actif (les réglages vidéo / démo / thème sont dans le profil)
    bool askProfileAtStartup = false;
    InputTiming input;

    bool Load(const fs::path& file);
    bool Save(const fs::path& file) const;
};

// État de navigation sauvegardé avant chaque lancement et à la fermeture.
struct NavState {
    int64_t node = -1;        // catégorie affichée (voir Library::kAll, etc.)
    int itemKind = 1;         // 0 = catégorie, 1 = jeu
    int64_t itemId = 0;

    bool Load(const fs::path& file);
    bool Save(const fs::path& file) const;
};

// Résolution des dossiers. Le dossier de données est indépendant du dossier du programme.
struct Paths {
    fs::path exeDir;
    fs::path dataDir;

    fs::path ConfigDir() const { return dataDir / L"config"; }
    fs::path SettingsFile() const { return ConfigDir() / L"settings.json"; }
    fs::path StateFile() const { return ConfigDir() / L"state.json"; }
    fs::path InputFile() const { return ConfigDir() / L"input.json"; }  // ancien emplacement (migration)
    fs::path ProfilesDir() const { return ConfigDir() / L"profiles"; }
    fs::path ProfileFile(const std::string& p) const { return ProfilesDir() / (util::Widen(p) + L".json"); }
    fs::path InputFileFor(const std::string& p) const { return ConfigDir() / L"input" / (util::Widen(p) + L".json"); }
    fs::path StateFileFor(const std::string& p) const { return ConfigDir() / (L"state-" + util::Widen(p) + L".json"); }
    fs::path ScraperFile() const { return ConfigDir() / L"scraper.json"; }
    fs::path DbFile() const { return dataDir / L"db" / L"library.sqlite"; }
    fs::path ThemesDir() const { return dataDir / L"themes"; }
    fs::path MediaDir() const { return dataDir / L"media"; }
    fs::path LogsDir() const { return dataDir / L"logs"; }
    fs::path BackupsDir() const { return dataDir / L"backups"; }
    fs::path RestorePending() const { return dataDir / L"restore-pending.zip"; }

    // Priorité : --data <chemin>  >  datadir.txt à côté de l'exe  >  H:\RacecabData
    static Paths Resolve(int argc, char** argv);
    void EnsureDirs() const;
};
