#include "media/LaunchBox.h"

#include <sqlite3.h>
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <ctime>
#include <format>
#include <unordered_set>

#include "core/Log.h"
#include "media/Scraper.h"
#include "media/Zip.h"

namespace {

class Q {
public:
    Q(sqlite3* db, const char* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &s_, nullptr) != SQLITE_OK) logx::Error("LaunchBox SQL : {}", sqlite3_errmsg(db));
    }
    ~Q() { sqlite3_finalize(s_); }
    Q& Bind(int i, const std::string& v) {
        sqlite3_bind_text(s_, i, v.c_str(), (int)v.size(), SQLITE_TRANSIENT);
        return *this;
    }
    bool Step() { return s_ && sqlite3_step(s_) == SQLITE_ROW; }
    void Run() {
        if (s_) {
            sqlite3_step(s_);
            sqlite3_reset(s_);
            sqlite3_clear_bindings(s_);
        }
    }
    std::string Text(int c) {
        auto p = sqlite3_column_text(s_, c);
        return p ? std::string((const char*)p) : std::string();
    }

private:
    sqlite3_stmt* s_ = nullptr;
};

// Décodage des entités XML (&amp; &lt; &gt; &quot; &apos; &#N; &#xN;) en UTF-8.
std::string Unescape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') {
            out += s[i];
            continue;
        }
        size_t semi = s.find(';', i);
        if (semi == std::string_view::npos || semi - i > 10) {
            out += s[i];
            continue;
        }
        std::string_view ent = s.substr(i + 1, semi - i - 1);
        uint32_t cp = 0;
        if (ent == "amp") cp = '&';
        else if (ent == "lt") cp = '<';
        else if (ent == "gt") cp = '>';
        else if (ent == "quot") cp = '"';
        else if (ent == "apos") cp = '\'';
        else if (ent.size() > 1 && ent[0] == '#')
            cp = ent[1] == 'x' || ent[1] == 'X' ? (uint32_t)strtoul(std::string(ent.substr(2)).c_str(), nullptr, 16)
                                                : (uint32_t)strtoul(std::string(ent.substr(1)).c_str(), nullptr, 10);
        if (!cp) {
            out += s[i];
            continue;
        }
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) out += {(char)(0xC0 | cp >> 6), (char)(0x80 | (cp & 0x3F))};
        else if (cp < 0x10000) out += {(char)(0xE0 | cp >> 12), (char)(0x80 | (cp >> 6 & 0x3F)), (char)(0x80 | (cp & 0x3F))};
        else out += {(char)(0xF0 | cp >> 18), (char)(0x80 | (cp >> 12 & 0x3F)), (char)(0x80 | (cp >> 6 & 0x3F)), (char)(0x80 | (cp & 0x3F))};
        i = semi;
    }
    return out;
}

std::string Field(std::string_view elem, const char* tag) {
    std::string open = std::string("<") + tag + ">", close = std::string("</") + tag + ">";
    size_t a = elem.find(open);
    if (a == std::string_view::npos) return {};
    a += open.size();
    size_t b = elem.find(close, a);
    if (b == std::string_view::npos) return {};
    return Unescape(elem.substr(a, b - a));
}

// Mots en minuscules entourés d'espaces (« initial d arcade stage 4 ») pour la recherche par mots entiers.
std::string Words(const std::string& s) {
    std::string out = " ";
    for (unsigned char c : s) {
        if (std::isalnum(c)) out += (char)std::tolower(c);
        else if (out.back() != ' ') out += ' ';
    }
    if (out.back() != ' ') out += ' ';
    return out;
}

std::vector<std::string> Tokens(const std::string& s) {
    std::vector<std::string> t;
    std::string w = Words(s), cur;
    for (char c : w) {
        if (c == ' ') {
            if (!cur.empty()) t.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    return t;
}

// Types d'images gardés dans l'index (les autres : boîtes, disques... sont inutiles sur une borne).
bool WantedImage(const std::string& t) {
    return t == "Clear Logo" || t == "Fanart - Background" || t == "Screenshot - Gameplay" ||
           t == "Screenshot - Game Title" || t == "Arcade - Marquee";
}

}  // namespace

bool LaunchBoxDb::BuildIndex(const fs::path& zip, const fs::path& dbFile, const std::vector<std::string>& platforms,
                             const std::atomic<bool>& cancel, const std::function<void(uint64_t)>& progress,
                             std::string& summary, std::string& err) {
    fs::path tmp = dbFile;
    tmp += L".tmp";
    std::error_code ec;
    fs::create_directories(dbFile.parent_path(), ec);
    fs::remove(tmp, ec);
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(util::PathU8(tmp).c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        err = "création de l'index impossible";
        sqlite3_close(db);
        return false;
    }
    sqlite3_exec(db,
                 "PRAGMA journal_mode=OFF; PRAGMA synchronous=OFF;"
                 "CREATE TABLE games(id TEXT PRIMARY KEY, name TEXT, platform TEXT, date TEXT, publisher TEXT,"
                 " developer TEXT, genres TEXT, overview TEXT);"
                 "CREATE TABLE names(id TEXT, name TEXT, norm TEXT, words TEXT, alt INTEGER);"
                 "CREATE TABLE images(id TEXT, type TEXT, region TEXT, file TEXT);"
                 "CREATE TABLE videos(id TEXT PRIMARY KEY, url TEXT);"
                 "CREATE TABLE info(key TEXT PRIMARY KEY, value TEXT);"
                 "BEGIN;",
                 nullptr, nullptr, nullptr);
    std::unordered_set<std::string> plat(platforms.begin(), platforms.end()), kept;
    size_t nGames = 0, nAlt = 0, nImg = 0, nVid = 0;
    {
        Q insGame(db, "INSERT OR IGNORE INTO games VALUES(?,?,?,?,?,?,?,?)");
        Q insName(db, "INSERT INTO names VALUES(?,?,?,?,?)");
        Q insImg(db, "INSERT INTO images VALUES(?,?,?,?)");
        Q insVid(db, "INSERT OR IGNORE INTO videos VALUES(?,?)");
        auto addName = [&](const std::string& id, const std::string& n, bool alt) {
            insName.Bind(1, id).Bind(2, n).Bind(3, Scraper::Normalize(n)).Bind(4, Words(n)).Bind(5, std::string(alt ? "1" : "0"));
            insName.Run();
        };

        // Analyse en flux : on cherche les éléments de premier niveau <Game>, <GameAlternateName>, <GameImage>.
        std::string buf;
        size_t off = 0;
        uint64_t total = 0, lastMb = 0;
        auto handle = [&](const std::string& tag, std::string_view e) {
            if (tag == "Game") {
                std::string p = Field(e, "Platform");
                if (!plat.count(p)) return;
                std::string id = Field(e, "DatabaseID"), name = Field(e, "Name");
                if (id.empty() || name.empty()) return;
                std::string date = Field(e, "ReleaseDate");
                if (date.empty()) date = Field(e, "ReleaseYear");
                insGame.Bind(1, id).Bind(2, name).Bind(3, p).Bind(4, date).Bind(5, Field(e, "Publisher"))
                    .Bind(6, Field(e, "Developer")).Bind(7, Field(e, "Genres")).Bind(8, Field(e, "Overview"));
                insGame.Run();
                addName(id, name, false);
                kept.insert(id);
                if (std::string v = util::Trim(Field(e, "VideoURL")); !v.empty()) {
                    insVid.Bind(1, id).Bind(2, v);
                    insVid.Run();
                    ++nVid;
                }
                ++nGames;
            } else if (tag == "GameAlternateName") {
                std::string id = Field(e, "DatabaseID");
                if (!kept.count(id)) return;
                std::string n = Field(e, "AlternateName");
                if (n.empty()) return;
                addName(id, n, true);
                ++nAlt;
            } else if (tag == "GameImage") {
                std::string id = Field(e, "DatabaseID");
                if (!kept.count(id)) return;
                std::string t = Field(e, "Type"), file = Field(e, "FileName");
                if (!WantedImage(t) || file.empty() || file.find_first_of("/\\") != std::string::npos) return;
                insImg.Bind(1, id).Bind(2, t).Bind(3, Field(e, "Region")).Bind(4, file);
                insImg.Run();
                ++nImg;
            }
        };
        bool ok = ZipStreamEntry(zip, "Metadata.xml", [&](const char* p, size_t n) {
            if (cancel) return false;
            buf.append(p, n);
            total += n;
            if (progress && total / (16 << 20) != lastMb) {
                lastMb = total / (16 << 20);
                progress(total >> 20);
            }
            for (;;) {
                size_t a = buf.find("<Game", off);
                if (a == std::string::npos) {
                    off = std::max(off, buf.size() > 32 ? buf.size() - 32 : 0);  // « <Game » coupé entre deux blocs
                    break;
                }
                size_t gt = buf.find('>', a);
                if (gt == std::string::npos) {
                    off = a;
                    break;
                }
                std::string tag = buf.substr(a + 1, gt - a - 1);
                if (tag != "Game" && tag != "GameAlternateName" && tag != "GameImage") {
                    off = gt + 1;
                    continue;
                }
                std::string close = "</" + tag + ">";
                size_t b = buf.find(close, gt);
                if (b == std::string::npos) {
                    off = a;
                    break;
                }
                handle(tag, std::string_view(buf).substr(gt + 1, b - gt - 1));
                off = b + close.size();
            }
            if (off > (4u << 20)) {
                buf.erase(0, off);
                off = 0;
            }
            return true;
        }, err);
        if (!ok) {
            if (cancel) err = "annulé";
            sqlite3_close(db);
            fs::remove(tmp, ec);
            return false;
        }
        std::string plist;
        for (auto& p : platforms) plist += (plist.empty() ? "" : ", ") + p;
        time_t now = time(nullptr);
        tm lt{};
        localtime_s(&lt, &now);
        char date[32];
        strftime(date, sizeof(date), "%Y-%m-%d %H:%M", &lt);
        Q info(db, "INSERT OR REPLACE INTO info VALUES(?,?)");
        info.Bind(1, std::string("built")).Bind(2, std::string(date));
        info.Run();
        info.Bind(1, std::string("games")).Bind(2, std::to_string(nGames));
        info.Run();
        info.Bind(1, std::string("platforms")).Bind(2, plist);
        info.Run();
        info.Bind(1, std::string("videos")).Bind(2, std::to_string(nVid));
        info.Run();
        summary = std::format("{} jeux ({}), {} noms alternatifs, {} images, {} liens vidéo", nGames, plist, nAlt, nImg, nVid);
    }
    sqlite3_exec(db,
                 "COMMIT; CREATE INDEX names_norm ON names(norm); CREATE INDEX images_id ON images(id);"
                 "VACUUM;",
                 nullptr, nullptr, nullptr);
    sqlite3_close(db);
    if (nGames == 0) {
        err = "aucun jeu trouvé pour ces plateformes";
        fs::remove(tmp, ec);
        return false;
    }
    if (!MoveFileExW(tmp.c_str(), dbFile.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        err = "remplacement de l'index impossible (fichier utilisé ?)";
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

std::string LaunchBoxDb::Describe(const fs::path& dbFile) {
    std::error_code ec;
    if (!fs::exists(dbFile, ec)) return {};
    sqlite3* db = nullptr;
    std::string out;
    if (sqlite3_open_v2(util::PathU8(dbFile).c_str(), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK) {
        std::string built, games, plat, vids;
        {
            Q q(db, "SELECT key, value FROM info");
            while (q.Step()) {
                std::string k = q.Text(0);
                if (k == "built") built = q.Text(1);
                else if (k == "games") games = q.Text(1);
                else if (k == "platforms") plat = q.Text(1);
                else if (k == "videos") vids = q.Text(1);
            }
        }
        if (!games.empty())
            out = std::format("{} jeux ({}){}, index du {}", games, plat,
                              vids.empty() ? std::string() : ", " + vids + " liens vidéo", built);
    }
    sqlite3_close(db);
    return out;
}

bool LaunchBoxDb::HasVideoLinks(const fs::path& dbFile) {
    sqlite3* db = nullptr;
    bool has = false;
    if (sqlite3_open_v2(util::PathU8(dbFile).c_str(), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK) {
        Q q(db, "SELECT 1 FROM sqlite_master WHERE type='table' AND name='videos'");
        has = q.Step();
    }
    sqlite3_close(db);
    return has;
}

LaunchBoxDb::~LaunchBoxDb() { Close(); }

void LaunchBoxDb::Close() {
    if (db_) sqlite3_close(db_);
    db_ = nullptr;
}

bool LaunchBoxDb::Open(const fs::path& dbFile, std::string& err) {
    std::error_code ec;
    auto stamp = fs::last_write_time(dbFile, ec);
    if (ec) {
        Close();
        err = "Base LaunchBox absente : téléchargez-la dans Administration > Médias";
        return false;
    }
    if (db_ && file_ == dbFile && stamp == stamp_) return true;
    Close();
    if (sqlite3_open_v2(util::PathU8(dbFile).c_str(), &db_, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        err = "Base LaunchBox illisible";
        Close();
        return false;
    }
    file_ = dbFile;
    stamp_ = stamp;
    {
        Q q(db_, "SELECT 1 FROM sqlite_master WHERE type='table' AND name='videos'");
        hasVideos_ = q.Step();
    }
    return true;
}

std::vector<LbMatch> LaunchBoxDb::Search(const std::string& query, size_t max) {
    std::vector<LbMatch> out;
    if (!db_) return out;
    std::unordered_set<std::string> seen;
    auto load = [&](const std::string& id, bool exact, const std::string& alt) {
        if (out.size() >= max || !seen.insert(id).second) return;
        Q q(db_, "SELECT id, name, platform, date, publisher, developer, genres, overview FROM games WHERE id=?");
        q.Bind(1, id);
        if (!q.Step()) return;
        LbMatch m;
        m.game = {q.Text(0), q.Text(1), q.Text(2), q.Text(3), q.Text(4), q.Text(5), q.Text(6), q.Text(7)};
        m.exact = exact;
        if (exact && alt != m.game.name) m.matchedAlt = alt;
        out.push_back(std::move(m));
    };
    std::string norm = Scraper::Normalize(query);
    if (norm.empty()) return out;
    {
        Q q(db_, "SELECT id, name, alt FROM names WHERE norm=? ORDER BY alt");
        q.Bind(1, norm);
        while (q.Step()) load(q.Text(0), true, q.Text(1));
    }
    // Mots entiers ; si rien, on se contente des deux premiers mots (« GTI Club 3 » → les « GTI Club »).
    std::vector<std::string> tok = Tokens(query);
    for (int pass = 0; pass < 2 && out.size() < max; ++pass) {
        size_t n = pass == 0 ? tok.size() : std::min<size_t>(2, tok.size());
        if (n == 0 || (pass == 1 && (n == tok.size() || out.size() > 0))) break;
        std::string sql = "SELECT DISTINCT id FROM names WHERE 1";
        for (size_t i = 0; i < n; ++i) sql += " AND words LIKE ?";
        sql += " LIMIT 60";
        Q q(db_, sql.c_str());
        for (size_t i = 0; i < n; ++i) q.Bind((int)i + 1, "% " + tok[i] + " %");
        while (q.Step()) load(q.Text(0), false, {});
    }
    return out;
}

bool LaunchBoxDb::Get(const std::string& id, LbGame& g, std::vector<LbImage>& images) {
    if (!db_) return false;
    Q q(db_, "SELECT id, name, platform, date, publisher, developer, genres, overview FROM games WHERE id=?");
    q.Bind(1, id);
    if (!q.Step()) return false;
    g = {q.Text(0), q.Text(1), q.Text(2), q.Text(3), q.Text(4), q.Text(5), q.Text(6), q.Text(7)};
    Q qi(db_, "SELECT type, region, file FROM images WHERE id=?");
    qi.Bind(1, id);
    while (qi.Step()) images.push_back({qi.Text(0), qi.Text(1), qi.Text(2)});
    if (hasVideos_) {
        Q qv(db_, "SELECT url FROM videos WHERE id=?");
        qv.Bind(1, id);
        if (qv.Step()) g.video = qv.Text(0);
    }
    return true;
}
