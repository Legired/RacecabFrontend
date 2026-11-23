#include "library/Database.h"

#include <sqlite3.h>

#include <unordered_map>

#include "core/Log.h"

namespace {

// Petit RAII autour d'une requête préparée.
class Stmt {
public:
    Stmt(sqlite3* db, const char* sql) : db_(db) {
        if (sqlite3_prepare_v2(db, sql, -1, &s_, nullptr) != SQLITE_OK)
            logx::Error("SQL prepare : {} ({})", sqlite3_errmsg(db), sql);
    }
    ~Stmt() { sqlite3_finalize(s_); }
    bool ok() const { return s_ != nullptr; }
    Stmt& Bind(int i, const std::string& v) {
        sqlite3_bind_text(s_, i, v.c_str(), (int)v.size(), SQLITE_TRANSIENT);
        return *this;
    }
    Stmt& Bind(int i, int64_t v) {
        sqlite3_bind_int64(s_, i, v);
        return *this;
    }
    Stmt& Bind(int i, int v) { return Bind(i, (int64_t)v); }
    Stmt& Bind(int i, bool v) { return Bind(i, (int64_t)(v ? 1 : 0)); }
    bool Step() { return sqlite3_step(s_) == SQLITE_ROW; }
    bool Run() {
        if (!s_) return false;
        int rc = sqlite3_step(s_);
        if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
            logx::Error("SQL : {}", sqlite3_errmsg(db_));
            return false;
        }
        return true;
    }
    std::string Text(int c) {
        auto p = sqlite3_column_text(s_, c);
        return p ? std::string(reinterpret_cast<const char*>(p)) : std::string();
    }
    int64_t Int(int c) { return sqlite3_column_int64(s_, c); }

private:
    sqlite3* db_;
    sqlite3_stmt* s_ = nullptr;
};

constexpr const char* kGameCols =
    "name, launch_path, args, workdir, run_mode, launch_delay_ms, pre_cmd, post_cmd, watch_process, "
    "alt_name, description, publisher, genre, platform, year, logo_path, fanart_path, video_path, "
    "enabled, favorite, video_url, video_start, video_duration";

void BindGame(Stmt& s, const Game& g) {
    s.Bind(1, g.name).Bind(2, g.launchPath).Bind(3, g.args).Bind(4, g.workDir).Bind(5, (int)g.runMode)
        .Bind(6, g.launchDelayMs).Bind(7, g.preCmd).Bind(8, g.postCmd).Bind(9, g.watchProcess)
        .Bind(10, g.altName).Bind(11, g.description).Bind(12, g.publisher).Bind(13, g.genre)
        .Bind(14, g.platform).Bind(15, g.year).Bind(16, g.logoPath).Bind(17, g.fanartPath)
        .Bind(18, g.videoPath).Bind(19, g.enabled).Bind(20, g.favorite).Bind(21, g.videoUrl).Bind(22, g.videoStart)
        .Bind(23, g.videoDuration);
}

}  // namespace

Database::~Database() { Close(); }

bool Database::Open(const fs::path& file) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    if (sqlite3_open(util::PathU8(file).c_str(), &db_) != SQLITE_OK) {
        logx::Error("Ouverture base impossible : {}", sqlite3_errmsg(db_));
        return false;
    }
    Exec("PRAGMA foreign_keys = ON");
    Exec("PRAGMA journal_mode = WAL");
    Exec("PRAGMA synchronous = NORMAL");
    return Migrate();
}

void Database::Close() {
    if (db_) sqlite3_close(db_);
    db_ = nullptr;
}

bool Database::Exec(const char* sql) {
    char* err = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        logx::Error("SQL : {} ({})", err ? err : "?", sql);
        sqlite3_free(err);
        return false;
    }
    return true;
}

bool Database::Migrate() {
    Stmt v(db_, "PRAGMA user_version");
    int version = v.Step() ? (int)v.Int(0) : 0;
    if (version < 1) {
        bool ok = Exec(R"SQL(
            CREATE TABLE IF NOT EXISTS games (
              id INTEGER PRIMARY KEY AUTOINCREMENT,
              name TEXT NOT NULL,
              launch_path TEXT NOT NULL,
              args TEXT NOT NULL DEFAULT '',
              workdir TEXT NOT NULL DEFAULT '',
              run_mode INTEGER NOT NULL DEFAULT 0,
              launch_delay_ms INTEGER NOT NULL DEFAULT 0,
              pre_cmd TEXT NOT NULL DEFAULT '',
              post_cmd TEXT NOT NULL DEFAULT '',
              watch_process TEXT NOT NULL DEFAULT '',
              alt_name TEXT NOT NULL DEFAULT '',
              description TEXT NOT NULL DEFAULT '',
              publisher TEXT NOT NULL DEFAULT '',
              genre TEXT NOT NULL DEFAULT '',
              platform TEXT NOT NULL DEFAULT '',
              year INTEGER NOT NULL DEFAULT 0,
              logo_path TEXT NOT NULL DEFAULT '',
              fanart_path TEXT NOT NULL DEFAULT '',
              video_path TEXT NOT NULL DEFAULT '',
              enabled INTEGER NOT NULL DEFAULT 1,
              favorite INTEGER NOT NULL DEFAULT 0,
              play_count INTEGER NOT NULL DEFAULT 0,
              play_seconds INTEGER NOT NULL DEFAULT 0,
              last_played INTEGER NOT NULL DEFAULT 0,
              added_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))
            );
            CREATE TABLE IF NOT EXISTS categories (
              id INTEGER PRIMARY KEY AUTOINCREMENT,
              parent_id INTEGER REFERENCES categories(id) ON DELETE CASCADE,
              name TEXT NOT NULL,
              sort INTEGER NOT NULL DEFAULT 0
            );
            CREATE TABLE IF NOT EXISTS game_categories (
              game_id INTEGER NOT NULL REFERENCES games(id) ON DELETE CASCADE,
              category_id INTEGER NOT NULL REFERENCES categories(id) ON DELETE CASCADE,
              PRIMARY KEY (game_id, category_id)
            );
            CREATE INDEX IF NOT EXISTS idx_gc_cat ON game_categories(category_id);
            PRAGMA user_version = 1;
        )SQL");
        if (!ok) return false;
        logx::Info("Base de données initialisée (schéma v1)");
        version = 1;
    }
    if (version < 2) {
        // Historique des parties : une ligne par partie (les compteurs de la table games sont conservés).
        bool ok = Exec(R"SQL(
            CREATE TABLE IF NOT EXISTS plays (
              id INTEGER PRIMARY KEY AUTOINCREMENT,
              game_id INTEGER NOT NULL REFERENCES games(id) ON DELETE CASCADE,
              started INTEGER NOT NULL,
              seconds INTEGER NOT NULL,
              profile TEXT NOT NULL DEFAULT ''
            );
            CREATE INDEX IF NOT EXISTS plays_started ON plays(started);
            PRAGMA user_version = 2;
        )SQL");
        if (!ok) return false;
        logx::Info("Base de données : schéma v2 (historique des parties)");
    }
    if (version < 3) {
        // Lien YouTube choisi à la main pour la vidéo du jeu (export / import de la configuration).
        bool ok = Exec(R"SQL(
            ALTER TABLE games ADD COLUMN video_url TEXT NOT NULL DEFAULT '';
            ALTER TABLE games ADD COLUMN video_start INTEGER NOT NULL DEFAULT 0;
            ALTER TABLE games ADD COLUMN video_duration INTEGER NOT NULL DEFAULT -1;
            PRAGMA user_version = 3;
        )SQL");
        if (!ok) return false;
        logx::Info("Base de données : schéma v3 (liens vidéo YouTube)");
    }
    return true;
}

std::vector<Game> Database::LoadGames() {
    std::vector<Game> out;
    Stmt s(db_, (std::string("SELECT id, ") + kGameCols +
                 ", play_count, play_seconds, last_played, added_at FROM games").c_str());
    while (s.Step()) {
        Game g;
        int c = 0;
        g.id = s.Int(c++);
        g.name = s.Text(c++);
        g.launchPath = s.Text(c++);
        g.args = s.Text(c++);
        g.workDir = s.Text(c++);
        g.runMode = (RunMode)s.Int(c++);
        g.launchDelayMs = (int)s.Int(c++);
        g.preCmd = s.Text(c++);
        g.postCmd = s.Text(c++);
        g.watchProcess = s.Text(c++);
        g.altName = s.Text(c++);
        g.description = s.Text(c++);
        g.publisher = s.Text(c++);
        g.genre = s.Text(c++);
        g.platform = s.Text(c++);
        g.year = (int)s.Int(c++);
        g.logoPath = s.Text(c++);
        g.fanartPath = s.Text(c++);
        g.videoPath = s.Text(c++);
        g.enabled = s.Int(c++) != 0;
        g.favorite = s.Int(c++) != 0;
        g.videoUrl = s.Text(c++);
        g.videoStart = (int)s.Int(c++);
        g.videoDuration = (int)s.Int(c++);
        g.playCount = (int)s.Int(c++);
        g.playSeconds = s.Int(c++);
        g.lastPlayed = s.Int(c++);
        g.addedAt = s.Int(c++);
        out.push_back(std::move(g));
    }
    // Liens catégories
    std::unordered_map<int64_t, size_t> idx;
    for (size_t i = 0; i < out.size(); ++i) idx[out[i].id] = i;
    Stmt l(db_, "SELECT game_id, category_id FROM game_categories");
    while (l.Step()) {
        auto it = idx.find(l.Int(0));
        if (it != idx.end()) out[it->second].categories.push_back(l.Int(1));
    }
    return out;
}

std::vector<Category> Database::LoadCategories() {
    std::vector<Category> out;
    Stmt s(db_, "SELECT id, IFNULL(parent_id, 0), name, sort FROM categories");
    while (s.Step()) out.push_back({s.Int(0), s.Int(1), s.Text(2), (int)s.Int(3)});
    return out;
}

int64_t Database::InsertGame(const Game& g) {
    Stmt s(db_, (std::string("INSERT INTO games (") + kGameCols +
                 ") VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)").c_str());
    BindGame(s, g);
    if (!s.Run()) return 0;
    int64_t id = sqlite3_last_insert_rowid(db_);
    SetGameCategories(id, g.categories);
    return id;
}

bool Database::UpdateGame(const Game& g) {
    Stmt s(db_,
           "UPDATE games SET name=?, launch_path=?, args=?, workdir=?, run_mode=?, launch_delay_ms=?, pre_cmd=?, "
           "post_cmd=?, watch_process=?, alt_name=?, description=?, publisher=?, genre=?, platform=?, year=?, "
           "logo_path=?, fanart_path=?, video_path=?, enabled=?, favorite=?, video_url=?, video_start=?, "
           "video_duration=? WHERE id=?");
    BindGame(s, g);
    s.Bind(24, g.id);
    return s.Run() && SetGameCategories(g.id, g.categories);
}

bool Database::DeleteGame(int64_t id) {
    Stmt s(db_, "DELETE FROM games WHERE id=?");
    s.Bind(1, id);
    return s.Run();
}

bool Database::SetGameCategories(int64_t gameId, const std::vector<int64_t>& cats) {
    Stmt d(db_, "DELETE FROM game_categories WHERE game_id=?");
    d.Bind(1, gameId);
    if (!d.Run()) return false;
    for (int64_t c : cats) {
        Stmt i(db_, "INSERT OR IGNORE INTO game_categories (game_id, category_id) VALUES (?,?)");
        i.Bind(1, gameId).Bind(2, c);
        if (!i.Run()) return false;
    }
    return true;
}

bool Database::SetFavorite(int64_t id, bool fav) {
    Stmt s(db_, "UPDATE games SET favorite=? WHERE id=?");
    s.Bind(1, fav).Bind(2, id);
    return s.Run();
}

bool Database::RecordPlay(int64_t id, int64_t seconds, int64_t when, const std::string& profile) {
    Stmt s(db_, "UPDATE games SET play_count=play_count+1, play_seconds=play_seconds+?, last_played=? WHERE id=?");
    s.Bind(1, seconds).Bind(2, when).Bind(3, id);
    if (!s.Run()) return false;
    Stmt h(db_, "INSERT INTO plays(game_id, started, seconds, profile) VALUES(?,?,?,?)");
    h.Bind(1, id).Bind(2, when - seconds).Bind(3, seconds).Bind(4, profile);
    return h.Run();
}

std::vector<Database::PlayEntry> Database::History(int limit) {
    std::vector<PlayEntry> out;
    Stmt s(db_, "SELECT p.game_id, COALESCE(g.name, ''), p.started, p.seconds, p.profile FROM plays p "
                "LEFT JOIN games g ON g.id = p.game_id ORDER BY p.started DESC, p.id DESC LIMIT ?");
    s.Bind(1, limit);
    while (s.Step()) out.push_back({s.Int(0), s.Text(1), s.Int(2), s.Int(3), s.Text(4)});
    return out;
}

Database::Totals Database::PlayTotals() {
    Totals t;
    Stmt s(db_, "SELECT COUNT(*), COALESCE(SUM(seconds),0), COUNT(DISTINCT game_id), COALESCE(MIN(started),0) FROM plays");
    if (s.Step()) t = {s.Int(0), s.Int(1), s.Int(2), s.Int(3)};
    return t;
}

bool Database::ClearHistory() {
    return Exec("DELETE FROM plays; UPDATE games SET play_count=0, play_seconds=0, last_played=0;");
}

int64_t Database::InsertCategory(const std::string& name, int64_t parent, int sort) {
    Stmt s(db_, "INSERT INTO categories (parent_id, name, sort) VALUES (NULLIF(?,0), ?, ?)");
    s.Bind(1, parent).Bind(2, name).Bind(3, sort);
    return s.Run() ? sqlite3_last_insert_rowid(db_) : 0;
}

bool Database::UpdateCategory(const Category& c) {
    Stmt s(db_, "UPDATE categories SET parent_id=NULLIF(?,0), name=?, sort=? WHERE id=?");
    s.Bind(1, c.parentId).Bind(2, c.name).Bind(3, c.sort).Bind(4, c.id);
    return s.Run();
}

bool Database::DeleteCategory(int64_t id) {
    Stmt s(db_, "DELETE FROM categories WHERE id=?");
    s.Bind(1, id);
    return s.Run();
}
