#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "core/Util.h"

// Réglages de récupération des médias : <données>/config/scraper.json
// Les identifiants sont saisis par l'utilisateur et ne quittent sa machine que vers le service concerné.
struct ScraperConfig {
    bool enabled = true;          // récupération autorisée (désactivable globalement)
    bool autoOnAdd = false;       // rechercher automatiquement à l'ajout d'un jeu
    bool wantVideo = true;
    std::string preferred = "launchbox";  // "launchbox", "screenscraper" ou "steamgriddb"
    std::string sgdbKey;
    std::string ssDevId, ssDevPassword, ssUser, ssPassword;
    // Adresses des services (modifiables uniquement à la main dans scraper.json, pour les tests).
    std::string sgdbBase = "https://www.steamgriddb.com/api/v2";
    std::string ssBase = "https://api.screenscraper.fr/api2/";
    // Systèmes ScreenScraper autorisés pour la recherche (comme RetroBat) : 269 = TeknoParrot, 75 = Arcade.
    // Vide = aucun filtre. Sans résultat dans ces systèmes, les autres sont proposés mais jamais associés automatiquement.
    std::string ssSystems = "269,75";
    // LaunchBox Games Database : sans compte. Plateformes gardées dans l'index local.
    std::string lbMetadataUrl = "https://gamesdb.launchbox-app.com/Metadata.zip";
    std::string lbImageBase = "https://images.launchbox-app.com/";
    std::vector<std::string> lbPlatforms = {"Arcade"};
    // Vidéos depuis les liens YouTube de LaunchBox, via yt-dlp installé par l'utilisateur (voir YtVideo.h).
    std::string ytdlpPath;      // vide = recherche automatique
    bool lbAutoVideo = false;   // télécharger automatiquement la vidéo proposée par LaunchBox (qualité variable)
    int videoSeconds = 30;      // durée de l'extrait gardé
    int videoStartSec = 10;     // début de l'extrait (saute les génériques)
    fs::path lbDir;  // <données>\cache\launchbox (fixé par le programme, non enregistré)

    bool HasSgdb() const { return !sgdbKey.empty(); }
    bool HasScreenScraper() const { return !ssDevId.empty() && !ssDevPassword.empty(); }
    bool Load(const fs::path& file);
    bool Save(const fs::path& file) const;
};

class LaunchBoxDb;

enum class Provider { SteamGridDB, ScreenScraper, LaunchBox };
const char* ProviderName(Provider p);

struct ScrapeCandidate {
    Provider provider;
    std::string id;
    std::string name;
    std::string year;
    std::string platform;
    bool exact = false;    // le service garantit un nom identique (ex. nom alternatif LaunchBox)
    bool autoOk = true;    // false : jamais d'association automatique (résultat hors des systèmes demandés)
    std::string note;      // précision affichée (nom alternatif, autre système...)
};

struct ScrapedMeta {
    std::string publisher, genre, description, platform;
    int year = 0;
    bool descriptionFr = false;  // description en français (préférée à une description en anglais)
    // Complète les champs vides avec ceux de o ; une description française remplace une description étrangère.
    void Merge(const ScrapedMeta& o) {
        if (publisher.empty()) publisher = o.publisher;
        if (genre.empty()) genre = o.genre;
        if (platform.empty()) platform = o.platform;
        if (!year) year = o.year;
        if (!o.description.empty() && (description.empty() || (o.descriptionFr && !descriptionFr))) {
            description = o.description;
            descriptionFr = o.descriptionFr;
        }
    }
    bool Complete() const { return !publisher.empty() && !genre.empty() && !description.empty() && year; }
};

struct SearchResult {
    int64_t gameId = 0;
    Provider provider = Provider::ScreenScraper;
    std::string query;
    bool ok = false;
    std::string error;
    std::vector<ScrapeCandidate> candidates;
    bool batch = false;  // recherche lancée par « tous les jeux sans logo »
};

struct FetchResult {
    int64_t gameId = 0;
    bool ok = false;
    std::string message;                // résumé lisible (fichiers obtenus / erreur)
    std::vector<std::string> written;   // fichiers écrits (chemins UTF-8)
    bool replaceVideo = false;          // vidéo YouTube choisie à la main : remplace la vidéo de la fiche
    std::string pendingFrom, pendingTo; // fichier prêt à remplacer une vidéo en cours de lecture (fait par l'interface)
    std::string videoUrl;               // vidéo YouTube choisie à la main : lien, début, durée (mémorisés dans la fiche)
    int videoStart = 0, videoDuration = -1;
    ScrapedMeta meta;
};

// Cascade : toutes les sources disponibles, dans l'ordre, chacune ne complétant que ce qui manque encore.
struct CompleteRequest {
    int64_t gameId = 0;
    std::string name;
    fs::path dir;          // dossier media\<id>
    ScrapedMeta current;   // métadonnées déjà connues du jeu (pour savoir s'il en manque)
    bool batch = false;
};

struct CompleteResult {
    int64_t gameId = 0;
    bool batch = false;
    FetchResult fetch;               // médias écrits et métadonnées réunies de toutes les sources
    std::vector<std::string> steps;  // « LaunchBox : obtenu logo, fond », « SteamGridDB : 3 résultats à confirmer »...
    std::vector<std::string> stillMissing;
    SearchResult toConfirm;          // résultats proposés par les sources sans nom exact unique
    std::vector<std::string> errors;
    bool Obtained() const { return !fetch.written.empty(); }
};

// File de travail unique (un seul accès réseau à la fois : respecte la limite de 1 thread des comptes gratuits).
// Rien ne bloque l'interface : les résultats sont récupérés par Poll*() sur le thread principal.
class Scraper {
public:
    Scraper();
    ~Scraper();
    void SetConfig(const ScraperConfig& c);
    ScraperConfig Config();

    void Search(int64_t gameId, const std::string& query, Provider p, bool batch = false);
    void Fetch(int64_t gameId, const ScrapeCandidate& c, const fs::path& gameMediaDir, bool overwrite);
    void CancelAll();
    // Vidéo YouTube choisie à la main pour un jeu : extrait à partir de startSec, écrit <dir>\video.mp4.
    // durationSec : durée voulue ; 0 = jusqu'à la fin de la vidéo ; -1 = durée des réglages (30 s par défaut).
    void DownloadVideo(int64_t gameId, const std::string& name, const std::string& url, int startSec, const fs::path& dir,
                       int durationSec = -1);
    // LaunchBox : télécharge Metadata.zip (si download ou s'il est absent) puis reconstruit l'index local.
    void UpdateLaunchBox(bool download);
    bool LaunchBoxReady();
    void UpdateYtDlp();            // yt-dlp -U, en tâche de fond
    std::string YtDlpInfo();       // résultat de la dernière mise à jour
    std::string LaunchBoxInfo();  // résumé de l'index ou dernier message d'erreur

    std::optional<SearchResult> PollSearch();
    std::optional<FetchResult> PollFetch();

    // Cascade de sources pour un jeu (ordre : source préférée, puis LaunchBox, ScreenScraper, SteamGridDB).
    // Une source dont les identifiants sont refusés ou le quota atteint est ignorée jusqu'au prochain SetConfig.
    void Complete(const CompleteRequest& req);
    std::optional<CompleteResult> PollComplete();
    std::vector<Provider> CascadeOrder();  // sources utilisables, dans l'ordre
    size_t Pending();
    std::string Status();  // tâche en cours

    static std::string Normalize(const std::string& name);  // pour la correspondance exacte

private:
    void Enqueue(std::string label, std::function<void()> job);
    void Loop();
    void SetCurrent(const std::string& s);
    bool DoSearch(const ScraperConfig& c, Provider p, const std::string& q, std::vector<ScrapeCandidate>& out, std::string& err);
    bool DoFetch(const ScraperConfig& c, const ScrapeCandidate& cand, const fs::path& dir, bool overwrite, FetchResult& r);
    void RefreshLaunchBoxInfo(const ScraperConfig& c);

    std::mutex m_;
    std::condition_variable cv_;
    std::deque<std::pair<std::string, std::function<void()>>> jobs_;
    std::deque<SearchResult> searches_;
    std::deque<FetchResult> fetches_;
    std::deque<CompleteResult> completes_;
    std::set<Provider> disabled_;  // sources écartées de la cascade (identifiants refusés, quota)
    std::string current_;
    ScraperConfig cfg_;
    std::atomic<bool> cancel_{false};
    std::unique_ptr<LaunchBoxDb> lb_;  // utilisé uniquement par le thread de travail
    bool lbReady_ = false;
    std::string lbInfo_;
    std::string ytdlpInfo_;
    bool quit_ = false;
    std::thread worker_;
};

// Fournisseurs (Providers.cpp) — appelés depuis le thread de travail uniquement.
namespace providers {
bool SgdbSearch(const ScraperConfig& c, const std::string& q, std::vector<ScrapeCandidate>& out, std::string& err);
bool SgdbFetch(const ScraperConfig& c, const ScrapeCandidate& cand, const fs::path& dir, bool overwrite,
               const std::atomic<bool>& cancel, FetchResult& res);
bool SsSearch(const ScraperConfig& c, const std::string& q, std::vector<ScrapeCandidate>& out, std::string& err);
bool SsFetch(const ScraperConfig& c, const ScrapeCandidate& cand, const fs::path& dir, bool overwrite,
             const std::atomic<bool>& cancel, FetchResult& res);
bool LbSearch(LaunchBoxDb& db, const std::string& q, std::vector<ScrapeCandidate>& out, std::string& err);
bool LbFetch(const ScraperConfig& c, LaunchBoxDb& db, const ScrapeCandidate& cand, const fs::path& dir, bool overwrite,
             const std::atomic<bool>& cancel, FetchResult& res);
}  // namespace providers
