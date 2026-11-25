#include "core/ConfigShare.h"

#include <windows.h>

#include <algorithm>
#include <ctime>
#include <format>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <set>

#include "core/Log.h"
#include "core/Settings.h"
#include "library/Library.h"
#include "media/Zip.h"

using json = nlohmann::json;

namespace share {
namespace {

constexpr int kFormat = 1;

// « Course/Drift » (chemin de catégorie lisible sur une autre borne, où les numéros diffèrent)
std::string CatPath(const Library& lib, int64_t id) {
    std::vector<std::string> parts;
    for (int guard = 0; id && guard < 32; ++guard) {
        const Category* c = lib.FindCategory(id);
        if (!c) break;
        parts.push_back(c->name);
        id = c->parentId;
    }
    std::string s;
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) s += (s.empty() ? "" : "/") + *it;
    return s;
}

std::string Lower(const std::string& s) { return util::ToLower(util::Trim(s)); }

// Remplacement du début d'un chemin (insensible à la casse, séparateurs \ ou /).
std::string Remap(const std::string& path, const std::string& oldRoot, const std::string& newRoot) {
    if (oldRoot.empty() || path.size() < oldRoot.size()) return path;
    std::string a = Lower(path.substr(0, oldRoot.size())), b = Lower(oldRoot);
    std::replace(a.begin(), a.end(), '/', '\\');
    std::replace(b.begin(), b.end(), '/', '\\');
    if (a != b) return path;
    if (path.size() > oldRoot.size() && path[oldRoot.size()] != '\\' && path[oldRoot.size()] != '/' &&
        oldRoot.back() != '\\' && oldRoot.back() != '/')
        return path;  // « D:\Jeux » ne doit pas correspondre à « D:\Jeux2\… »
    return newRoot + path.substr(oldRoot.size());
}

// Dossier commun de plusieurs chemins de fichiers (« D:\Jeux »), vide s'il n'y en a pas.
std::string CommonRoot(const std::vector<std::string>& paths) {
    std::string root;
    bool first = true;
    for (const auto& p : paths) {
        if (p.empty()) continue;
        std::string dir = p.substr(0, p.find_last_of("\\/"));
        if (first) {
            root = dir;
            first = false;
            continue;
        }
        size_t n = 0;
        while (n < root.size() && n < dir.size() && std::tolower((unsigned char)root[n]) == std::tolower((unsigned char)dir[n])) ++n;
        root = root.substr(0, n);
        size_t cut = root.find_last_of("\\/");
        if (n < dir.size() || n < root.size()) root = cut == std::string::npos ? "" : root.substr(0, cut);
    }
    while (!root.empty() && (root.back() == '\\' || root.back() == '/')) root.pop_back();
    if (root.size() <= 2) return {};  // « D: » seul : rien d'utile à proposer
    return root;
}

bool ReadEntry(const fs::path& zip, const std::string& name, std::string& out, std::string& err) {
    out.clear();
    return ZipStreamEntry(zip, name, [&](const char* d, size_t n) {
        out.append(d, n);
        return out.size() < (64u << 20);
    }, err);
}

bool ExtractEntry(const fs::path& zip, const std::string& name, const fs::path& dst, std::string& err) {
    std::error_code ec;
    fs::create_directories(dst.parent_path(), ec);
    fs::path tmp = dst;
    tmp += L".part";
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) {
        err = "écriture impossible : " + util::PathU8(dst);
        return false;
    }
    bool ok = ZipStreamEntry(zip, name, [&](const char* d, size_t n) {
        f.write(d, (std::streamsize)n);
        return (bool)f;
    }, err);
    f.close();
    if (ok) fs::rename(tmp, dst, ec);
    if (!ok || ec) {
        fs::remove(tmp, ec);
        if (err.empty()) err = "écriture impossible : " + util::PathU8(dst);
        return false;
    }
    return true;
}

// Entrée d'archive sûre : pas de « .. », pas de chemin absolu.
bool SafeName(const std::string& n) {
    return !n.empty() && n.find("..") == std::string::npos && n[0] != '/' && n[0] != '\\' && n.find(':') == std::string::npos;
}

std::string Now() {
    time_t t = time(nullptr);
    tm lt{};
    localtime_s(&lt, &t);
    char b[32];
    strftime(b, sizeof(b), "%Y-%m-%d %H:%M", &lt);
    return b;
}

json GameToJson(const Library& lib, const Game& g) {
    json cats = json::array();
    for (int64_t c : g.categories) cats.push_back(CatPath(lib, c));
    return {{"name", g.name},           {"launchPath", g.launchPath},     {"args", g.args},
            {"workDir", g.workDir},     {"runMode", (int)g.runMode},      {"launchDelayMs", g.launchDelayMs},
            {"preCmd", g.preCmd},       {"postCmd", g.postCmd},           {"watchProcess", g.watchProcess},
            {"altName", g.altName},     {"description", g.description},   {"publisher", g.publisher},
            {"genre", g.genre},         {"platform", g.platform},         {"year", g.year},
            {"enabled", g.enabled},     {"favorite", g.favorite},         {"categories", cats},
            {"videoUrl", g.videoUrl},   {"videoStart", g.videoStart},     {"videoDuration", g.videoDuration}};
}

Game GameFromJson(const json& j) {
    Game g;
    g.name = j.value("name", "");
    g.launchPath = j.value("launchPath", "");
    g.args = j.value("args", "");
    g.workDir = j.value("workDir", "");
    g.runMode = (RunMode)std::clamp(j.value("runMode", 0), 0, 3);
    g.launchDelayMs = j.value("launchDelayMs", 0);
    g.preCmd = j.value("preCmd", "");
    g.postCmd = j.value("postCmd", "");
    g.watchProcess = j.value("watchProcess", "");
    g.altName = j.value("altName", "");
    g.description = j.value("description", "");
    g.publisher = j.value("publisher", "");
    g.genre = j.value("genre", "");
    g.platform = j.value("platform", "");
    g.year = j.value("year", 0);
    g.enabled = j.value("enabled", true);
    g.favorite = j.value("favorite", false);
    g.videoUrl = j.value("videoUrl", "");
    g.videoStart = j.value("videoStart", 0);
    g.videoDuration = j.value("videoDuration", -1);
    return g;
}

const Game* FindExisting(const Library& lib, const Game& g) {
    for (const Game& e : lib.Games())
        if (Lower(e.name) == Lower(g.name) || (!g.launchPath.empty() && Lower(e.launchPath) == Lower(g.launchPath)))
            return &e;
    return nullptr;
}

bool LoadConfig(const fs::path& zip, json& cfg, std::string& err) {
    std::string text;
    if (!ReadEntry(zip, "config.json", text, err)) {
        err = "ce fichier n'est pas une configuration Racecab Frontend (config.json absent)";
        return false;
    }
    try {
        cfg = json::parse(text);
    } catch (const std::exception& e) {
        err = std::string("config.json illisible : ") + e.what();
        return false;
    }
    if (cfg.value("format", 0) < 1 || !cfg.contains("games")) {
        err = "format de configuration inconnu";
        return false;
    }
    if (cfg.value("format", 0) > kFormat) {
        err = "configuration créée par une version plus récente du frontend : mettez-le à jour";
        return false;
    }
    return true;
}

}  // namespace

std::string RemapIn(const std::string& text, const std::string& oldRoot, const std::string& newRoot) {
    std::string root = oldRoot;
    while (!root.empty() && (root.back() == '\\' || root.back() == '/')) root.pop_back();
    if (root.empty()) return text;
    const std::string low = util::ToLower(root);
    std::string out;
    for (size_t i = 0; i < text.size();) {
        const size_t e = i + root.size();
        if (e <= text.size() && util::ToLower(text.substr(i, root.size())) == low &&
            (e == text.size() || text[e] == '\\' || text[e] == '/' || text[e] == '"')) {
            out += newRoot;
            i = e;
        } else {
            out += text[i++];
        }
    }
    return out;
}

// ---------------------------------------------------------------- Export

bool Export(const Library& lib, const Paths& paths, const ExportOptions& opt, const fs::path& outZip,
            std::string& summary, std::string& err) {
    fs::path part = outZip;
    part += L".part";
    ZipWriter zw;
    if (!zw.Open(part, err)) return false;
    std::error_code ec;
    json games = json::array();
    int nMedia = 0, nLinks = 0;
    int index = 0;
    for (const Game& g : lib.Games()) {
        json jg = GameToJson(lib, g);
        if (!g.videoUrl.empty()) ++nLinks;
        if (opt.images || opt.videos) {
            // Médias : dossier media\<id> (convention) + fichiers indiqués explicitement dans la fiche.
            std::map<std::string, fs::path> files;  // nom dans l'archive -> fichier
            const std::string base = std::format("media/{}/", index);
            auto isVideo = [](const fs::path& p) {
                std::wstring e = p.extension().wstring();
                std::transform(e.begin(), e.end(), e.begin(), ::towlower);
                return e == L".mp4" || e == L".mkv" || e == L".webm" || e == L".avi";
            };
            for (auto& e : fs::directory_iterator(lib.MediaDirFor(g.id), ec)) {
                if (!e.is_regular_file(ec)) continue;
                std::wstring ext = e.path().extension().wstring();
                if (ext == L".part" || ext == L".tmp") continue;
                if (isVideo(e.path()) ? !opt.videos : !opt.images) continue;
                files[base + util::PathU8(e.path().filename())] = e.path();
            }
            auto explicitFile = [&](const std::string& p, const char* stem) {
                if (p.empty()) return;
                fs::path src = util::U8Path(p);
                if (!fs::is_regular_file(src, ec) || (isVideo(src) ? !opt.videos : !opt.images)) return;
                files[base + stem + util::PathU8(src.extension())] = src;
            };
            explicitFile(g.logoPath, "logo");
            explicitFile(g.fanartPath, "fanart");
            explicitFile(g.videoPath, "video");
            json media = json::array();
            for (auto& [name, src] : files) {
                if (!zw.AddFile(name, src, err)) {
                    zw.Close(err);
                    fs::remove(part, ec);
                    return false;
                }
                media.push_back(name);
                ++nMedia;
            }
            if (!media.empty()) jg["media"] = media;
        }
        games.push_back(jg);
        ++index;
    }
    json cats = json::array();
    for (const Category& c : lib.Categories()) cats.push_back(CatPath(lib, c.id));

    int nProfiles = 0;
    if (opt.profiles) {
        // Profils : catégories masquées traduites en chemins (les numéros diffèrent d'une borne à l'autre).
        for (auto& e : fs::directory_iterator(paths.ProfilesDir(), ec)) {
            if (e.path().extension() != L".json") continue;
            try {
                std::ifstream f(e.path(), std::ios::binary);
                json p = json::parse(f, nullptr, true, true);
                json hidden = json::array();
                if (p.contains("hiddenCategories"))
                    for (auto& id : p["hiddenCategories"])
                        if (id.is_number_integer()) hidden.push_back(CatPath(lib, id.get<int64_t>()));
                p["hiddenCategoryPaths"] = hidden;
                fs::path tmp = fs::temp_directory_path() / (L"racecab-share-" + e.path().filename().wstring());
                {
                    std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
                    o << p.dump(2);
                }
                bool ok = zw.AddFile("profiles/" + util::PathU8(e.path().filename()), tmp, err);
                fs::remove(tmp, ec);
                if (!ok) {
                    zw.Close(err);
                    fs::remove(part, ec);
                    return false;
                }
                ++nProfiles;
            } catch (const std::exception& ex) {
                logx::Warn("Export : profil {} ignoré ({})", util::PathU8(e.path().filename()), ex.what());
            }
        }
        for (auto& e : fs::recursive_directory_iterator(paths.ThemesDir(), ec)) {
            if (!e.is_regular_file(ec)) continue;
            std::string rel = util::PathU8(fs::relative(e.path(), paths.ThemesDir(), ec));
            std::replace(rel.begin(), rel.end(), '\\', '/');
            if (!zw.AddFile("themes/" + rel, e.path(), err)) {
                zw.Close(err);
                fs::remove(part, ec);
                return false;
            }
        }
    }

    json cfg = {{"format", kFormat}, {"app", "Racecab Frontend"}, {"exported", Now()}, {"categories", cats}, {"games", games}};
    fs::path tmp = fs::temp_directory_path() / L"racecab-share-config.json";
    {
        std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
        o << cfg.dump(2);
    }
    bool ok = zw.AddFile("config.json", tmp, err) && zw.Close(err);
    fs::remove(tmp, ec);
    if (!ok) {
        fs::remove(part, ec);
        return false;
    }
    if (!MoveFileExW(part.c_str(), outZip.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        err = "écriture du fichier impossible";
        fs::remove(part, ec);
        return false;
    }
    summary = std::format("{} jeu(x), {} lien(s) YouTube, {} fichier(s) média, {} profil(s)", lib.Games().size(), nLinks,
                          nMedia, nProfiles);
    logx::Info("Configuration exportée dans {} : {}", util::PathU8(outZip), summary);
    return true;
}

// ---------------------------------------------------------------- Aperçu

bool ReadPreview(const Library& lib, const fs::path& zip, const std::string& oldRoot, const std::string& newRoot,
                 Preview& p, std::string& err) {
    p = {};
    json cfg;
    if (!LoadConfig(zip, cfg, err)) return false;
    p.exported = cfg.value("exported", "");
    std::vector<std::string> launch;
    std::error_code ec;
    for (auto& jg : cfg["games"]) {
        Game g = GameFromJson(jg);
        if (g.name.empty()) continue;
        ++p.games;
        launch.push_back(g.launchPath);
        if (!g.videoUrl.empty()) ++p.videoLinks;
        if (jg.contains("media")) p.mediaFiles += (int)jg["media"].size();
        g.launchPath = Remap(g.launchPath, oldRoot, newRoot);
        if (FindExisting(lib, g)) ++p.existing;
        else ++p.newGames;
        if (!g.launchPath.empty() && !fs::exists(util::U8Path(g.launchPath), ec)) p.missingPaths.push_back(g.name);
    }
    p.commonRoot = CommonRoot(launch);
    std::vector<ZipEntryInfo> entries;
    if (ZipList(zip, entries, err))
        for (auto& e : entries)
            if (e.name.rfind("profiles/", 0) == 0 && e.name.size() > 14) ++p.profiles;
    err.clear();
    return true;
}

// ---------------------------------------------------------------- Import

bool Import(Library& lib, const Paths& paths, const fs::path& zip, const ImportOptions& opt, ImportResult& r,
            std::string& err) {
    r = {};
    json cfg;
    if (!LoadConfig(zip, cfg, err)) return false;
    std::error_code ec;
    for (auto& c : cfg.value("categories", json::array()))
        if (c.is_string() && !c.get<std::string>().empty()) lib.EnsureCategoryPath(c.get<std::string>());

    for (auto& jg : cfg["games"]) {
        Game g = GameFromJson(jg);
        if (g.name.empty()) continue;
        g.launchPath = Remap(g.launchPath, opt.oldRoot, opt.newRoot);
        g.workDir = Remap(g.workDir, opt.oldRoot, opt.newRoot);
        g.args = RemapIn(g.args, opt.oldRoot, opt.newRoot);
        for (auto& c : jg.value("categories", json::array()))
            if (c.is_string() && !c.get<std::string>().empty()) g.categories.push_back(lib.EnsureCategoryPath(c.get<std::string>()));

        int64_t id = 0;
        if (const Game* e = FindExisting(lib, g)) {
            // Déjà présent : on complète sans rien écraser.
            Game u = *e;
            auto fill = [](std::string& dst, const std::string& src) {
                if (dst.empty()) dst = src;
            };
            fill(u.altName, g.altName);
            fill(u.description, g.description);
            fill(u.publisher, g.publisher);
            fill(u.genre, g.genre);
            fill(u.platform, g.platform);
            if (!u.year) u.year = g.year;
            if (u.videoUrl.empty() && !g.videoUrl.empty()) {
                u.videoUrl = g.videoUrl;
                u.videoStart = g.videoStart;
                u.videoDuration = g.videoDuration;
            }
            for (int64_t c : g.categories)
                if (std::find(u.categories.begin(), u.categories.end(), c) == u.categories.end()) u.categories.push_back(c);
            lib.UpdateGame(u);
            id = u.id;
            ++r.updated;
        } else {
            id = lib.AddGame(g);
            if (!id) continue;
            ++r.added;
        }
        // Médias contenus dans le fichier : copiés seulement s'il n'y a rien de ce type ici.
        if (opt.media && jg.contains("media")) {
            fs::path dir = lib.MediaDirFor(id);
            for (auto& m : jg["media"]) {
                std::string name = m.is_string() ? m.get<std::string>() : "";
                if (!SafeName(name) || name.rfind("media/", 0) != 0) continue;
                fs::path file = util::U8Path(name.substr(name.find_last_of('/') + 1));
                std::wstring stem = file.stem().wstring();
                bool have = false;
                for (auto& e : fs::directory_iterator(dir, ec))
                    if (e.path().stem().wstring() == stem) have = true;
                if (have) continue;
                std::string werr;
                if (ExtractEntry(zip, name, dir / file, werr)) ++r.mediaFiles;
                else logx::Warn("Import : {} ({})", name, werr);
            }
        }
        const Game* now = lib.FindGame(id);
        if (now && !now->videoUrl.empty() && lib.VideoFor(*now).empty()) r.needVideo.push_back(id);
    }

    if (opt.profiles) {
        std::vector<ZipEntryInfo> entries;
        std::string lerr;
        ZipList(zip, entries, lerr);
        std::set<std::string> localThemes;  // thèmes déjà installés ici : jamais écrasés
        for (auto& d : fs::directory_iterator(paths.ThemesDir(), ec))
            if (d.is_directory(ec)) localThemes.insert(Lower(util::PathU8(d.path().filename())));
        for (auto& e : entries) {
            if (!SafeName(e.name)) continue;
            if (e.name.rfind("profiles/", 0) == 0 && e.name.size() > 14 && e.name.ends_with(".json")) {
                std::string text, perr;
                if (!ReadEntry(zip, e.name, text, perr)) continue;
                try {
                    json p = json::parse(text);
                    json ids = json::array();
                    for (auto& path : p.value("hiddenCategoryPaths", json::array()))
                        if (path.is_string() && !path.get<std::string>().empty())
                            ids.push_back(lib.EnsureCategoryPath(path.get<std::string>()));
                    p["hiddenCategories"] = ids;
                    p.erase("hiddenCategoryPaths");
                    std::string name = p.value("name", e.name.substr(9, e.name.size() - 14));
                    std::error_code dec;
                    fs::create_directories(paths.ProfilesDir(), dec);
                    std::ofstream o(paths.ProfileFile(name), std::ios::binary | std::ios::trunc);
                    o << p.dump(2);
                    ++r.profiles;
                } catch (const std::exception& ex) {
                    logx::Warn("Import : profil {} ignoré ({})", e.name, ex.what());
                }
            } else if (e.name.rfind("themes/", 0) == 0 && e.name.size() > 7) {
                // Thème absent ici : copié en entier (un thème existant n'est jamais écrasé).
                std::string rest = e.name.substr(7);
                std::string theme = rest.substr(0, rest.find('/'));
                if (theme.empty() || theme == rest) continue;
                if (localThemes.count(Lower(theme))) continue;
                fs::path dst = paths.ThemesDir() / util::U8Path(rest);
                std::string terr;
                ExtractEntry(zip, e.name, dst, terr);
            }
        }
    }
    r.summary = std::format("{} jeu(x) ajouté(s), {} complété(s), {} fichier(s) média, {} profil(s), {} vidéo(s) à télécharger",
                            r.added, r.updated, r.mediaFiles, r.profiles, r.needVideo.size());
    logx::Info("Configuration importée depuis {} : {}", util::PathU8(zip), r.summary);
    return true;
}

bool SyncFrom(Library& lib, const Paths& paths, const fs::path& baseData, const std::string& oldRoot,
              const std::string& newRoot, ImportResult& r, std::string& err) {
    Paths basePaths;
    basePaths.dataDir = baseData;
    std::error_code ec;
    if (!fs::exists(basePaths.DbFile(), ec)) {
        err = "bibliothèque de la base introuvable : " + util::PathU8(basePaths.DbFile());
        return false;
    }
    Library base;
    if (!base.Open(basePaths.DbFile(), basePaths.MediaDir())) {
        err = "bibliothèque de la base illisible : " + util::PathU8(basePaths.DbFile());
        return false;
    }
    // Passage par le format d'échange : médias (vidéos comprises) et catégories en chemins.
    const fs::path zip = paths.dataDir / L"sync-base.zip";
    ExportOptions eo;
    eo.images = true;
    eo.videos = true;
    eo.profiles = false;
    std::string summary;
    if (!Export(base, basePaths, eo, zip, summary, err)) return false;
    ImportOptions io;
    io.oldRoot = oldRoot;
    io.newRoot = newRoot;
    io.profiles = false;  // les profils de la borne (catégories masquées, thème, vidéo) ne sont pas touchés
    io.media = true;
    const bool ok = Import(lib, paths, zip, io, r, err);
    fs::remove(zip, ec);
    return ok;
}

}  // namespace share
