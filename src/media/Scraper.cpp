#include "media/Scraper.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>

#include "core/Log.h"
#include "media/LaunchBox.h"
#include "media/YtVideo.h"
#include "net/Http.h"

const char* ProviderName(Provider p) {
    switch (p) {
        case Provider::SteamGridDB: return "SteamGridDB";
        case Provider::LaunchBox: return "LaunchBox";
        default: return "ScreenScraper";
    }
}

bool ScraperConfig::Load(const fs::path& file) {
    std::ifstream f(file, std::ios::binary);
    if (!f) return false;
    try {
        auto j = nlohmann::json::parse(f, nullptr, true, true);
        enabled = j.value("enabled", enabled);
        autoOnAdd = j.value("autoOnAdd", autoOnAdd);
        wantVideo = j.value("wantVideo", wantVideo);
        preferred = j.value("preferred", preferred);
        sgdbKey = j.value("steamgriddbKey", "");
        ssDevId = j.value("screenscraperDevId", "");
        ssDevPassword = j.value("screenscraperDevPassword", "");
        ssUser = j.value("screenscraperUser", "");
        ssPassword = j.value("screenscraperPassword", "");
        sgdbBase = j.value("steamgriddbBaseUrl", sgdbBase);
        ssBase = j.value("screenscraperBaseUrl", ssBase);
        ssSystems = j.value("screenscraperSystems", ssSystems);
        lbMetadataUrl = j.value("launchboxMetadataUrl", lbMetadataUrl);
        lbImageBase = j.value("launchboxImageBaseUrl", lbImageBase);
        if (j.contains("launchboxPlatforms") && j["launchboxPlatforms"].is_array())
            lbPlatforms = j["launchboxPlatforms"].get<std::vector<std::string>>();
        ytdlpPath = j.value("ytdlpPath", ytdlpPath);
        lbAutoVideo = j.value("launchboxAutoVideo", lbAutoVideo);
        videoSeconds = std::clamp(j.value("videoSeconds", videoSeconds), 5, 120);
        videoStartSec = std::clamp(j.value("videoStartSec", videoStartSec), 0, 600);
        return true;
    } catch (const std::exception& e) {
        logx::Error("scraper.json invalide : {}", e.what());
        return false;
    }
}

bool ScraperConfig::Save(const fs::path& file) const {
    nlohmann::json j = {{"enabled", enabled},
                        {"autoOnAdd", autoOnAdd},
                        {"wantVideo", wantVideo},
                        {"preferred", preferred},
                        {"steamgriddbKey", sgdbKey},
                        {"screenscraperDevId", ssDevId},
                        {"screenscraperDevPassword", ssDevPassword},
                        {"screenscraperUser", ssUser},
                        {"screenscraperPassword", ssPassword},
                        {"screenscraperSystems", ssSystems},
                        {"launchboxPlatforms", lbPlatforms},
                        {"ytdlpPath", ytdlpPath},
                        {"launchboxAutoVideo", lbAutoVideo},
                        {"videoSeconds", videoSeconds},
                        {"videoStartSec", videoStartSec}};
    ScraperConfig def;
    if (sgdbBase != def.sgdbBase) j["steamgriddbBaseUrl"] = sgdbBase;
    if (ssBase != def.ssBase) j["screenscraperBaseUrl"] = ssBase;
    if (lbMetadataUrl != def.lbMetadataUrl) j["launchboxMetadataUrl"] = lbMetadataUrl;
    if (lbImageBase != def.lbImageBase) j["launchboxImageBaseUrl"] = lbImageBase;
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream f(file, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << j.dump(2);
    return (bool)f;
}

Scraper::Scraper() { worker_ = std::thread([this] { Loop(); }); }

Scraper::~Scraper() {
    {
        std::lock_guard lk(m_);
        quit_ = true;
        jobs_.clear();
    }
    cancel_ = true;
    cv_.notify_all();
    worker_.join();
}

void Scraper::SetConfig(const ScraperConfig& c) {
    {
        std::lock_guard lk(m_);
        cfg_ = c;
        disabled_.clear();  // nouveaux identifiants : toutes les sources sont de nouveau essayées
    }
    RefreshLaunchBoxInfo(c);
}

void Scraper::RefreshLaunchBoxInfo(const ScraperConfig& c) {
    std::string info = c.lbDir.empty() ? std::string() : LaunchBoxDb::Describe(c.lbDir / L"launchbox.db");
    std::lock_guard lk(m_);
    lbReady_ = !info.empty();
    lbInfo_ = lbReady_ ? info : "base non téléchargée";
}

bool Scraper::LaunchBoxReady() {
    std::lock_guard lk(m_);
    return lbReady_;
}

std::string Scraper::LaunchBoxInfo() {
    std::lock_guard lk(m_);
    return lbInfo_;
}

void Scraper::SetCurrent(const std::string& s) {
    std::lock_guard lk(m_);
    current_ = s;
}

void Scraper::UpdateYtDlp() {
    Enqueue("Mise à jour de yt-dlp", [this] {
        std::string out;
        ytvideo::SelfUpdate(Config(), out);
        std::lock_guard lk(m_);
        ytdlpInfo_ = out;
    });
}

std::string Scraper::YtDlpInfo() {
    std::lock_guard lk(m_);
    return ytdlpInfo_;
}

void Scraper::UpdateLaunchBox(bool download) {
    Enqueue("Base LaunchBox", [this, download] {
        ScraperConfig c = Config();
        fs::path zip = c.lbDir / L"Metadata.zip", dbFile = c.lbDir / L"launchbox.db";
        std::error_code ec;
        fs::create_directories(c.lbDir, ec);
        std::string err, summary;
        bool ok = true;
        if (download || !fs::exists(zip, ec)) {
            logx::Info("LaunchBox : téléchargement de {}", c.lbMetadataUrl);
            HttpResponse r = HttpDownload(c.lbMetadataUrl, zip, &cancel_, [this](uint64_t got, uint64_t total) {
                SetCurrent(total ? std::format("Base LaunchBox : téléchargement {} / {} Mo", got >> 20, total >> 20)
                                 : std::format("Base LaunchBox : téléchargement {} Mo", got >> 20));
            });
            if (!r.ok()) {
                ok = false;
                err = r.status ? std::format("téléchargement refusé (HTTP {})", r.status) : r.error;
            }
        }
        if (ok) {
            if (lb_) lb_->Close();  // l'index ouvert empêcherait son remplacement
            SetCurrent("Base LaunchBox : construction de l'index");
            ok = LaunchBoxDb::BuildIndex(zip, dbFile, c.lbPlatforms, cancel_, [this](uint64_t mb) {
                SetCurrent(std::format("Base LaunchBox : construction de l'index ({} Mo analysés)", mb));
            }, summary, err);
        }
        RefreshLaunchBoxInfo(c);
        if (ok) {
            logx::Info("LaunchBox : index construit, {}", summary);
        } else {
            logx::Error("LaunchBox : {}", err);
            std::lock_guard lk(m_);
            lbInfo_ = (lbReady_ ? lbInfo_ + " ; " : std::string()) + "échec de la mise à jour : " + err;
        }
    });
}

ScraperConfig Scraper::Config() {
    std::lock_guard lk(m_);
    return cfg_;
}

void Scraper::Enqueue(std::string label, std::function<void()> job) {
    {
        std::lock_guard lk(m_);
        jobs_.push_back({std::move(label), std::move(job)});
    }
    cv_.notify_one();
}

void Scraper::CancelAll() {
    {
        std::lock_guard lk(m_);
        jobs_.clear();
    }
    cancel_ = true;  // interrompt le téléchargement en cours ; remis à zéro à la tâche suivante
}

size_t Scraper::Pending() {
    std::lock_guard lk(m_);
    return jobs_.size() + (current_.empty() ? 0 : 1);
}

std::string Scraper::Status() {
    std::lock_guard lk(m_);
    return current_;
}

std::optional<SearchResult> Scraper::PollSearch() {
    std::lock_guard lk(m_);
    if (searches_.empty()) return std::nullopt;
    auto r = std::move(searches_.front());
    searches_.pop_front();
    return r;
}

std::optional<FetchResult> Scraper::PollFetch() {
    std::lock_guard lk(m_);
    if (fetches_.empty()) return std::nullopt;
    auto r = std::move(fetches_.front());
    fetches_.pop_front();
    return r;
}

std::string Scraper::Normalize(const std::string& name) {
    std::string out;
    for (unsigned char c : name)
        if (std::isalnum(c)) out += (char)std::tolower(c);
    return out;
}

void Scraper::Search(int64_t gameId, const std::string& query, Provider p, bool batch) {
    Enqueue(std::string("Recherche « ") + query + " » (" + ProviderName(p) + ")", [this, gameId, query, p, batch] {
        SearchResult r;
        r.gameId = gameId;
        r.provider = p;
        r.query = query;
        r.batch = batch;
        ScraperConfig c = Config();
        r.ok = DoSearch(c, p, query, r.candidates, r.error);
        if (!r.ok) logx::Warn("Recherche {} « {} » : {}", ProviderName(p), query, r.error);
        else logx::Info("Recherche {} « {} » : {} résultat(s)", ProviderName(p), query, r.candidates.size());
        std::lock_guard lk(m_);
        searches_.push_back(std::move(r));
    });
}

void Scraper::Fetch(int64_t gameId, const ScrapeCandidate& cand, const fs::path& dir, bool overwrite) {
    Enqueue(std::string("Téléchargement des médias : ") + cand.name, [this, gameId, cand, dir, overwrite] {
        FetchResult r;
        r.gameId = gameId;
        ScraperConfig c = Config();
        r.ok = DoFetch(c, cand, dir, overwrite, r);
        logx::Info("Médias de « {} » ({}) : {}", cand.name, ProviderName(cand.provider), r.message);
        std::lock_guard lk(m_);
        fetches_.push_back(std::move(r));
    });
}

void Scraper::DownloadVideo(int64_t gameId, const std::string& name, const std::string& url, int startSec,
                            const fs::path& dir, int durationSec) {
    Enqueue("Vidéo YouTube : " + name, [this, gameId, name, url, startSec, dir, durationSec] {
        ScraperConfig c = Config();
        c.videoStartSec = startSec;
        if (durationSec >= 0) c.videoSeconds = durationSec;
        FetchResult r;
        r.gameId = gameId;
        r.replaceVideo = true;
        r.videoUrl = url;
        r.videoStart = startSec;
        r.videoDuration = durationSec;
        std::string err;
        std::error_code ec;
        fs::create_directories(dir, ec);
        fs::path pending;
        r.ok = ytvideo::Download(c, url, dir, cancel_, err, &pending);
        if (r.ok && !pending.empty()) {
            r.pendingFrom = util::PathU8(pending);
            r.pendingTo = util::PathU8(dir / L"video.mp4");
        }
        if (r.ok) {
            r.written.push_back(util::PathU8(dir / L"video.mp4"));
            r.message = c.videoSeconds > 0
                            ? std::format("vidéo YouTube : extrait de {} s à partir de {} s enregistré", c.videoSeconds, startSec)
                            : std::format("vidéo YouTube : enregistrée de {} s jusqu'à la fin", startSec);
        } else {
            r.message = "vidéo YouTube : " + err;
        }
        logx::Info("« {} » : {}", name, r.message);
        std::lock_guard lk(m_);
        fetches_.push_back(std::move(r));
    });
}

bool Scraper::DoSearch(const ScraperConfig& c, Provider p, const std::string& q, std::vector<ScrapeCandidate>& out,
                       std::string& err) {
    try {
        if (!c.enabled) {
            err = "Récupération des médias désactivée dans les réglages";
            return false;
        }
        if (p == Provider::SteamGridDB) return providers::SgdbSearch(c, q, out, err);
        if (p == Provider::LaunchBox) {
            if (!lb_) lb_ = std::make_unique<LaunchBoxDb>();
            return lb_->Open(c.lbDir / L"launchbox.db", err) && providers::LbSearch(*lb_, q, out, err);
        }
        return providers::SsSearch(c, q, out, err);
    } catch (const std::exception& e) {  // toujours un résultat : l'interface n'attend jamais indéfiniment
        err = std::string("réponse inattendue du service (") + e.what() + ")";
        return false;
    }
}

bool Scraper::DoFetch(const ScraperConfig& c, const ScrapeCandidate& cand, const fs::path& dir, bool overwrite,
                      FetchResult& r) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    try {
        if (!c.enabled) {
            r.message = "Récupération des médias désactivée dans les réglages";
            return false;
        }
        if (cand.provider == Provider::SteamGridDB) return providers::SgdbFetch(c, cand, dir, overwrite, cancel_, r);
        if (cand.provider == Provider::LaunchBox) {
            if (!lb_) lb_ = std::make_unique<LaunchBoxDb>();
            return lb_->Open(c.lbDir / L"launchbox.db", r.message) &&
                   providers::LbFetch(c, *lb_, cand, dir, overwrite, cancel_, r);
        }
        return providers::SsFetch(c, cand, dir, overwrite, cancel_, r);
    } catch (const std::exception& e) {
        r.message = std::string("réponse inattendue du service (") + e.what() + ")";
        return false;
    }
}

std::vector<Provider> Scraper::CascadeOrder() {
    ScraperConfig c = Config();
    std::lock_guard lk(m_);
    auto usable = [&](Provider p) {
        if (disabled_.count(p)) return false;
        if (p == Provider::LaunchBox) return lbReady_;
        if (p == Provider::ScreenScraper) return c.HasScreenScraper();
        return c.HasSgdb();
    };
    std::vector<Provider> order;
    Provider pref = c.preferred == "screenscraper" ? Provider::ScreenScraper
                    : c.preferred == "steamgriddb" ? Provider::SteamGridDB
                                                   : Provider::LaunchBox;
    for (Provider p : {pref, Provider::LaunchBox, Provider::ScreenScraper, Provider::SteamGridDB})
        if (usable(p) && std::find(order.begin(), order.end(), p) == order.end()) order.push_back(p);
    return order;
}

namespace {
bool Has(const fs::path& dir, std::initializer_list<const wchar_t*> names) {
    std::error_code ec;
    for (auto n : names)
        if (fs::exists(dir / n, ec)) return true;
    return false;
}
// Erreur qui rend la source inutilisable pour la suite (identifiants, quota, base absente).
bool Fatal(const std::string& e) {
    return e.find("HTTP 4") != std::string::npos || e.find("login") != std::string::npos ||
           e.find("refus") != std::string::npos || e.find("non renseign") != std::string::npos ||
           e.find("Base LaunchBox") != std::string::npos;
}
}  // namespace

void Scraper::Complete(const CompleteRequest& req) {
    Enqueue("Toutes les sources : " + req.name, [this, req] {
        ScraperConfig c = Config();
        CompleteResult res;
        res.gameId = req.gameId;
        res.batch = req.batch;
        res.fetch.gameId = req.gameId;
        res.toConfirm.gameId = req.gameId;
        res.toConfirm.query = req.name;
        res.toConfirm.batch = req.batch;
        res.toConfirm.ok = true;
        ScrapedMeta known = req.current;  // ce que le jeu a déjà + ce que les sources apportent

        // Ce que chaque source sait fournir.
        enum Need { kLogo = 1, kFanart = 2, kShot = 4, kVideo = 8, kMeta = 16 };
        auto missing = [&]() {
            int m = 0;
            if (!Has(req.dir, {L"logo.png", L"logo.jpg"})) m |= kLogo;
            if (!Has(req.dir, {L"fanart.jpg", L"fanart.png"})) m |= kFanart;
            if (!Has(req.dir, {L"screenshot.jpg", L"screenshot.png"})) m |= kShot;
            if (c.wantVideo && !Has(req.dir, {L"video.mp4"})) m |= kVideo;
            if (!known.Complete() || !known.descriptionFr) m |= kMeta;
            return m;
        };
        const bool ytOk = c.lbAutoVideo && !ytvideo::FindYtDlp(c).empty();  // vidéos LaunchBox : option + yt-dlp
        auto provides = [ytOk](Provider p) {
            switch (p) {
                case Provider::LaunchBox: return kLogo | kFanart | kShot | kMeta | (ytOk ? kVideo : 0);
                case Provider::ScreenScraper: return kLogo | kFanart | kShot | kVideo | kMeta;
                default: return kLogo | kFanart;
            }
        };
        const std::string norm = Normalize(req.name);
        for (Provider p : CascadeOrder()) {
            if (cancel_) break;
            int need = missing();
            // Une description en anglais suffit si seul le français manque et que la source n'apporte que cela.
            if (!(need & ~kMeta) && known.Complete()) need &= ~kMeta;
            if (!(need & provides(p))) continue;
            SetCurrent(std::string("Toutes les sources : ") + req.name + " (" + ProviderName(p) + ")");
            std::vector<ScrapeCandidate> cands;
            std::string err;
            if (!DoSearch(c, p, req.name, cands, err)) {
                res.errors.push_back(std::string(ProviderName(p)) + " : " + err);
                res.steps.push_back(std::string(ProviderName(p)) + " : erreur");
                if (Fatal(err)) {
                    std::lock_guard lk(m_);
                    disabled_.insert(p);
                    logx::Warn("Cascade : {} écarté jusqu'au prochain changement de réglages ({})", ProviderName(p), err);
                }
                continue;
            }
            std::vector<const ScrapeCandidate*> exact;
            for (const auto& cand : cands)
                if (cand.autoOk && (cand.exact || Normalize(cand.name) == norm)) exact.push_back(&cand);
            if (exact.size() != 1) {
                for (auto cand : cands) {
                    cand.note = std::string(ProviderName(p)) + (cand.note.empty() ? "" : " - " + cand.note);
                    res.toConfirm.candidates.push_back(cand);
                }
                res.steps.push_back(std::format("{} : {}", ProviderName(p),
                                                cands.empty() ? std::string("aucun résultat")
                                                : exact.empty() ? std::format("{} résultat(s) à confirmer", cands.size())
                                                                : std::format("{} noms identiques, à confirmer", exact.size())));
                continue;
            }
            FetchResult f;
            f.gameId = req.gameId;
            bool ok = DoFetch(c, *exact[0], req.dir, false, f);
            res.steps.push_back(std::string(ProviderName(p)) + " : " + f.message);
            if (!ok && Fatal(f.message)) {
                std::lock_guard lk(m_);
                disabled_.insert(p);
            }
            if (!ok) res.errors.push_back(std::string(ProviderName(p)) + " : " + f.message);
            for (auto& w : f.written) res.fetch.written.push_back(w);
            if (!f.pendingFrom.empty()) {
                res.fetch.pendingFrom = f.pendingFrom;
                res.fetch.pendingTo = f.pendingTo;
            }
            res.fetch.meta.Merge(f.meta);
            known.Merge(f.meta);
        }
        int left = missing();
        if (left & kLogo) res.stillMissing.push_back("logo");
        if (left & kFanart) res.stillMissing.push_back("fond");
        if (left & kShot) res.stillMissing.push_back("capture");
        if (left & kVideo) res.stillMissing.push_back("vidéo");
        res.fetch.ok = res.errors.empty() || res.Obtained();
        std::string msg;
        for (auto& s : res.steps) msg += (msg.empty() ? "" : " | ") + s;
        if (res.steps.empty()) msg = res.stillMissing.empty() ? "déjà complet" : "aucune source disponible pour ce qui manque";
        if (!res.stillMissing.empty()) {
            msg += " | manque encore :";
            for (size_t i = 0; i < res.stillMissing.size(); ++i) msg += (i ? ", " : " ") + res.stillMissing[i];
        }
        res.fetch.message = msg;
        logx::Info("Cascade « {} » : {}", req.name, msg);
        std::lock_guard lk(m_);
        completes_.push_back(std::move(res));
    });
}

std::optional<CompleteResult> Scraper::PollComplete() {
    std::lock_guard lk(m_);
    if (completes_.empty()) return std::nullopt;
    auto r = std::move(completes_.front());
    completes_.pop_front();
    return r;
}

void Scraper::Loop() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock lk(m_);
            current_.clear();
            cv_.wait(lk, [this] { return quit_ || !jobs_.empty(); });
            if (quit_) return;
            current_ = jobs_.front().first;
            job = std::move(jobs_.front().second);
            jobs_.pop_front();
        }
        cancel_ = false;
        try {
            job();
        } catch (const std::exception& e) {
            logx::Error("Récupération des médias : erreur inattendue {}", e.what());
        }
    }
}
