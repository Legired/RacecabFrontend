// Fournisseurs de médias et métadonnées.
//
// SteamGridDB (https://www.steamgriddb.com/api/v2) : clé API personnelle (en-tête « Authorization: Bearer »).
//   Points d'accès vérifiés dans le client officiel node-steamgriddb : /search/autocomplete/{terme},
//   /logos/game/{id}, /heroes/game/{id} ; réponse {success, data:[{id, url, score, style, width, height...}]}.
//   Sans clé : HTTP 401 {"success":false,"errors":["Authentication Required"]} (vérifié).
//
// ScreenScraper (https://api.screenscraper.fr/api2) : identifiants développeur obligatoires (devid/devpassword,
//   à demander sur leur forum), compte utilisateur facultatif. Données sous licence CC BY-NC-SA.
//   jeuRecherche.php (recherche par nom, 30 résultats max) et jeuInfos.php?gameid= (détail + médias).
//   Format JSON : tableaux noms[{region,text}], synopsis[{langue,text}], medias[{type,region,url,format}]
//   (vérifié dans le code de Skyscraper, faute d'identifiants pour un appel réel).
//   Sans identifiants : texte « Erreur de login : Vérifier vos identifiants développeur ! » (vérifié).
#include <windows.h>

#include <algorithm>
#include <ctime>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>

#include "core/Log.h"
#include "media/LaunchBox.h"
#include "media/Scraper.h"
#include "media/YtVideo.h"
#include "net/Http.h"

using nlohmann::json;

namespace {




bool IsPng(const std::string& b) { return b.size() > 8 && b.compare(0, 4, "\x89PNG") == 0; }
bool IsJpeg(const std::string& b) { return b.size() > 3 && (unsigned char)b[0] == 0xFF && (unsigned char)b[1] == 0xD8; }
bool IsMp4(const std::string& b) { return b.size() > 12 && b.compare(4, 4, "ftyp") == 0; }

bool HasMedia(const fs::path& dir, std::initializer_list<const wchar_t*> names) {
    std::error_code ec;
    for (auto n : names)
        if (fs::exists(dir / n, ec)) return true;
    return false;
}

// Écriture atomique (fichier temporaire puis remplacement) : jamais de média à moitié écrit.
bool SaveFile(const fs::path& file, const std::string& bytes, std::string& err) {
    fs::path tmp = file;
    tmp += L".part";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f || !f.write(bytes.data(), (std::streamsize)bytes.size())) {
            err = "écriture impossible : " + util::PathU8(file);
            return false;
        }
    }
    if (!MoveFileExW(tmp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        err = "remplacement impossible : " + util::PathU8(file);
        return false;
    }
    return true;
}

json ParseJson(const std::string& body, std::string& err) {
    try {
        return json::parse(body);
    } catch (const std::exception&) {
        std::string t = util::Trim(body.substr(0, 200));
        err = t.empty() ? "réponse vide" : t;  // ScreenScraper répond en texte brut en cas d'erreur
        return json();
    }
}

std::string Str(const json& j, const char* k) {
    if (!j.is_object() || !j.contains(k)) return {};
    const json& v = j[k];
    if (v.is_string()) return v.get<std::string>();
    if (v.is_number()) return v.dump();
    if (v.is_object() && v.contains("text") && v["text"].is_string()) return v["text"].get<std::string>();
    return {};
}

int YearOf(const std::string& s) {
    for (size_t i = 0; i + 4 <= s.size(); ++i)
        if (isdigit((unsigned char)s[i]) && isdigit((unsigned char)s[i + 1]) && isdigit((unsigned char)s[i + 2]) &&
            isdigit((unsigned char)s[i + 3]))
            return std::stoi(s.substr(i, 4));
    return 0;
}

// Choisit le texte d'un tableau [{key: valeur, text}] selon un ordre de préférence.
std::string Pick(const json& arr, const char* key, std::initializer_list<const char*> prefs) {
    if (!arr.is_array()) return {};
    for (const char* p : prefs)
        for (const auto& e : arr)
            if (Str(e, key) == p && !Str(e, "text").empty()) return Str(e, "text");
    for (const auto& e : arr)
        if (!Str(e, "text").empty()) return Str(e, "text");
    return {};
}

std::string HttpErrorText(const HttpResponse& r, const char* service) {
    if (!r.status) return r.error;
    switch (r.status) {
        case 401: case 403: return std::format("{} : identifiants ou clé refusés (HTTP {})", service, r.status);
        case 404: return std::format("{} : introuvable (HTTP 404)", service);
        case 429: return std::format("{} : trop de requêtes simultanées, réessayez plus tard (HTTP 429)", service);
        case 430: return std::format("{} : quota journalier atteint (HTTP 430)", service);
        case 431: return std::format("{} : trop de recherches sans résultat aujourd'hui (HTTP 431)", service);
    }
    std::string body = util::Trim(r.body.substr(0, 160));
    return std::format("{} : erreur HTTP {}{}{}", service, r.status, body.empty() ? "" : " - ", body);
}

// Télécharge une image et l'enregistre sous <base>.png ou <base>.jpg selon son contenu réel.
bool DownloadImage(const std::string& url, const std::vector<std::string>& hdr, const fs::path& dir, const wchar_t* base,
                   const std::atomic<bool>& cancel, std::vector<std::string>& written, std::string& err) {
    HttpResponse r = HttpGet(url, hdr, 30000, 40 * 1024 * 1024, &cancel);
    if (!r.ok()) {
        err = HttpErrorText(r, "téléchargement");
        return false;
    }
    const wchar_t* ext = IsPng(r.body) ? L".png" : IsJpeg(r.body) ? L".jpg" : nullptr;
    if (!ext) {
        err = "le fichier reçu n'est pas une image PNG/JPEG";
        return false;
    }
    fs::path file = dir / (std::wstring(base) + ext);
    std::error_code ec;  // l'autre extension éventuelle est retirée pour ne pas garder deux versions
    fs::remove(dir / (std::wstring(base) + (wcscmp(ext, L".png") == 0 ? L".jpg" : L".png")), ec);
    if (!SaveFile(file, r.body, err)) return false;
    written.push_back(util::PathU8(file));
    return true;
}

std::string Summary(const std::vector<std::string>& got, const std::vector<std::string>& missing) {
    std::string s = got.empty() ? "aucun média téléchargé" : "obtenu : ";
    for (size_t i = 0; i < got.size(); ++i) s += (i ? ", " : "") + got[i];
    if (!missing.empty()) {
        s += " ; absent : ";
        for (size_t i = 0; i < missing.size(); ++i) s += (i ? ", " : "") + missing[i];
    }
    return s;
}

// ---------------------------------------------------------------- SteamGridDB

std::vector<std::string> SgdbHeaders(const ScraperConfig& c) { return {"Authorization: Bearer " + c.sgdbKey}; }

// Meilleure image : style « official » d'abord, puis meilleur score.
const json* BestImage(const json& data) {
    const json* best = nullptr;
    int bestRank = -1;
    for (const auto& img : data) {
        if (!img.contains("url")) continue;
        int score = img.value("score", 0);
        bool official = false;
        if (img.contains("style")) {
            const json& st = img["style"];
            if (st.is_string()) official = st.get<std::string>() == "official";
            else if (st.is_array())
                for (const auto& s : st) official |= s.is_string() && s.get<std::string>() == "official";
        }
        int rank = (official ? 100000 : 0) + score;
        if (rank > bestRank) {
            bestRank = rank;
            best = &img;
        }
    }
    return best;
}

}  // namespace

namespace providers {

bool SgdbSearch(const ScraperConfig& c, const std::string& q, std::vector<ScrapeCandidate>& out, std::string& err) {
    if (!c.HasSgdb()) {
        err = "Clé API SteamGridDB non renseignée";
        return false;
    }
    HttpResponse r = HttpGet(c.sgdbBase + "/search/autocomplete/" + UrlEncode(q), SgdbHeaders(c));
    if (!r.ok()) {
        err = HttpErrorText(r, "SteamGridDB");
        return false;
    }
    json j = ParseJson(r.body, err);
    if (!j.is_object() || !j.value("success", false)) {
        if (err.empty()) err = "SteamGridDB : réponse inattendue";
        return false;
    }
    for (const auto& g : j.value("data", json::array())) {
        ScrapeCandidate cand{Provider::SteamGridDB, Str(g, "id"), Str(g, "name"), "", ""};
        if (g.contains("release_date") && g["release_date"].is_number()) {
            time_t t = g["release_date"].get<int64_t>();
            tm tmv{};
            if (gmtime_s(&tmv, &t) == 0 && t > 0) cand.year = std::to_string(1900 + tmv.tm_year);
        }
        if (!cand.id.empty()) out.push_back(cand);
    }
    return true;
}

bool SgdbFetch(const ScraperConfig& c, const ScrapeCandidate& cand, const fs::path& dir, bool overwrite,
               const std::atomic<bool>& cancel, FetchResult& res) {
    auto hdr = SgdbHeaders(c);
    std::vector<std::string> got, missing;
    std::string err;

    // URL de la meilleure image d'une liste SteamGridDB (chaîne vide si aucune).
    auto bestUrl = [&](const std::string& endpoint) -> std::string {
        HttpResponse r = HttpGet(endpoint, hdr);
        if (!r.ok()) {
            err = HttpErrorText(r, "SteamGridDB");
            return {};
        }
        json j = ParseJson(r.body, err);
        if (!j.is_object() || !j.contains("data") || !j["data"].is_array()) return {};
        const json* img = BestImage(j["data"]);  // pointe dans j, qui vit jusqu'à la fin de la fonction
        return img ? Str(*img, "url") : std::string();
    };

    if (overwrite || !HasMedia(dir, {L"logo.png", L"logo.jpg"})) {
        std::string url = bestUrl(std::format("{}/logos/game/{}?mimes=image/png&nsfw=false&humor=false", c.sgdbBase, cand.id));
        if (!url.empty() && DownloadImage(url, {}, dir, L"logo", cancel, res.written, err)) got.push_back("logo");
        else missing.push_back("logo");
    }
    if (cancel) { res.message = "annulé"; return false; }
    if (overwrite || !HasMedia(dir, {L"fanart.jpg", L"fanart.png"})) {
        std::string url = bestUrl(std::format("{}/heroes/game/{}?mimes=image/png,image/jpeg&nsfw=false&humor=false", c.sgdbBase, cand.id));
        if (!url.empty() && DownloadImage(url, {}, dir, L"fanart", cancel, res.written, err)) got.push_back("fond");
        else missing.push_back("fond");
    }
    res.message = Summary(got, missing) + (got.empty() && !err.empty() ? " (" + err + ")" : "");
    return !got.empty() || (missing.empty());
}

// ---------------------------------------------------------------- ScreenScraper

namespace {
std::string SsAuth(const ScraperConfig& c) {
    std::string a = "devid=" + UrlEncode(c.ssDevId) + "&devpassword=" + UrlEncode(c.ssDevPassword) +
                    "&softname=RacecabFrontend&output=json";
    if (!c.ssUser.empty()) a += "&ssid=" + UrlEncode(c.ssUser) + "&sspassword=" + UrlEncode(c.ssPassword);
    return a;
}

bool SsCall(const ScraperConfig& c, const std::string& endpoint, const std::string& params, json& out, std::string& err) {
    if (!c.HasScreenScraper()) {
        err = "Identifiants développeur ScreenScraper non renseignés";
        return false;
    }
    HttpResponse r = HttpGet(c.ssBase + endpoint + "?" + SsAuth(c) + "&" + params);
    if (!r.ok()) {
        err = HttpErrorText(r, "ScreenScraper");
        return false;
    }
    out = ParseJson(r.body, err);
    if (!out.is_object() || !out.contains("response")) {
        err = "ScreenScraper : " + (err.empty() ? std::string("réponse inattendue") : err);
        return false;
    }
    return true;
}

ScrapeCandidate SsCandidate(const json& g) {
    ScrapeCandidate cand{Provider::ScreenScraper, Str(g, "id"), "", "", ""};
    cand.name = Pick(g.value("noms", json::array()), "region", {"wor", "eu", "us", "fr", "jp", "ss"});
    if (cand.name.empty()) cand.name = Str(g, "nom");
    int y = YearOf(Pick(g.value("dates", json::array()), "region", {"wor", "eu", "us", "fr", "jp"}));
    if (y) cand.year = std::to_string(y);
    if (g.contains("systeme")) cand.platform = Str(g["systeme"], "text");
    return cand;
}

// URL du meilleur média parmi les types demandés (dans l'ordre), région préférée d'abord.
std::string SsMedia(const json& medias, std::initializer_list<const char*> types) {
    if (!medias.is_array()) return {};
    for (const char* t : types) {
        for (const char* reg : {"wor", "eu", "us", "fr", "jp", "ss", ""})
            for (const auto& m : medias)
                if (Str(m, "type") == t && Str(m, "region") == reg && !Str(m, "url").empty()) return Str(m, "url");
        for (const auto& m : medias)
            if (Str(m, "type") == t && !Str(m, "url").empty()) return Str(m, "url");
    }
    return {};
}
}  // namespace

// Nom comparable : lettres et chiffres seulement, sans « The » initial (« The Walking Dead » = « Walking Dead »).
static std::string SsComparable(const std::string& name) {
    std::string n = Scraper::Normalize(name);
    return n.rfind("the", 0) == 0 && n.size() > 6 && util::ToLower(name).rfind("the ", 0) == 0 ? n.substr(3) : n;
}

bool SsSearch(const ScraperConfig& c, const std::string& q, std::vector<ScrapeCandidate>& out, std::string& err) {
    // Comme RetroBat : recherche limitée aux systèmes TeknoParrot (269) et Arcade (75) pour ne pas tomber
    // sur la version console d'un jeu. ScreenScraper renvoie parfois d'autres systèmes malgré le filtre
    // (ex. « non Jeu », « Taito Classics ») : seul un jeu d'un système demandé peut être associé automatiquement.
    std::vector<std::string> systems;
    for (size_t a = 0; a <= c.ssSystems.size();) {
        size_t b = c.ssSystems.find(',', a);
        if (b == std::string::npos) b = c.ssSystems.size();
        std::string s = util::Trim(c.ssSystems.substr(a, b - a));
        if (!s.empty()) systems.push_back(s);
        a = b + 1;
    }
    const std::string want = SsComparable(q);
    auto run = [&](const std::string& query, const std::string& systemId) {
        json j;
        std::string params = "recherche=" + UrlEncode(query) + (systemId.empty() ? "" : "&systemeid=" + UrlEncode(systemId));
        if (!SsCall(c, "jeuRecherche.php", params, j, err)) return false;
        for (const auto& g : j["response"].value("jeux", json::array())) {
            ScrapeCandidate cand = SsCandidate(g);
            if (cand.id.empty() || cand.name.empty()) continue;
            bool dup = false;
            for (const auto& o : out) dup |= o.id == cand.id;
            if (dup) continue;
            const std::string sys = g.contains("systeme") ? Str(g["systeme"], "id") : "";
            if (sys == "168") continue;  // système « non Jeu » de ScreenScraper (fiches de rangement)
            // Plusieurs noms par jeu (ex. « Initial D Arcade Stage 7 AA X » en région ss, « Initial D : Arcade
            // Stage 7 Double Ace Cross » en mondial) : nom identique si l'un d'eux correspond.
            for (const auto& n : g.value("noms", json::array()))
                if (!cand.exact && SsComparable(Str(n, "text")) == want) {
                    cand.exact = true;
                    cand.name = Str(n, "text");
                }
            cand.autoOk = systems.empty() || std::find(systems.begin(), systems.end(), sys) != systems.end();
            if (!cand.autoOk) cand.note = "hors systèmes arcade : à vérifier";
            out.push_back(cand);
        }
        return true;
    };
    // La recherche de ScreenScraper ne trouve rien si un mot diffère (« AAX » au lieu de « AA X », « GTI Club 3 »
    // nommé « GTI Club 3 - Supermini Festa! ») : sans résultat, on retire le dernier mot (deux mots au moins).
    std::vector<std::string> words;
    for (size_t a = 0; a < q.size();) {
        size_t b = q.find(' ', a);
        if (b == std::string::npos) b = q.size();
        if (b > a) words.push_back(q.substr(a, b - a));
        a = b + 1;
    }
    if (words.empty()) return run(q, "");
    for (size_t n = words.size();; --n) {
        std::string query;
        for (size_t i = 0; i < n; ++i) query += (i ? " " : "") + words[i];
        for (const std::string& s : systems)
            if (!run(query, s)) return false;
        if (out.empty() && !run(query, "")) return false;  // recherche libre : hors systèmes demandés, à confirmer
        if (!out.empty() || n <= 2) break;
    }
    return true;
}

bool SsFetch(const ScraperConfig& c, const ScrapeCandidate& cand, const fs::path& dir, bool overwrite,
             const std::atomic<bool>& cancel, FetchResult& res) {
    json j;
    std::string err;
    if (!SsCall(c, "jeuInfos.php", "gameid=" + UrlEncode(cand.id), j, err)) {
        res.message = err;
        return false;
    }
    const json& g = j["response"].value("jeu", json::object());
    const json medias = g.value("medias", json::array());

    // Métadonnées (appliquées par le frontend aux seuls champs vides, sauf remplacement demandé)
    res.meta.publisher = Str(g, "editeur");
    res.meta.year = YearOf(Pick(g.value("dates", json::array()), "region", {"wor", "eu", "us", "fr", "jp"}));
    res.meta.description = Pick(g.value("synopsis", json::array()), "langue", {"fr", "en"});
    if (g.contains("synopsis") && g["synopsis"].is_array())
        for (const auto& s : g["synopsis"])
            if (Str(s, "langue") == "fr" && Str(s, "text") == res.meta.description) res.meta.descriptionFr = true;
    if (g.contains("systeme")) res.meta.platform = Str(g["systeme"], "text");
    if (g.contains("genres") && g["genres"].is_array() && !g["genres"].empty())
        res.meta.genre = Pick(g["genres"][0].value("noms", json::array()), "langue", {"fr", "en"});

    std::vector<std::string> got, missing;
    struct Want {
        const wchar_t* base;
        std::initializer_list<const char*> types;
        const char* label;
        std::initializer_list<const wchar_t*> existing;
    };
    Want wants[] = {{L"logo", {"wheel-hd", "wheel"}, "logo", {L"logo.png", L"logo.jpg"}},
                    {L"fanart", {"fanart"}, "fond", {L"fanart.jpg", L"fanart.png"}},
                    {L"screenshot", {"ss"}, "capture", {L"screenshot.jpg", L"screenshot.png"}}};
    for (const Want& w : wants) {
        if (cancel) { res.message = "annulé"; return false; }
        if (!overwrite && HasMedia(dir, w.existing)) continue;
        std::string url = SsMedia(medias, w.types);
        if (!url.empty() && DownloadImage(url, {}, dir, w.base, cancel, res.written, err)) got.push_back(w.label);
        else missing.push_back(w.label);
    }
    if (c.wantVideo && (overwrite || !HasMedia(dir, {L"video.mp4"}))) {
        std::string url = SsMedia(medias, {"video-normalized", "video"});
        HttpResponse r = url.empty() ? HttpResponse{} : HttpGet(url, {}, 60000, 300 * 1024 * 1024, &cancel);
        // ScreenScraper peut répondre « NOMEDIA » en texte : on vérifie que c'est bien un MP4.
        if (r.ok() && IsMp4(r.body) && SaveFile(dir / L"video.mp4", r.body, err)) {
            res.written.push_back(util::PathU8(dir / L"video.mp4"));
            got.push_back("vidéo");
        } else {
            missing.push_back("vidéo");
        }
    }
    res.message = Summary(got, missing);
    return true;
}

// ---------------------------------------------------------------- LaunchBox

bool LbSearch(LaunchBoxDb& db, const std::string& q, std::vector<ScrapeCandidate>& out, std::string&) {
    for (const LbMatch& m : db.Search(q)) {
        ScrapeCandidate cand{Provider::LaunchBox, m.game.id, m.game.name, "", m.game.platform};
        if (int y = YearOf(m.game.date)) cand.year = std::to_string(y);
        cand.exact = m.exact;
        if (!m.matchedAlt.empty()) cand.note = "nom alternatif : " + m.matchedAlt;
        out.push_back(cand);
    }
    return true;
}

bool LbFetch(const ScraperConfig& c, LaunchBoxDb& db, const ScrapeCandidate& cand, const fs::path& dir, bool overwrite,
             const std::atomic<bool>& cancel, FetchResult& res) {
    LbGame g;
    std::vector<LbImage> images;
    if (!db.Get(cand.id, g, images)) {
        res.message = "jeu absent de la base LaunchBox (base mise à jour depuis ?)";
        return false;
    }
    res.meta.publisher = !g.publisher.empty() ? g.publisher : g.developer;
    res.meta.year = YearOf(g.date);
    res.meta.genre = util::Trim(g.genres.substr(0, g.genres.find(';')));
    res.meta.description = g.overview;
    res.meta.platform = g.platform;

    // Meilleure image d'un type : région mondiale / sans région d'abord, puis Amérique du Nord, Europe, Japon...
    auto best = [&](std::initializer_list<const char*> types) -> std::string {
        for (const char* t : types)
            for (const char* reg : {"", "World", "North America", "United States", "Europe", "Japan"})
                for (const auto& im : images)
                    if (im.type == t && im.region == reg) return im.file;
        for (const char* t : types)
            for (const auto& im : images)
                if (im.type == t) return im.file;
        return {};
    };
    struct Want {
        const wchar_t* base;
        std::initializer_list<const char*> types;
        const char* label;
        std::initializer_list<const wchar_t*> existing;
    };
    Want wants[] = {{L"logo", {"Clear Logo"}, "logo", {L"logo.png", L"logo.jpg"}},
                    {L"fanart", {"Fanart - Background"}, "fond", {L"fanart.jpg", L"fanart.png"}},
                    {L"screenshot", {"Screenshot - Gameplay", "Screenshot - Game Title"}, "capture",
                     {L"screenshot.jpg", L"screenshot.png"}}};
    std::vector<std::string> got, missing;
    std::string err;
    for (const Want& w : wants) {
        if (cancel) { res.message = "annulé"; return false; }
        if (!overwrite && HasMedia(dir, w.existing)) continue;
        std::string file = best(w.types);
        if (!file.empty() && DownloadImage(c.lbImageBase + UrlEncode(file), {}, dir, w.base, cancel, res.written, err))
            got.push_back(w.label);
        else
            missing.push_back(w.label);
    }
    // Vidéo : lien YouTube de LaunchBox, téléchargé avec yt-dlp s'il est installé.
    std::string verr;
    if (c.wantVideo && c.lbAutoVideo && (overwrite || !HasMedia(dir, {L"video.mp4"})) && !cancel) {
        if (g.video.empty()) {
            verr = "pas de lien vidéo dans LaunchBox";
        } else if (ytvideo::FindYtDlp(c).empty()) {
            verr = "vidéo : yt-dlp non installé";
        } else if (fs::path pending; ytvideo::Download(c, g.video, dir, cancel, verr, &pending)) {
            got.push_back("vidéo");
            res.written.push_back(util::PathU8(dir / L"video.mp4"));
            if (!pending.empty()) {
                res.pendingFrom = util::PathU8(pending);
                res.pendingTo = util::PathU8(dir / L"video.mp4");
            }
            verr.clear();
        } else {
            missing.push_back("vidéo");
        }
    }
    res.message = Summary(got, missing) + (got.empty() && !err.empty() ? " (" + err + ")" : "") +
                  (verr.empty() ? "" : " (" + verr + ")");
    return true;
}

}  // namespace providers
