#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "core/Util.h"

struct sqlite3;

// Base de données de jeux LaunchBox (gratuite, sans compte) utilisée hors ligne.
//   Metadata.zip (https://gamesdb.launchbox-app.com/Metadata.zip, ~110 Mo, mis à jour chaque jour) contient
//   Metadata.xml : éléments <Game>, puis <GameAlternateName>, puis <GameImage> (DatabaseID, FileName, Type, Region).
//   Les images sont à https://images.launchbox-app.com/<FileName>. Vidéos : lien YouTube <VideoURL> du jeu.
// Seules les plateformes choisies (par défaut « Arcade ») sont gardées dans un index SQLite local
// (<données>\cache\launchbox\launchbox.db) : la recherche est instantanée et ne demande pas Internet.
struct LbGame {
    std::string id, name, platform, date, publisher, developer, genres, overview;
    std::string video;  // lien YouTube (VideoURL), vide si aucun
};
struct LbImage {
    std::string type, region, file;
};
struct LbMatch {
    LbGame game;
    bool exact = false;       // nom ou nom alternatif identique (après normalisation)
    std::string matchedAlt;   // nom alternatif qui a donné la correspondance exacte
};

class LaunchBoxDb {
public:
    ~LaunchBoxDb();
    // Construit l'index depuis Metadata.zip (remplacement atomique de l'ancien index).
    static bool BuildIndex(const fs::path& zip, const fs::path& dbFile, const std::vector<std::string>& platforms,
                           const std::atomic<bool>& cancel, const std::function<void(uint64_t mb)>& progress,
                           std::string& summary, std::string& err);
    // Résumé de l'index (date, nombre de jeux, plateformes) ; vide si absent.
    static std::string Describe(const fs::path& dbFile);
    // false si l'index date d'avant les liens vidéo (à reconstruire depuis Metadata.zip).
    static bool HasVideoLinks(const fs::path& dbFile);

    bool Open(const fs::path& dbFile, std::string& err);  // ouvre en lecture seule (rouvre si le fichier a changé)
    void Close();
    std::vector<LbMatch> Search(const std::string& query, size_t max = 30);
    bool Get(const std::string& id, LbGame& g, std::vector<LbImage>& images);

private:
    sqlite3* db_ = nullptr;
    fs::path file_;
    fs::file_time_type stamp_{};
    bool hasVideos_ = false;
};
