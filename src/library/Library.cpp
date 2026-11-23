#include "library/Library.h"

#include <windows.h>

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>

#include "core/Log.h"

namespace {

int CompareNames(const std::string& a, const std::string& b) {
    std::wstring wa = util::Widen(a), wb = util::Widen(b);
    int r = CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE | SORT_DIGITSASNUMBERS, wa.c_str(),
                            (int)wa.size(), wb.c_str(), (int)wb.size(), nullptr, nullptr, 0);
    return r - 2;  // CSTR_LESS_THAN=1, EQUAL=2, GREATER=3
}

std::string NormalizePath(const std::string& p) {
    std::error_code ec;
    fs::path abs = fs::absolute(util::U8Path(p), ec);
    return util::ToLower(util::PathU8(abs.lexically_normal()));
}

}  // namespace

bool Library::Open(const fs::path& dbFile, const fs::path& mediaDir) {
    mediaDir_ = mediaDir;
    if (!db_.Open(dbFile)) return false;
    Reload();
    logx::Info("Bibliothèque : {} jeux, {} catégories", games_.size(), cats_.size());
    return true;
}

void Library::Reload() {
    games_ = db_.LoadGames();
    cats_ = db_.LoadCategories();
    Reindex();
}

void Library::Reindex() {
    gameIdx_.clear();
    catIdx_.clear();
    for (size_t i = 0; i < games_.size(); ++i) gameIdx_[games_[i].id] = i;
    for (size_t i = 0; i < cats_.size(); ++i) catIdx_[cats_[i].id] = i;
}

const Game* Library::FindGame(int64_t id) const {
    auto it = gameIdx_.find(id);
    return it == gameIdx_.end() ? nullptr : &games_[it->second];
}

const Category* Library::FindCategory(int64_t id) const {
    auto it = catIdx_.find(id);
    return it == catIdx_.end() ? nullptr : &cats_[it->second];
}

bool Library::NodeExists(int64_t node) const {
    if (node == kFavorites || node == kRecent) return showAuto_;
    return node == kRoot || node == kAll || (FindCategory(node) && !CategoryHidden(node));
}

void Library::SetProfileFilter(std::vector<int64_t> hidden, bool filterGames, bool showAuto) {
    hidden_ = std::move(hidden);
    filterGames_ = filterGames;
    showAuto_ = showAuto;
}

bool Library::CategoryHidden(int64_t id) const {
    int guard = 0;
    while (id > 0 && guard++ < 64) {
        if (std::find(hidden_.begin(), hidden_.end(), id) != hidden_.end()) return true;
        id = ParentOf(id);
    }
    return false;
}

bool Library::GameVisible(const Game& g) const {
    if (!g.enabled) return false;
    if (hidden_.empty() || !filterGames_) return true;
    for (int64_t c : g.categories)
        if (FindCategory(c) && !CategoryHidden(c)) return true;
    return false;  // aucune catégorie visible dans ce profil
}

std::vector<int64_t> Library::AttractGames(const std::vector<int64_t>& cats) const {
    std::vector<int64_t> out;
    for (const Game& g : games_) {
        if (!GameVisible(g)) continue;
        bool in = cats.empty();
        for (int64_t c : g.categories)
            for (int64_t want : cats) in |= IsDescendant(c, want);
        if (in) out.push_back(g.id);
    }
    return out;
}

std::string Library::NodeName(int64_t node) const {
    switch (node) {
        case kRoot: return "Catégories";
        case kAll: return "Tous les jeux";
        case kFavorites: return "Favoris";
        case kRecent: return "Récemment joués";
    }
    const Category* c = FindCategory(node);
    return c ? c->name : "?";
}

std::string Library::NodePath(int64_t node) const {
    std::vector<std::string> parts;
    int guard = 0;
    while (node != kRoot && guard++ < 64) {
        parts.push_back(NodeName(node));
        node = ParentOf(node);
    }
    std::string out;
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
        if (!out.empty()) out += "  \xE2\x80\xBA  ";  // ›
        out += *it;
    }
    return out.empty() ? NodeName(kRoot) : out;
}

int64_t Library::ParentOf(int64_t node) const {
    if (node <= 0) return kRoot;
    const Category* c = FindCategory(node);
    return c ? c->parentId : kRoot;
}

bool Library::IsDescendant(int64_t node, int64_t ancestor) const {
    int guard = 0;
    while (node > 0 && guard++ < 64) {
        if (node == ancestor) return true;
        node = ParentOf(node);
    }
    return false;
}

std::vector<int64_t> Library::ChildCategories(int64_t node, bool all) const {
    std::vector<int64_t> out;
    if (node < 0) return out;
    if (node == kRoot) {
        out = {kAll};
        if (all || showAuto_) {
            out.push_back(kFavorites);
            out.push_back(kRecent);
        }
    }
    std::vector<const Category*> kids;
    for (const Category& c : cats_)
        if (c.parentId == node && (all || !CategoryHidden(c.id))) kids.push_back(&c);
    std::stable_sort(kids.begin(), kids.end(), [](const Category* a, const Category* b) {
        if (a->sort != b->sort) return a->sort < b->sort;
        return CompareNames(a->name, b->name) < 0;
    });
    for (const Category* c : kids) out.push_back(c->id);
    return out;
}

std::vector<int64_t> Library::Siblings(int64_t node) const {
    if (node == kRoot) return {kRoot};
    return ChildCategories(ParentOf(node));
}

std::vector<WheelItem> Library::Items(int64_t node) const {
    std::vector<WheelItem> out;
    for (int64_t c : ChildCategories(node)) out.push_back({ItemKind::Category, c});
    if (node == kRoot) return out;

    std::vector<const Game*> list;
    for (const Game& g : games_) {
        if (!GameVisible(g)) continue;
        bool in = false;
        if (node == kAll) in = true;
        else if (node == kFavorites) in = g.favorite;
        else if (node == kRecent) in = g.lastPlayed > 0;
        else in = std::find(g.categories.begin(), g.categories.end(), node) != g.categories.end();
        if (in) list.push_back(&g);
    }
    if (node == kRecent) {
        std::sort(list.begin(), list.end(), [](auto a, auto b) { return a->lastPlayed > b->lastPlayed; });
        if (list.size() > (size_t)kRecentLimit) list.resize(kRecentLimit);
    } else {
        // Tri choisi dans le profil ; le nom départage toujours (ordre stable et prévisible).
        const std::string& s = sort_;
        std::sort(list.begin(), list.end(), [&s](auto a, auto b) {
            if (s == "year" && a->year != b->year) return (a->year ? a->year : 9999) < (b->year ? b->year : 9999);
            if (s == "played" && a->playSeconds != b->playSeconds) return a->playSeconds > b->playSeconds;
            if (s == "count" && a->playCount != b->playCount) return a->playCount > b->playCount;
            return CompareNames(a->name, b->name) < 0;
        });
    }
    for (const Game* g : list) out.push_back({ItemKind::Game, g->id});
    return out;
}

int Library::CountGames(int64_t node) const {
    int n = 0;
    for (const WheelItem& it : Items(node))
        if (it.kind == ItemKind::Game) ++n;
    return n;
}

std::string Library::MediaCandidate(int64_t id, std::initializer_list<const wchar_t*> names) const {
    fs::path dir = mediaDir_ / std::to_wstring(id);
    std::error_code ec;
    for (const wchar_t* n : names) {
        fs::path p = dir / n;
        if (fs::is_regular_file(p, ec)) return util::PathU8(p);
    }
    return {};
}

std::string Library::LogoFor(const Game& g) const {
    if (!g.logoPath.empty()) return g.logoPath;
    return MediaCandidate(g.id, {L"logo.png", L"logo.webp", L"logo.jpg"});
}

std::string Library::FanartFor(const Game& g) const {
    if (!g.fanartPath.empty()) return g.fanartPath;
    return MediaCandidate(g.id, {L"fanart.jpg", L"fanart.png", L"screenshot.jpg", L"screenshot.png"});
}

std::string Library::VideoFor(const Game& g) const {
    if (!g.videoPath.empty()) return g.videoPath;
    return MediaCandidate(g.id, {L"video.mp4", L"video.mkv", L"video.webm", L"video.avi"});
}

int64_t Library::AddGame(Game g) {
    g.id = db_.InsertGame(g);
    if (!g.id) return 0;
    g.addedAt = util::NowUnix();
    games_.push_back(std::move(g));
    Reindex();
    logx::Info("Jeu ajouté : {} (id {})", games_.back().name, games_.back().id);
    return games_.back().id;
}

bool Library::UpdateGame(const Game& g) {
    auto it = gameIdx_.find(g.id);
    if (it == gameIdx_.end() || !db_.UpdateGame(g)) return false;
    Game& dst = games_[it->second];
    // Les statistiques ne sont pas éditables : on les conserve.
    Game copy = g;
    copy.playCount = dst.playCount;
    copy.playSeconds = dst.playSeconds;
    copy.lastPlayed = dst.lastPlayed;
    copy.addedAt = dst.addedAt;
    dst = std::move(copy);
    return true;
}

bool Library::RemoveGame(int64_t id) {
    const Game* g = FindGame(id);
    if (!g || !db_.DeleteGame(id)) return false;
    logx::Info("Jeu retiré de la bibliothèque (fichiers conservés) : {}", g->name);
    games_.erase(games_.begin() + gameIdx_[id]);
    Reindex();
    return true;
}

bool Library::ToggleFavorite(int64_t id) {
    auto it = gameIdx_.find(id);
    if (it == gameIdx_.end()) return false;
    Game& g = games_[it->second];
    if (!db_.SetFavorite(id, !g.favorite)) return false;
    g.favorite = !g.favorite;
    return true;
}

bool Library::ClearStats() {
    if (!db_.ClearHistory()) return false;
    for (Game& g : games_) {
        g.playCount = 0;
        g.playSeconds = 0;
        g.lastPlayed = 0;
    }
    return true;
}

void Library::RecordPlay(int64_t id, int64_t seconds, const std::string& profile) {
    auto it = gameIdx_.find(id);
    if (it == gameIdx_.end()) return;
    int64_t now = util::NowUnix();
    if (!db_.RecordPlay(id, seconds, now, profile)) return;
    Game& g = games_[it->second];
    g.playCount++;
    g.playSeconds += seconds;
    g.lastPlayed = now;
}

std::optional<int64_t> Library::FindDuplicate(const std::string& launchPath, const std::string& args,
                                              int64_t exceptId) const {
    std::string a = NormalizePath(launchPath), b = util::Trim(args);
    for (const Game& g : games_)
        if (g.id != exceptId && NormalizePath(g.launchPath) == a && util::Trim(g.args) == b) return g.id;
    return std::nullopt;
}

int64_t Library::AddCategory(const std::string& name, int64_t parent) {
    if (parent < 0) parent = kRoot;
    int sort = 0;
    for (const Category& c : cats_)
        if (c.parentId == parent) sort = std::max(sort, c.sort + 1);
    int64_t id = db_.InsertCategory(name, parent, sort);
    if (!id) return 0;
    cats_.push_back({id, parent, name, sort});
    Reindex();
    return id;
}

bool Library::RenameCategory(int64_t id, const std::string& name) {
    auto it = catIdx_.find(id);
    if (it == catIdx_.end()) return false;
    Category c = cats_[it->second];
    c.name = name;
    if (!db_.UpdateCategory(c)) return false;
    cats_[it->second] = c;
    return true;
}

bool Library::MoveCategory(int64_t id, int64_t newParent) {
    auto it = catIdx_.find(id);
    if (it == catIdx_.end() || newParent < 0) return false;
    if (newParent == id || IsDescendant(newParent, id)) return false;  // pas de cycle
    Category c = cats_[it->second];
    c.parentId = newParent;
    if (!db_.UpdateCategory(c)) return false;
    cats_[it->second] = c;
    return true;
}

bool Library::ShiftCategory(int64_t id, int delta) {
    const Category* c = FindCategory(id);
    if (!c) return false;
    std::vector<int64_t> sib = ChildCategories(c->parentId, true);
    sib.erase(std::remove_if(sib.begin(), sib.end(), [](int64_t x) { return x <= 0; }), sib.end());
    auto pos = std::find(sib.begin(), sib.end(), id) - sib.begin();
    int64_t np = pos + delta;
    if (np < 0 || np >= (int64_t)sib.size()) return false;
    std::swap(sib[pos], sib[np]);
    db_.Begin();
    for (size_t i = 0; i < sib.size(); ++i) {
        Category& cc = cats_[catIdx_[sib[i]]];
        cc.sort = (int)i;
        db_.UpdateCategory(cc);
    }
    db_.Commit();
    return true;
}

bool Library::DeleteCategory(int64_t id) {
    if (!FindCategory(id) || !db_.DeleteCategory(id)) return false;
    // Sous-catégories supprimées en cascade par SQLite ; on recharge pour rester cohérent.
    Reload();
    return true;
}

int64_t Library::EnsureCategoryPath(const std::string& path) {
    int64_t parent = kRoot;
    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find('/', start);
        if (end == std::string::npos) end = path.size();
        std::string part = util::Trim(path.substr(start, end - start));
        start = end + 1;
        if (part.empty()) continue;
        int64_t found = 0;
        for (const Category& c : cats_)
            if (c.parentId == parent && CompareNames(c.name, part) == 0) found = c.id;
        parent = found ? found : AddCategory(part, parent);
        if (!parent) return 0;
    }
    return parent;
}

bool Library::ImportList(const fs::path& file, int& added, int& duplicates, std::string& error) {
    added = duplicates = 0;
    std::ifstream f(file, std::ios::binary);
    if (!f) {
        error = "Fichier illisible";
        return false;
    }
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(f, nullptr, true, true);
    } catch (const std::exception& e) {
        error = std::string("JSON invalide : ") + e.what();
        return false;
    }
    const nlohmann::json& list = j.is_array() ? j : j.value("games", nlohmann::json::array());
    fs::path base = file.parent_path();
    auto str = [](const nlohmann::json& o, const char* k) { return o.contains(k) && o[k].is_string() ? o[k].get<std::string>() : std::string(); };
    auto resolve = [&](std::string p) {
        if (p.empty()) return p;
        fs::path pp = util::U8Path(p);
        if (pp.is_relative()) pp = base / pp;
        return util::PathU8(pp.lexically_normal());
    };

    db_.Begin();
    for (const auto& o : list) {
        Game g;
        g.name = str(o, "name");
        g.launchPath = resolve(str(o, "launch"));
        if (g.name.empty() || g.launchPath.empty()) continue;
        g.args = str(o, "args");
        g.workDir = resolve(str(o, "workdir"));
        g.altName = str(o, "altName");
        g.description = str(o, "description");
        g.publisher = str(o, "publisher");
        g.genre = str(o, "genre");
        g.platform = str(o, "platform");
        g.year = o.value("year", 0);
        g.logoPath = resolve(str(o, "logo"));
        g.fanartPath = resolve(str(o, "fanart"));
        g.videoPath = resolve(str(o, "video"));
        g.preCmd = str(o, "preCmd");
        g.postCmd = str(o, "postCmd");
        g.watchProcess = str(o, "watchProcess");
        g.favorite = o.value("favorite", false);
        std::string rm = util::ToLower(str(o, "runMode"));
        if (rm == "normal") g.runMode = RunMode::Normal;
        else if (rm == "minimized") g.runMode = RunMode::Minimized;
        else if (rm == "hidden") g.runMode = RunMode::Hidden;
        if (FindDuplicate(g.launchPath, g.args, 0)) {
            ++duplicates;
            continue;
        }
        if (o.contains("categories"))
            for (const auto& c : o["categories"])
                if (c.is_string())
                    if (int64_t id = EnsureCategoryPath(c.get<std::string>())) g.categories.push_back(id);
        if (AddGame(std::move(g))) ++added;
    }
    db_.Commit();
    logx::Info("Import {} : {} ajoutés, {} doublons ignorés", util::PathU8(file), added, duplicates);
    return true;
}
