#pragma once
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "library/Database.h"

enum class ItemKind { Category = 0, Game = 1 };

struct WheelItem {
    ItemKind kind;
    int64_t id;
    bool operator==(const WheelItem&) const = default;
};

// Bibliothèque en mémoire + persistance SQLite.
// Nœuds de navigation : 0 = racine, kAll/kFavorites/kRecent = catégories automatiques, >0 = catégories utilisateur.
class Library {
public:
    static constexpr int64_t kRoot = 0;
    static constexpr int64_t kAll = -1;
    static constexpr int64_t kFavorites = -2;
    static constexpr int64_t kRecent = -3;
    static constexpr int kRecentLimit = 25;

    bool Open(const fs::path& dbFile, const fs::path& mediaDir);

    const std::vector<Game>& Games() const { return games_; }
    const std::vector<Category>& Categories() const { return cats_; }
    const Game* FindGame(int64_t id) const;
    const Category* FindCategory(int64_t id) const;

    // Navigation
    std::string NodeName(int64_t node) const;
    std::string NodePath(int64_t node) const;  // "Course › Drift"
    int64_t ParentOf(int64_t node) const;
    // Triées. all = true : ignore le filtre du profil (administration).
    std::vector<int64_t> ChildCategories(int64_t node, bool all = false) const;

    // Filtre du profil actif (navigation uniquement ; l'administration voit tout).
    void SetProfileFilter(std::vector<int64_t> hidden, bool filterGames, bool showAuto);
    void SetSort(const std::string& mode) { sort_ = mode; }  // "name", "year", "played", "count"
    const std::string& Sort() const { return sort_; }
    bool CategoryHidden(int64_t id) const;  // elle-même ou un parent masqué
    bool GameVisible(const Game& g) const;
    std::vector<int64_t> AttractGames(const std::vector<int64_t>& categories) const;  // vide = tout le profil
    std::vector<int64_t> Siblings(int64_t node) const;
    std::vector<WheelItem> Items(int64_t node) const;          // sous-catégories puis jeux
    int CountGames(int64_t node) const;                         // jeux directs
    bool IsDescendant(int64_t node, int64_t ancestor) const;
    bool NodeExists(int64_t node) const;

    // Médias : chemin explicite, sinon <media>/<id>/logo.png|fanart.jpg...
    std::string LogoFor(const Game& g) const;
    std::string FanartFor(const Game& g) const;
    std::string VideoFor(const Game& g) const;
    fs::path MediaDirFor(int64_t gameId) const { return mediaDir_ / std::to_wstring(gameId); }

    // Modifications
    int64_t AddGame(Game g);
    bool UpdateGame(const Game& g);
    bool RemoveGame(int64_t id);  // retire de la bibliothèque uniquement, ne touche jamais au disque
    bool ToggleFavorite(int64_t id);
    void RecordPlay(int64_t id, int64_t seconds, const std::string& profile = {});
    Database& Db() { return db_; }
    bool ClearStats();  // efface l'historique et remet compteurs / temps de jeu à zéro
    std::optional<int64_t> FindDuplicate(const std::string& launchPath, const std::string& args, int64_t exceptId) const;

    int64_t AddCategory(const std::string& name, int64_t parent);
    bool RenameCategory(int64_t id, const std::string& name);
    bool MoveCategory(int64_t id, int64_t newParent);
    bool ShiftCategory(int64_t id, int delta);  // ordre parmi les sœurs
    bool DeleteCategory(int64_t id);
    int64_t EnsureCategoryPath(const std::string& path);  // "Course/Drift", crée au besoin

    // Import d'une liste JSON (voir docs/Import.md). Les doublons (même fichier + arguments) sont ignorés.
    bool ImportList(const fs::path& file, int& added, int& duplicates, std::string& error);

private:
    void Reload();
    void Reindex();
    std::string MediaCandidate(int64_t id, std::initializer_list<const wchar_t*> names) const;

    Database db_;
    fs::path mediaDir_;
    std::vector<Game> games_;
    std::vector<Category> cats_;
    std::unordered_map<int64_t, size_t> gameIdx_;
    std::unordered_map<int64_t, size_t> catIdx_;
    std::vector<int64_t> hidden_;
    bool filterGames_ = true;
    bool showAuto_ = true;
    std::string sort_ = "name";
};
