#pragma once
#include <SDL3/SDL.h>

#include <deque>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/Settings.h"
#include "core/Updater.h"
#include "input/Input.h"
#include "launcher/Launcher.h"
#include "library/Library.h"
#include "media/Scraper.h"
#include "profiles/Profile.h"
#include "video/CropDetect.h"
#include "video/VideoPlayer.h"
#include "render/Recorder.h"
#include "render/Text.h"
#include "render/TextureCache.h"
#include "theme/Theme.h"
#include "ui/Wheel.h"

class AdminUI;

// Boucle principale et machine à états du frontend.
class App {
public:
    enum class State { Browse, Launching, InGame, Returning, QuitConfirm, Menu, Wizard, Attract, ProfileSelect, LetterSelect };

    App();
    ~App();
    int Run(int argc, char** argv);

    // Accès pour l'administration
    Library& Lib() { return lib_; }
    Theme& CurrentTheme() { return theme_; }
    Settings& Config() { return settings_; }
    Input& Inputs() { return input_; }
    const Paths& Dirs() const { return paths_; }
    SDL_Window* Window() const { return window_; }
    TextureCache* Textures() const { return textures_.get(); }
    TextRenderer* Texts() const { return text_.get(); }
    float FrameMsAvg() const { return frameMsAvg_; }
    float FrameMsMax() const { return frameMsMax_; }

    void RefreshItems();        // à appeler après une modification de la bibliothèque
    void ApplyWindowSettings(); // plein écran, vsync
    void SaveSettings();
    void LaunchGame(int64_t id);
    void Toast(const std::string& msg, bool error = false);
    void RequestQuit() { running_ = false; }
    static const char* Version();
    Updater& Updates() { return updater_; }
    bool InstallUpdate(std::string& err);  // lance l'installation de la mise à jour prête, puis ferme le frontend
    void RequestRestart() {  // ex. après une restauration : le frontend se relance aussitôt
        restartRequested_ = true;
        running_ = false;
    }
    bool RestartRequested() const { return restartRequested_; }

    // Type de borne : "racecab" (volant / boutons) ou "guncab" (pistolet, vu comme une souris)
    bool GunMode() const { return settings_.cabMode == "guncab"; }
    void SetCabMode(const std::string& mode);
    std::vector<std::string> ProfilesForMode() const;  // profils proposés sur ce type de borne
    void OpenMenu();
    void StartWizard();

    // Profils
    Profile& CurrentProfile() { return profile_; }
    const std::string& ProfileName() const { return profileName_; }
    fs::path CurrentInputFile() const { return paths_.InputFileFor(profileName_); }
    void ActivateProfile(const std::string& name);
    void SaveProfile();  // enregistre et applique le filtre / les réglages du profil actif
    void OpenProfileSelect();

    // Médias
    VideoPlayer* Video() const { return video_.get(); }
    Scraper& Scrape() { return scraper_; }
    ScraperConfig& ScrapeConfig() { return scraperCfg_; }
    void SaveScraperConfig();
    std::optional<Provider> PreferredProvider();
    void StartBatch(const std::vector<int64_t>& gameIds);  // cascade de toutes les sources pour chaque jeu
    void CompleteGame(int64_t gameId, bool batch = false);  // cascade pour un jeu
    bool IsIncomplete(const Game& g);                       // logo, fond ou vidéo (si une source peut en fournir) absents
    std::vector<int64_t> IncompleteGames();
    std::optional<SearchResult>& LastSearch() { return lastSearch_; }
    std::vector<SearchResult>& ToConfirm() { return toConfirm_; }
    const std::string& LastFetchMessage() const { return lastFetchMessage_; }
    std::string& BatchStatus() { return batchStatus_; }

private:
    void HandleInput(const InputEvent& ev);
    void UpdateVideo(uint64_t now);
    void PollScraper();
    void HandleSearch(SearchResult r);
    void HandleComplete(CompleteResult r);
    void ApplyFetch(const FetchResult& r);
    void MenuAction(Action a);
    void WizardUpdate(uint64_t now);
    void WizardAbort();
    void DrawMenu(float W, float H);
    void DrawProfileSelect(float W, float H);
    void ProfileSelectAction(Action a);
    std::string StartupProfile() const;
    void CycleSort();
    void OpenLetterSelect();
    void LetterSelectAction(Action a);
    void DrawLetterSelect(float W, float H);
    bool HandleGunEvent(const SDL_Event& e);
    void Shoot(SDL_FPoint p);
    void UpdateHolds(uint64_t now);
    void DrawGunOverlay(float W, float H);
    // Disposition pistolet : grande image + bandes flèches, ou mosaïque
    bool GunLayout() const { return GunMode() && settings_.gun.arcadeLayout; }
    bool Mosaic() const { return GunLayout() && settings_.gun.view == "mosaic" && state_ != State::Attract; }
    void SetGunView(const std::string& v);
    void DrawGunBrowse(float W, float H);
    void DrawBlurFill(SDL_Texture* src, const SDL_FRect& box, const SDL_FRect* srcRect = nullptr);  // fond flou
    bool VideoSrcRect(SDL_FRect& r);  // zone utile de la vidéo en cours (sans bandes noires), en pixels de texture
    void DrawMediaBox(const WheelItem& it, const SDL_FRect& box, float fade);
    void DrawArrowBand(const SDL_FRect& r, bool left, int previewIndex, const char* label, float arrowY);
    bool GunBrowseShoot(SDL_FPoint p, uint64_t now);
    void SelectIndex(int idx);
    bool HandleSetupOptions(int argc, char** argv);  // --mode, --kiosk, --autostart, --setup-only
    void UpdateAttract(uint64_t now);
    void StartAttract(uint64_t now);
    void ExitAttract();
    void DrawWizard(float W, float H);
    bool Init(int argc, char** argv);
    void Shutdown();
    void HandleEvent(const SDL_Event& e);
    void HandleAction(Action a);
    void Update(float dt);
    void Render();

    void EnterNode(int64_t node, std::optional<WheelItem> select);
    WheelItem* SelectedItem();
    void SaveNavState();
    void OnGameFinished(const LaunchResult& r);
    void RestoreForeground();
    void LeaveLaunchScreen(void* gameWindow, const char* why);  // écran de chargement retiré, le jeu passe devant

    // Rendu
    void DrawBackground(float W, float H);
    void DrawHeader(float W, float H);
    void DrawMedia(float W, float H);
    void DrawInfo(float W, float H);
    void DrawWheel(float W, float H);
    void DrawOverlay(float W, float H);
    void DrawItemVisual(const WheelItem& it, const SDL_FRect& r, float alpha, double angle, bool selected);
    void FillRect(const SDL_FRect& r, Color c, float alpha = 1.0f);
    void DrawImageFit(SDL_Texture* t, const SDL_FRect& box, float alpha, double angle = 0.0, bool cover = false,
                      const SDL_FRect* srcRect = nullptr);
    std::string ItemTitle(const WheelItem& it) const;
    std::string ItemLogoPath(const WheelItem& it) const;

    Paths paths_;
    Settings settings_;
    Theme theme_;
    Library lib_;
    Input input_;
    Launcher launcher_;
    Wheel wheel_;

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    std::unique_ptr<TextureCache> textures_;
    std::unique_ptr<TextRenderer> text_;
    std::unique_ptr<AdminUI> admin_;
    std::unique_ptr<VideoPlayer> video_;
    SDL_Texture* videoTex_ = nullptr;  // image vidéo de la frame courante
    Updater updater_;
    bool updateNotified_ = false;
    CropCache crops_;                  // zones utiles des vidéos (bandes noires retirées)
    SDL_Texture* blurA_ = nullptr;     // fond flou : réductions successives de l'image (filtrage linéaire)
    SDL_Texture* blurB_ = nullptr;
    int blurAw_ = 0, blurAh_ = 0, blurBw_ = 0, blurBh_ = 0;
    Scraper scraper_;
    ScraperConfig scraperCfg_;
    std::optional<SearchResult> lastSearch_;
    std::vector<SearchResult> toConfirm_;
    std::string lastFetchMessage_, batchStatus_;
    struct PendingMove {
        std::string from, to, game;
        uint64_t deadline;
    };
    std::vector<PendingMove> pendingMoves_;  // vidéos téléchargées à mettre en place dès la fin de la lecture
    void ProcessPendingMoves(uint64_t now);
    int batchFetched_ = 0, batchAmbiguous_ = 0, batchNone_ = 0, batchErrors_ = 0;

    State state_ = State::Browse;
    bool running_ = true;
    bool restartRequested_ = false;
    std::string startupMessage_;  // message affiché au démarrage (restauration...)

    // Pistolet (mode GunCab) : position visée en coordonnées de rendu, zones cliquables de la frame précédente
    SDL_FPoint pointer_{-1, -1};
    SDL_FPoint pointerArea_{1, 1};  // taille de l'écran de rendu (zones du thème en fractions)
    uint64_t pointerSeenAt_ = 0;
    uint64_t gunBackDownAt_ = 0;
    bool gunBackHoldFired_ = false;
    SDL_FRect gunBackRect_{}, gunMenuRect_{}, gunViewRect_{}, menuBox_{}, quitYesRect_{}, quitNoRect_{};
    SDL_FRect gunLeftRect_{}, gunRightRect_{}, gunCenterRect_{};
    std::vector<std::pair<SDL_FRect, int>> tileRects_;  // mosaïque : case -> indice
    std::vector<SDL_FRect> menuRects_, profileRects_, letterRects_;
    uint64_t backHeldSince_ = 0;  // Retour maintenu 5 s : quitter (avec confirmation), même en mode kiosque

    // Saut à une lettre
    std::vector<char> letters_;
    int letterIndex_ = 0;
    bool importOnly_ = false;
    int64_t node_ = Library::kAll;
    std::vector<WheelItem> items_;
    std::unordered_map<int64_t, WheelItem> lastSelection_;  // sélection mémorisée par catégorie
    std::vector<Wheel::Slot> slots_;
    std::vector<int> prefetch_;

    int64_t launchingId_ = 0;
    uint64_t stateSince_ = 0;
    uint64_t nextWindowScan_ = 0;
    void* fgAtLaunch_ = nullptr;   // fenêtre au premier plan au moment du lancement  // recherche de la fenêtre du jeu pendant l'écran de chargement
    std::string toast_;
    bool toastError_ = false;
    uint64_t toastUntil_ = 0;
    uint64_t selectionChangedAt_ = 0;
    uint64_t arrowShotAt_[2] = {0, 0};  // disposition pistolet : dernier tir sur la flèche gauche / droite (retour visuel)

    float frameMsAvg_ = 0, frameMsMax_ = 0;

    // Profil actif et sélection
    Profile profile_;
    std::string profileName_;
    std::vector<std::string> profileList_;
    int profileIndex_ = 0;

    // Mode démonstration
    uint64_t idleSince_ = 0;
    int64_t attractSavedNode_ = Library::kAll;
    std::optional<WheelItem> attractSavedItem_;
    uint64_t attractNextAt_ = 0;
    std::mt19937 rng_{std::random_device{}()};

    // Menu de la borne (navigable au volant / aux boutons)
    struct MenuEntry {
        std::string label;
        std::function<void()> run;
    };
    std::vector<MenuEntry> menu_;
    int menuIndex_ = 0;

    // Assistant de configuration des commandes
    struct Wizard {
        enum class Phase { WaitRelease, Listen, Show } phase = Phase::WaitRelease;
        int step = 0;
        uint64_t since = 0;
        std::optional<Binding> got;
        std::string warning;
        std::vector<Binding> backup;
        std::vector<Binding> results;
    } wiz_;

    // Tests automatisés (options --actions / --screenshot, voir docs/Compilation.md)
    void ParseTestOptions(int argc, char** argv);
    void RunScript(uint64_t nowMs);
    void MaybeScreenshot(uint64_t nowMs);
    std::deque<std::pair<uint64_t, std::string>> script_;  // (délai ms, action ou "wait")
    uint64_t scriptNextAt_ = 0;
    std::string screenshotPath_;
    std::string recordPath_;   // --record fichier.mp4 : démonstration filmée
    std::unique_ptr<Recorder> recorder_;
    uint64_t recordStopAt_ = 0;
    uint64_t screenshotAt_ = 0;
};
