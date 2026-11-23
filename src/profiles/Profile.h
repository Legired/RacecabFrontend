#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/Util.h"

struct VideoSettings {
    bool enabled = true;
    bool loop = true;
    float volume = 0.6f;
    bool mute = false;
    int delayMs = 400;  // la vidéo démarre quand la sélection reste stable ce temps-là
    bool hwDecode = true;
    bool blurFill = true;  // bandes autour de la vidéo remplies par la vidéo elle-même, floue et assombrie
    bool autoCrop = true;  // bandes noires incrustées dans la vidéo détectées et retirées à l'affichage
};

// Mode démonstration. Il ne lance JAMAIS de jeu : il ne fait que présenter la bibliothèque.
struct AttractSettings {
    bool enabled = true;
    int idleSeconds = 120;          // inactivité avant démarrage
    int displaySeconds = 12;        // durée d'affichage de chaque jeu
    bool random = true;             // sinon : ordre de la liste
    std::vector<int64_t> categories;  // catégories présentées (vide = toutes celles du profil)
    bool video = true;
    float volume = 0.3f;
    bool mute = false;
    float speed = 0.6f;             // vitesse de défilement de la roue (x vitesse du thème)
};

// Profil de borne : <données>/config/profiles/<nom>.json
// Mapping des commandes : config/input/<nom>.json ; état de navigation : config/state-<nom>.json
struct Profile {
    std::string name = "Racecab";
    std::string theme = "default";
    std::string mode;              // "racecab", "guncab" ou vide (proposé sur les deux types de borne)
    std::string sort = "name";     // tri des jeux : "name", "year", "played" (temps de jeu), "count" (lancements)
    std::vector<int64_t> hiddenCategories;  // masquées dans ce profil (avec leurs sous-catégories)
    bool filterGames = true;       // ne montrer que les jeux d'au moins une catégorie visible (si des catégories sont masquées)
    bool showAutoCategories = true;  // Favoris et Récemment joués
    VideoSettings video;
    AttractSettings attract;

    bool FitsMode(const std::string& cabMode) const { return mode.empty() || mode == cabMode; }
    bool Load(const fs::path& file);
    bool Save(const fs::path& file) const;

    static std::vector<std::string> List(const fs::path& dir);
    // Crée les profils par défaut s'il n'en existe aucun.
    static void EnsureDefaults(const fs::path& dir);
};
