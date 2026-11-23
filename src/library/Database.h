#pragma once
#include <optional>
#include <string>
#include <vector>

#include "core/Util.h"
#include "library/Model.h"

struct sqlite3;

// Accès SQLite. Toutes les écritures passent par ici ; aucune ne touche aux fichiers des jeux.
class Database {
public:
    ~Database();
    bool Open(const fs::path& file);
    void Close();

    std::vector<Game> LoadGames();
    std::vector<Category> LoadCategories();

    int64_t InsertGame(const Game& g);
    bool UpdateGame(const Game& g);
    bool DeleteGame(int64_t id);
    bool SetGameCategories(int64_t gameId, const std::vector<int64_t>& cats);
    bool SetFavorite(int64_t id, bool fav);
    bool RecordPlay(int64_t id, int64_t seconds, int64_t when, const std::string& profile = {});

    // Historique des parties (schéma v2)
    struct PlayEntry {
        int64_t gameId = 0;
        std::string name;   // nom du jeu (vide si le jeu a été supprimé)
        int64_t started = 0, seconds = 0;
        std::string profile;
    };
    std::vector<PlayEntry> History(int limit);
    struct Totals {
        int64_t plays = 0, seconds = 0, gamesPlayed = 0, firstPlay = 0;
    };
    Totals PlayTotals();
    bool ClearHistory();  // efface l'historique et remet les compteurs à zéro

    int64_t InsertCategory(const std::string& name, int64_t parent, int sort);
    bool UpdateCategory(const Category& c);
    bool DeleteCategory(int64_t id);  // les liens jeu<->catégorie partent en cascade

    bool Exec(const char* sql);
    bool Begin() { return Exec("BEGIN"); }
    bool Commit() { return Exec("COMMIT"); }

private:
    bool Migrate();
    sqlite3* db_ = nullptr;
};
