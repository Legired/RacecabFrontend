#include "app/App.h"

#include "render/Shapes.h"

#include <windows.h>

#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>

#include "admin/AdminUI.h"
#include "core/Backup.h"
#include "core/ConfigShare.h"
#include "core/Log.h"
#include "core/System.h"
#include "media/LaunchBox.h"
#include "media/YtVideo.h"

namespace {
constexpr const char* kVersion = "0.6.0";

bool Inside(const SDL_FRect& r, SDL_FPoint p) { return p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h; }

const char* SortLabel(const std::string& s) {
    if (s == "year") return "Année";
    if (s == "played") return "Temps de jeu";
    if (s == "count") return "Lancements";
    return "Nom";
}

// Lettre de rangement d'un nom : A-Z, sinon « # ».
char LetterOf(const std::string& name) {
    for (unsigned char c : name) {
        if (std::isalpha(c) && c < 128) return (char)std::toupper(c);
        if (std::isdigit(c)) return '#';
        if (c >= 128) return '#';
    }
    return '#';
}

// Temps écoulé sans débordement : un horodatage pris pendant la frame peut être postérieur à « now ».
uint64_t Elapsed(uint64_t now, uint64_t since) { return now > since ? now - since : 0; }

HWND NativeWindow(SDL_Window* w) {
    return (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(w), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
}

// Fenêtre qui peut être celle du jeu : visible, non réduite, assez grande, ni console ni fenêtre outil.
// La console d'un .bat (conhost ou Windows Terminal) ne compte jamais.
bool LooksLikeGameWindow(HWND h) {
    if (!h || !IsWindowVisible(h) || IsIconic(h)) return false;
    if (GetWindowLongPtrW(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return false;
    wchar_t cls[64]{};
    GetClassNameW(h, cls, 64);
    if (!wcscmp(cls, L"ConsoleWindowClass") || !wcscmp(cls, L"CASCADIA_HOSTING_WINDOW_CLASS") ||
        !wcscmp(cls, L"PseudoConsoleWindow"))
        return false;
    RECT rc;
    if (!GetWindowRect(h, &rc)) return false;
    return rc.right - rc.left >= 320 && rc.bottom - rc.top >= 240;
}

std::wstring ProcessFileName(DWORD pid) {
    std::wstring name;
    if (HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        wchar_t buf[MAX_PATH];
        DWORD n = MAX_PATH;
        if (QueryFullProcessImageNameW(p, 0, buf, &n)) {
            name.assign(buf, n);
            name = name.substr(name.find_last_of(L"\\/") + 1);
        }
        CloseHandle(p);
    }
    return name;
}

// Vraie fenêtre du jeu : une fenêtre d'un processus du jeu (Job) ou du processus à surveiller.
// À défaut, une fenêtre d'un autre programme passée au premier plan (jeu lancé hors du Job par un lanceur).
HWND FindGameWindow(const std::vector<uint32_t>& pids, const std::wstring& watch, HWND fgAtLaunch) {
    struct Ctx {
        const std::vector<uint32_t>& pids;
        const std::wstring& watch;
        DWORD self;
        HWND found;
    } c{pids, watch, GetCurrentProcessId(), nullptr};
    EnumWindows(
        [](HWND h, LPARAM lp) -> BOOL {
            auto& c = *(Ctx*)lp;
            DWORD pid = 0;
            GetWindowThreadProcessId(h, &pid);
            if (pid == c.self || !LooksLikeGameWindow(h)) return TRUE;
            bool game = std::find(c.pids.begin(), c.pids.end(), pid) != c.pids.end() ||
                        (!c.watch.empty() && _wcsicmp(ProcessFileName(pid).c_str(), c.watch.c_str()) == 0);
            if (!game) return TRUE;
            c.found = h;
            return FALSE;
        },
        (LPARAM)&c);
    if (c.found) return c.found;
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    // Seulement une fenêtre passée devant après le lancement (pas celle qui l'était déjà).
    return fg != fgAtLaunch && pid && pid != c.self && LooksLikeGameWindow(fg) ? fg : nullptr;
}

std::string DescribeWindow(HWND h) {
    wchar_t cls[64]{}, title[96]{};
    GetClassNameW(h, cls, 64);
    GetWindowTextW(h, title, 96);
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    return std::format("« {} » ({}, {})", util::Narrow(title), util::Narrow(ProcessFileName(pid)), util::Narrow(cls));
}
}  // namespace

App::App() = default;
App::~App() = default;

int App::Run(int argc, char** argv) {
    if (!Init(argc, argv)) {
        Shutdown();
        return 1;
    }
    if (importOnly_) {
        Shutdown();
        return 0;
    }
    uint64_t prev = SDL_GetTicksNS();
    uint64_t statWindowStart = SDL_GetTicks();
    float workMax = 0;

    while (running_) {
        SDL_Event e;
        if (state_ == State::InGame) {
            // Pendant le jeu : aucun rendu, réveil toutes les 250 ms pour surveiller la fin.
            if (SDL_WaitEventTimeout(&e, 250)) {
                HandleEvent(e);
                while (SDL_PollEvent(&e)) HandleEvent(e);
            }
            if (auto r = launcher_.Poll()) OnGameFinished(*r);
            prev = SDL_GetTicksNS();
            continue;
        }

        while (SDL_PollEvent(&e)) HandleEvent(e);
        const uint64_t nowMs = SDL_GetTicks();
        input_.Update(nowMs);
        InputEvent ev;
        while (input_.Pop(ev))
            if (!admin_->IsOpen()) HandleInput(ev);
        if (state_ == State::Wizard) WizardUpdate(nowMs);
        UpdateHolds(nowMs);
        RunScript(nowMs);
        UpdateAttract(nowMs);

        if (state_ == State::Launching) {
            Launcher::Phase ph = launcher_.CurrentPhase();
            // L'écran de chargement reste au premier plan tant que la vraie fenêtre du jeu n'est pas là
            // (la console d'un .bat ou un lanceur intermédiaire ne doivent pas apparaître).
            if ((ph == Launcher::Phase::Running || ph == Launcher::Phase::Watching) && nowMs >= nextWindowScan_) {
                nextWindowScan_ = nowMs + 150;
                if (HWND game = FindGameWindow(launcher_.Pids(), launcher_.WatchName(), (HWND)fgAtLaunch_))
                    LeaveLaunchScreen(game, "fenêtre du jeu repérée");
                else if (Elapsed(nowMs, stateSince_) > (uint64_t)settings_.launchScreenMaxSec * 1000)
                    LeaveLaunchScreen(nullptr, "aucune fenêtre de jeu repérée, délai dépassé");
            }
        }
        if (auto r = launcher_.Poll()) OnGameFinished(*r);
        PollScraper();
        if (!updateNotified_ && updater_.GetState() == Updater::State::Available) {
            updateNotified_ = true;
            Toast("Mise à jour " + updater_.Release().version + " disponible : Administration > Système");
        }
        if (!pendingMoves_.empty()) ProcessPendingMoves(nowMs);
        UpdateVideo(nowMs);

        uint64_t now = SDL_GetTicksNS();
        float dt = std::min(0.1f, (float)((now - prev) / 1e9));
        prev = now;
        frameMsAvg_ = frameMsAvg_ * 0.95f + dt * 1000.0f * 0.05f;

        Update(dt);
        uint64_t workStart = SDL_GetTicksNS();
        Render();
        float work = (float)((SDL_GetTicksNS() - workStart) / 1e6);
        workMax = std::max(workMax, work);
        if (nowMs - statWindowStart > 1000) {
            frameMsMax_ = workMax;
            workMax = 0;
            statWindowStart = nowMs;
        }
        if (!settings_.vsync) SDL_Delay(1);  // évite une boucle à 100 % CPU sans vsync
    }
    SaveNavState();
    Shutdown();
    return 0;
}

bool App::Init(int argc, char** argv) {
    paths_ = Paths::Resolve(argc, argv);
    paths_.EnsureDirs();
    logx::Init(paths_.LogsDir());
    logx::Info("=== Racecab Frontend {} ===", kVersion);
    logx::Info("Dossier de données : {}", util::PathU8(paths_.dataDir));

    // Restauration programmée : appliquée avant toute ouverture de fichier de configuration.
    if (backup::ApplyPendingRestore(paths_, startupMessage_)) logx::Info("{}", startupMessage_);

    if (!settings_.Load(paths_.SettingsFile())) settings_.Save(paths_.SettingsFile());
    if (HandleSetupOptions(argc, argv)) {
        importOnly_ = true;  // --setup-only : réglages écrits, pas de fenêtre
        return true;
    }
    if (settings_.kiosk) {
        settings_.fullscreen = true;
        settings_.hideCursor = true;
    }
    logx::Info("Type de borne : {}{}", GunMode() ? "GunCab (pistolet)" : "Racecab (volant)", settings_.kiosk ? ", mode kiosque" : "");

    if (!lib_.Open(paths_.DbFile(), paths_.MediaDir())) return false;
    for (int i = 1; i < argc; ++i) {
        std::string k = argv[i];
        if (k == "--import-only") importOnly_ = true;
        if ((k == "--import" || k == "--import-only") && i + 1 < argc) {
            int added = 0, dup = 0;
            std::string err;
            if (!lib_.ImportList(util::U8Path(argv[i + 1]), added, dup, err)) logx::Error("Import : {}", err);
        }
    }
    if (importOnly_) return true;  // import en ligne de commande, sans ouvrir de fenêtre
    if (settings_.autoBackup) backup::AutoIfDue(paths_, settings_.backupKeep);

    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "0");  // ne jamais lire les commandes destinées au jeu
    SDL_SetHint(SDL_HINT_APP_NAME, "Racecab Frontend");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
        logx::Error("SDL_Init : {}", SDL_GetError());
        return false;
    }
    if (!TTF_Init()) {
        logx::Error("TTF_Init : {}", SDL_GetError());
        return false;
    }

    SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY | (settings_.fullscreen ? SDL_WINDOW_FULLSCREEN : SDL_WINDOW_RESIZABLE);
    window_ = SDL_CreateWindow("Racecab Frontend", settings_.windowW, settings_.windowH, flags);
    if (!window_) {
        logx::Error("Création de la fenêtre : {}", SDL_GetError());
        return false;
    }
    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_) {
        logx::Error("Création du renderer : {}", SDL_GetError());
        return false;
    }
    SDL_SetRenderVSync(renderer_, settings_.vsync ? 1 : 0);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    logx::Info("Renderer : {}", SDL_GetRendererName(renderer_));

    Theme::EnsureBuiltins(paths_.ThemesDir());
    // Migration : avant les profils, mapping et état étaient uniques ; ils deviennent ceux du profil Racecab.
    {
        std::error_code ec;
        if (fs::exists(paths_.InputFile(), ec) && !fs::exists(paths_.InputFileFor("Racecab"), ec))
            fs::copy_file(paths_.InputFile(), paths_.InputFileFor("Racecab"), ec);
        if (fs::exists(paths_.StateFile(), ec) && !fs::exists(paths_.StateFileFor("Racecab"), ec))
            fs::copy_file(paths_.StateFile(), paths_.StateFileFor("Racecab"), ec);
    }
    Profile::EnsureDefaults(paths_.ProfilesDir());
    input_.Init(settings_.input);

    textures_ = std::make_unique<TextureCache>(renderer_, (size_t)settings_.textureCacheMB * 1024 * 1024,
                                               settings_.maxImageSide);
    text_ = std::make_unique<TextRenderer>(renderer_);
    video_ = std::make_unique<VideoPlayer>(renderer_);
    crops_.Init(paths_.dataDir / L"cache" / L"videocrop.json");
    scraperCfg_.lbDir = paths_.dataDir / L"cache" / L"launchbox";
    if (!scraperCfg_.Load(paths_.ScraperFile())) scraperCfg_.Save(paths_.ScraperFile());
    scraper_.SetConfig(scraperCfg_);
    {
        // Index LaunchBox d'avant les liens vidéo : reconstruit en tâche de fond depuis Metadata.zip (sans téléchargement).
        std::error_code ec;
        const fs::path db = scraperCfg_.lbDir / L"launchbox.db";
        if (scraper_.LaunchBoxReady() && !LaunchBoxDb::HasVideoLinks(db) && fs::exists(scraperCfg_.lbDir / L"Metadata.zip", ec)) {
            logx::Info("Index LaunchBox sans liens vidéo : reconstruction en tâche de fond");
            scraper_.UpdateLaunchBox(false);
        }
    }
    if (settings_.updateAutoCheck && !settings_.updateRepo.empty()) updater_.Check(settings_.updateRepo, kVersion);
    admin_ = std::make_unique<AdminUI>(*this);
    if (!admin_->Init(window_, renderer_)) return false;

    ActivateProfile(StartupProfile());
    if (settings_.askProfileAtStartup) OpenProfileSelect();

    if (settings_.hideCursor || GunMode()) SDL_HideCursor();  // pistolet : viseur dessiné par le frontend
    if (!startupMessage_.empty()) Toast(startupMessage_);
    ParseTestOptions(argc, argv);
    return true;
}

bool App::HandleSetupOptions(int argc, char** argv) {
    bool setupOnly = false, changed = false;
    for (int i = 1; i < argc; ++i) {
        std::string k = argv[i], v = i + 1 < argc ? argv[i + 1] : "";
        if (k == "--setup-only") setupOnly = true;
        if (k == "--mode" && (v == "racecab" || v == "guncab")) {
            settings_.cabMode = v;
            changed = true;
        }
        if (k == "--kiosk" && (v == "on" || v == "off")) {
            settings_.kiosk = v == "on";
            changed = true;
        }
        if (k == "--autostart" && (v == "on" || v == "off")) {
            std::string err;
            if (sys::SetAutoStart(v == "on", paths_.dataDir, err)) logx::Info("Démarrage avec Windows : {}", v);
            else logx::Error("Démarrage avec Windows : {}", err);
        }
    }
    if (changed || setupOnly) {
        Profile::EnsureDefaults(paths_.ProfilesDir());
        Theme::EnsureBuiltins(paths_.ThemesDir());
        settings_.profile = StartupProfile();
        settings_.Save(paths_.SettingsFile());
        logx::Info("Installation : borne {}, kiosque {}, profil {}", settings_.cabMode, settings_.kiosk ? "oui" : "non",
                   settings_.profile);
    }
    return setupOnly;
}

std::vector<std::string> App::ProfilesForMode() const {
    std::vector<std::string> out;
    for (const std::string& n : Profile::List(paths_.ProfilesDir())) {
        Profile p;
        if (!p.Load(paths_.ProfileFile(n)) || p.FitsMode(settings_.cabMode)) out.push_back(n);
    }
    return out;
}

std::string App::StartupProfile() const {
    std::vector<std::string> ok = ProfilesForMode();
    auto has = [&](const std::string& n) { return std::find(ok.begin(), ok.end(), n) != ok.end(); };
    if (has(settings_.profile)) return settings_.profile;
    std::string def = settings_.cabMode == "guncab" ? "GunCab" : "Racecab";
    if (has(def)) return def;
    if (!ok.empty()) return ok.front();
    return settings_.profile;
}

void App::SetCabMode(const std::string& mode) {
    if (mode != "racecab" && mode != "guncab") return;
    settings_.cabMode = mode;
    SaveSettings();
    std::string want = StartupProfile();
    if (want != profileName_) ActivateProfile(want);
    if (GunMode()) SDL_HideCursor();
    Toast(GunMode() ? "Borne GunCab : commandes au pistolet" : "Borne Racecab : commandes au volant");
    logx::Info("Type de borne changé : {}", mode);
}

// ---------------------------------------------------------------- Tests automatisés
// --actions "Next,Next,wait:500,Select,F1"   : actions injectées (200 ms d'écart), F1 = administration
// --screenshot fichier.png                     : capture 1,2 s après la dernière action, puis quitte

void App::ParseTestOptions(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i) {
        std::string k = argv[i], v = argv[i + 1];
        if (k == "--screenshot") screenshotPath_ = v;
        if (k == "--record") recordPath_ = v;
        if (k == "--actions") {
            size_t s = 0;
            while (s <= v.size()) {
                size_t e = v.find(',', s);
                if (e == std::string::npos) e = v.size();
                std::string tok = util::Trim(v.substr(s, e - s));
                s = e + 1;
                if (tok.empty()) continue;
                if (tok.rfind("wait:", 0) == 0) script_.push_back({(uint64_t)std::stoul(tok.substr(5)), "wait"});
                else script_.push_back({200, tok});
            }
        }
    }
    scriptNextAt_ = SDL_GetTicks() + 1500;  // laisse le temps au premier affichage
    if (!recordPath_.empty()) {
        int ow, oh;
        SDL_GetRenderOutputSize(renderer_, &ow, &oh);
        recorder_ = std::make_unique<Recorder>();
        std::string err;
        if (!recorder_->Start(util::Widen(recordPath_), ow, oh, 2'200'000, err)) {
            logx::Error("Enregistrement vidéo : {}", err);
            recorder_.reset();
        }
    }
    if (!screenshotPath_.empty() && script_.empty()) screenshotAt_ = SDL_GetTicks() + 2500;
}

void App::RunScript(uint64_t now) {
    if (script_.empty() || now < scriptNextAt_) return;
    if (state_ == State::Launching || state_ == State::InGame || state_ == State::Returning) return;  // attend le retour d'un jeu
    auto [delay, tok] = script_.front();
    script_.pop_front();
    scriptNextAt_ = now + delay;
    logx::Info("[test] action : {}", tok);
    Action a;
    // Événements bruts injectés dans la file SDL : ils suivent exactement le chemin d'une vraie entrée.
    auto pushPair = [](SDL_Event down, SDL_EventType upType) {
        SDL_PushEvent(&down);
        SDL_Event up = down;
        up.type = upType;
        if (upType == SDL_EVENT_KEY_UP) up.key.down = false;
        if (upType == SDL_EVENT_JOYSTICK_BUTTON_UP) up.jbutton.down = false;
        SDL_PushEvent(&up);
    };
    if (tok == "F1") admin_->Toggle();
    else if (tok.rfind("adminTab:", 0) == 0) admin_->OpenTab(tok.substr(9));
    else if (tok == "updateCheck") updater_.Check(settings_.updateRepo, kVersion);  // = boutons de Système > Mises à jour
    else if (tok == "updateDownload") updater_.Download(paths_.dataDir);
    else if (tok == "updateInstall") {
        std::string err;
        if (!InstallUpdate(err)) logx::Error("[test] mise à jour : {}", err);
    }
    else if (tok.rfind("scanFolder:", 0) == 0) {  // = Jeux > « Scanner un dossier » puis choix du dossier
        admin_->OpenTab("Jeux");
        admin_->OnDialogResult(AdminUI::Field::ScanFolder, tok.substr(11));
    }
    else if (tok.rfind("shareExport:", 0) == 0) {  // = bouton « Exporter la configuration » (options par défaut)
        std::string summary, err;
        bool ok = share::Export(lib_, paths_, {}, util::U8Path(tok.substr(12)), summary, err);
        logx::Info("[test] export : {} {}", ok ? summary : "échec", err);
    }
    else if (tok == "HoldBack") HandleInput({Action::Back, true});
    else if (tok == "attract") StartAttract(now);
    else if (tok.rfind("profile:", 0) == 0) ActivateProfile(tok.substr(8));
    else if (tok.rfind("cabmode:", 0) == 0) SetCabMode(tok.substr(8));
    else if (tok.rfind("launch:", 0) == 0) LaunchGame(std::stoll(tok.substr(7)));
    else if (tok == "backupNow") {
        std::string name, err;
        backup::Create(paths_, "sauvegarde", false, name, err);
    } else if (tok == "restoreOldest") {  // restaure la plus ancienne sauvegarde puis redémarre
        auto list = backup::List(paths_);
        std::string err;
        if (!list.empty() && backup::ScheduleRestore(paths_, list.back().file, err)) RequestRestart();
    }
    else if (tok.rfind("ytvideo:", 0) == 0) {
        // « ytvideo:<id jeu>@<début s>[/<durée s>]@<lien> » = bouton « Télécharger l'extrait » (durée 0 = jusqu'à la fin)
        std::string rest = tok.substr(8);
        size_t a = rest.find('@'), b = a == std::string::npos ? a : rest.find('@', a + 1);
        if (b != std::string::npos)
            if (const Game* g = lib_.FindGame(std::stoll(rest.substr(0, a)))) {
                std::string times = rest.substr(a + 1, b - a - 1);
                size_t slash = times.find('/');
                int dur = slash == std::string::npos ? -1 : std::stoi(times.substr(slash + 1));
                scraper_.DownloadVideo(g->id, g->name, rest.substr(b + 1), std::stoi(times.substr(0, slash)),
                                       paths_.MediaDir() / std::to_wstring(g->id), dur);
            }
    }
    else if (tok == "lbUpdate") scraper_.UpdateLaunchBox(false);  // index depuis Metadata.zip (téléchargé s'il manque)
    else if (tok == "scrapeMissing") {  // équivalent du bouton « Compléter les médias des jeux incomplets »
        StartBatch(IncompleteGames());
        logx::Info("[test] {}", batchStatus_);
    }
    else if (tok.rfind("shoot:", 0) == 0 || tok == "rightclick" || tok.rfind("aim:", 0) == 0) {
        // Pistolet simulé : « shoot:0.5/0.85 » = tir à 50 % de la largeur, 85 % de la hauteur.
        int ow, oh;
        SDL_GetRenderOutputSize(renderer_, &ow, &oh);
        float fx = 0.5f, fy = 0.5f;
        size_t c = tok.find(':'), sl = tok.find('/');
        if (c != std::string::npos && sl != std::string::npos) {
            fx = std::stof(tok.substr(c + 1, sl - c - 1));
            fy = std::stof(tok.substr(sl + 1));
        }
        float wx = 0, wy = 0;
        SDL_RenderCoordinatesToWindow(renderer_, fx * ow, fy * oh, &wx, &wy);
        logx::Info("[test] pistolet : rendu {}x{}, cible ({:.0f}, {:.0f}) -> fenêtre ({:.0f}, {:.0f})", ow, oh, fx * ow, fy * oh, wx, wy);
        SDL_Event m{};
        m.type = SDL_EVENT_MOUSE_MOTION;
        m.motion.x = wx;
        m.motion.y = wy;
        SDL_PushEvent(&m);
        if (tok.rfind("aim:", 0) != 0) {
            SDL_Event b{};
            b.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            b.button.button = tok == "rightclick" ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
            b.button.down = true;
            b.button.x = wx;
            b.button.y = wy;
            SDL_PushEvent(&b);
            b.type = SDL_EVENT_MOUSE_BUTTON_UP;
            b.button.down = false;
            SDL_PushEvent(&b);
        }
    } else if (tok.rfind("key:", 0) == 0) {
        SDL_Event e{};
        e.type = SDL_EVENT_KEY_DOWN;
        e.key.key = SDL_GetKeyFromName(tok.substr(4).c_str());
        e.key.down = true;
        pushPair(e, SDL_EVENT_KEY_UP);
    } else if (tok.rfind("joybutton:", 0) == 0) {
        constexpr SDL_JoystickID kFake = 9999;
        input_.TestAddDevice(kFake, "Joystick de test", false);
        SDL_Event e{};
        e.type = SDL_EVENT_JOYSTICK_BUTTON_DOWN;
        e.jbutton.which = kFake;
        e.jbutton.button = (Uint8)std::stoi(tok.substr(10));
        e.jbutton.down = true;
        pushPair(e, SDL_EVENT_JOYSTICK_BUTTON_UP);
    } else if (ActionFromName(tok, a)) {
        if (!admin_->IsOpen()) HandleAction(a);
    } else if (tok != "wait") logx::Warn("[test] action inconnue : {}", tok);
    if (script_.empty() && !screenshotPath_.empty()) screenshotAt_ = now + (tok == "wait" ? delay : 0) + 1200;
    if (script_.empty() && recorder_) recordStopAt_ = now + (tok == "wait" ? delay : 0) + 300;
}

void App::MaybeScreenshot(uint64_t now) {
    if (screenshotPath_.empty() || !screenshotAt_ || now < screenshotAt_ || !script_.empty()) return;
    if (state_ == State::Launching || state_ == State::InGame || state_ == State::Returning) return;
    if (SDL_Surface* raw = SDL_RenderReadPixels(renderer_, nullptr)) {
        SDL_Surface* s = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGB24);  // l'écran est opaque : pas de canal alpha
        SDL_DestroySurface(raw);
        if (!s) return;
        if (IMG_SavePNG(s, screenshotPath_.c_str())) logx::Info("[test] capture : {}", screenshotPath_);
        else logx::Error("[test] capture impossible : {}", SDL_GetError());
        SDL_DestroySurface(s);
    }
    screenshotPath_.clear();
    running_ = false;
}

void App::Shutdown() {
    if (blurA_) SDL_DestroyTexture(blurA_);
    if (blurB_) SDL_DestroyTexture(blurB_);
    blurA_ = blurB_ = nullptr;
    if (admin_) admin_->Shutdown();
    admin_.reset();
    scraper_.CancelAll();
    video_.reset();
    textures_.reset();
    text_.reset();
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    renderer_ = nullptr;
    window_ = nullptr;
    TTF_Quit();
    SDL_Quit();
    logx::Info("Arrêt du frontend");
    logx::Shutdown();
}

void App::SaveSettings() {
    settings_.input = input_.Timing();
    settings_.Save(paths_.SettingsFile());
}

void App::ApplyWindowSettings() {
    SDL_SetWindowFullscreen(window_, settings_.fullscreen);
    if (!settings_.fullscreen) {
        SDL_SetWindowResizable(window_, true);
        SDL_SetWindowSize(window_, settings_.windowW, settings_.windowH);
        SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
    SDL_SetRenderVSync(renderer_, settings_.vsync ? 1 : 0);
}

// ---------------------------------------------------------------- Événements et actions

void App::HandleEvent(const SDL_Event& e) {
    const bool deviceEvent = e.type == SDL_EVENT_JOYSTICK_ADDED || e.type == SDL_EVENT_JOYSTICK_REMOVED;
    if (e.type == SDL_EVENT_QUIT) {
        running_ = false;
        return;
    }
    if (e.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
        input_.Clear();
        input_.BlockUntil(SDL_GetTicks() + 300);
    }
    if (input_.IsUserActivity(e)) {
        idleSince_ = SDL_GetTicks();
        // Démo : la moindre entrée l'arrête immédiatement, et cette entrée n'a pas d'autre effet
        // (appuyer sur Valider pour réveiller la borne ne lance pas de jeu).
        if (state_ == State::Attract) {
            ExitAttract();
            return;
        }
    }

    if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F1 && !e.key.repeat &&
        (state_ == State::Browse || state_ == State::Menu)) {
        if (state_ == State::Menu) state_ = State::Browse;
        admin_->Toggle();
        return;
    }
    // Échap au clavier annule toujours l'assistant (le volant peut ne plus rien faire si le mapping est faux).
    if (state_ == State::Wizard && e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE && !e.key.repeat) {
        WizardAbort();
        return;
    }
    if (admin_->IsOpen()) {
        // L'administration consomme les événements ; Input les voit aussi pour l'affichage
        // « dernière entrée détectée », mais ses actions sont ignorées (voir Run).
        admin_->ProcessEvent(e);
        input_.HandleEvent(e);
        return;
    }
    if (GunMode() && HandleGunEvent(e)) return;
    if (state_ == State::Browse || state_ == State::QuitConfirm || state_ == State::Menu ||
        state_ == State::Wizard || state_ == State::ProfileSelect || state_ == State::LetterSelect || deviceEvent)
        input_.HandleEvent(e);
}

// ---------------------------------------------------------------- Pistolet (borne GunCab)
// Les pistolets (Retro Shooter RS3, Sinden, Gun4IR, AimTrak...) en mode souris : viser déplace le viseur,
// la gâchette (clic gauche) choisit ce qui est visé, le clic droit (recharge / tir hors écran) vaut Retour,
// le clic du milieu ouvre le menu. Les autres boutons du pistolet envoient des touches : assistant de configuration.

bool App::HandleGunEvent(const SDL_Event& e) {
    const bool active = state_ == State::Browse || state_ == State::Menu || state_ == State::ProfileSelect ||
                        state_ == State::LetterSelect || state_ == State::QuitConfirm;
    auto aim = [&](float wx, float wy) {
        float x = wx, y = wy;
        SDL_RenderCoordinatesFromWindow(renderer_, wx, wy, &x, &y);
        pointer_ = {x, y};
        pointerSeenAt_ = SDL_GetTicks();
    };
    switch (e.type) {
        case SDL_EVENT_MOUSE_MOTION: aim(e.motion.x, e.motion.y); return true;
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            aim(e.button.x, e.button.y);
            const uint64_t now = SDL_GetTicks();
            if (!active || input_.Blocked(now)) return true;  // garde après le retour d'un jeu
            idleSince_ = now;
            if (e.button.button == SDL_BUTTON_LEFT) Shoot(pointer_);
            else if (e.button.button == SDL_BUTTON_RIGHT) {
                gunBackDownAt_ = now;
                gunBackHoldFired_ = false;
            } else if (e.button.button == SDL_BUTTON_MIDDLE) {
                if (state_ == State::Browse) OpenMenu();
                else HandleAction(Action::Menu);
            }
            return true;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e.button.button == SDL_BUTTON_RIGHT) {
                // Comme le bouton Retour : court = retour (au relâchement), long = menu de la borne.
                if (gunBackDownAt_ && !gunBackHoldFired_ && active) HandleAction(Action::Back);
                gunBackDownAt_ = 0;
            }
            return true;
        case SDL_EVENT_MOUSE_WHEEL: return true;
        default: return false;
    }
}

void App::Shoot(SDL_FPoint p) {
    const uint64_t now = SDL_GetTicks();
    logx::Info("Pistolet : tir en ({:.0f}, {:.0f})", p.x, p.y);
    switch (state_) {
        case State::Browse: {
            if (Inside(gunBackRect_, p)) {
                HandleAction(Action::Back);
                return;
            }
            if (Inside(gunMenuRect_, p)) {
                OpenMenu();
                return;
            }
            if (GunLayout()) {
                if (Inside(gunViewRect_, p)) SetGunView(Mosaic() ? "single" : "mosaic");
                else GunBrowseShoot(p, now);
                return;
            }
            int hit = -1;
            float best = -1;
            for (const Wheel::Slot& s : slots_)
                if (Inside(s.rect, p) && s.focus > best) {
                    best = s.focus;
                    hit = s.index;
                }
            const RectF& m = theme_.mediaRect;
            SDL_FRect media{m.x * pointerArea_.x, m.y * pointerArea_.y, m.w * pointerArea_.x, m.h * pointerArea_.y};
            const int sel = wheel_.Selected(), n = (int)items_.size();
            if (n == 0) return;
            if (hit == sel || (hit < 0 && Inside(media, p))) {
                // Tir sur le jeu déjà sélectionné : lancement, seulement s'il est sélectionné depuis un moment
                // (deux tirs rapprochés ne lancent pas par erreur le jeu qu'on vient de viser).
                if (Elapsed(now, selectionChangedAt_) >= (uint64_t)settings_.gun.launchGuardMs && !wheel_.Animating())
                    HandleAction(Action::Select);
                return;
            }
            if (hit < 0) return;
            int delta = hit - sel;
            if (delta > n / 2) delta -= n;
            if (delta < -n / 2) delta += n;
            wheel_.Move(delta);
            selectionChangedAt_ = now;
            return;
        }
        case State::Menu:
            for (size_t i = 0; i < menuRects_.size(); ++i)
                if (Inside(menuRects_[i], p)) {
                    menuIndex_ = (int)i;
                    MenuAction(Action::Select);
                    return;
                }
            if (!Inside(menuBox_, p)) MenuAction(Action::Back);
            return;
        case State::ProfileSelect:
            for (size_t i = 0; i < profileRects_.size(); ++i)
                if (Inside(profileRects_[i], p)) {
                    profileIndex_ = (int)i;
                    ProfileSelectAction(Action::Select);
                    return;
                }
            ProfileSelectAction(Action::Back);
            return;
        case State::LetterSelect:
            for (size_t i = 0; i < letterRects_.size(); ++i)
                if (Inside(letterRects_[i], p)) {
                    letterIndex_ = (int)i;
                    LetterSelectAction(Action::Select);
                    return;
                }
            LetterSelectAction(Action::Back);
            return;
        case State::QuitConfirm:
            if (Inside(quitYesRect_, p)) running_ = false;
            else state_ = State::Browse;
            return;
        default: return;
    }
}

void App::SetGunView(const std::string& v) {
    if (settings_.gun.view == v) return;
    settings_.gun.view = v;
    SaveSettings();
    selectionChangedAt_ = SDL_GetTicks();
}

void App::SelectIndex(int idx) {
    const int n = (int)items_.size();
    if (n == 0) return;
    idx = ((idx % n) + n) % n;
    wheel_.Reset(n, idx);  // sans animation : la disposition pistolet n'affiche pas la bande
    selectionChangedAt_ = SDL_GetTicks();
}

bool App::GunBrowseShoot(SDL_FPoint p, uint64_t now) {
    const int n = (int)items_.size();
    if (n == 0) return false;
    const int sel = wheel_.Selected();
    if (Mosaic()) {
        const int per = settings_.gun.mosaicCols * settings_.gun.mosaicRows;
        const int start = sel / per * per;
        if (Inside(gunLeftRect_, p)) {  // page précédente (la dernière depuis la première)
            arrowShotAt_[0] = now;
            int prev = start - per;
            SelectIndex(prev < 0 ? (n - 1) / per * per : prev);
            return true;
        }
        if (Inside(gunRightRect_, p)) {
            arrowShotAt_[1] = now;
            SelectIndex(start + per >= n ? 0 : start + per);
            return true;
        }
        for (const auto& [r, idx] : tileRects_)
            if (Inside(r, p)) {  // case touchée : le jeu passe en grand ; un tir sur l'image le lancera
                SelectIndex(idx);
                SetGunView("single");
                return true;
            }
        return false;
    }
    if (Inside(gunLeftRect_, p)) {
        arrowShotAt_[0] = now;
        SelectIndex(sel - 1);
        return true;
    }
    if (Inside(gunRightRect_, p)) {
        arrowShotAt_[1] = now;
        SelectIndex(sel + 1);
        return true;
    }
    if (Inside(gunCenterRect_, p)) {
        // Lancement seulement si le jeu est affiché depuis un moment : deux tirs rapprochés ne lancent rien par erreur.
        if (Elapsed(now, selectionChangedAt_) >= (uint64_t)settings_.gun.launchGuardMs) HandleAction(Action::Select);
        return true;
    }
    return false;
}

void App::DrawArrowBand(const SDL_FRect& r, bool left, int previewIndex, const char* label, float arrowY) {
    const float H = pointerArea_.y;
    const uint64_t now = SDL_GetTicks();
    const bool hover = GunMode() && state_ == State::Browse && Inside(r, pointer_);
    // Retour visuel du tir : éclair bref et léger grossissement du bouton.
    float flash = 0.0f;
    if (uint64_t at = arrowShotAt_[left ? 0 : 1]; at && Elapsed(now, at) < 320) flash = 1.0f - Elapsed(now, at) / 320.0f;
    const Color black{0, 0, 0, 255}, white{255, 255, 255, 255};

    // Bande : verre sombre arrondi, lueur d'accent depuis le bord extérieur.
    const float rad = std::min(r.w * 0.16f, H * 0.022f);
    shapes::FillRound(renderer_, r, rad, black, 0.42f);
    const float glow = (hover ? 0.34f : 0.12f) + 0.45f * flash;
    shapes::FillRoundGradientH(renderer_, r, rad, theme_.accent, left ? glow : 0.0f, theme_.accent, left ? 0.0f : glow);
    shapes::StrokeRound(renderer_, r, rad, std::max(1.0f, H * 0.0015f), hover ? theme_.accent : white, hover ? 0.7f : 0.08f);

    // Bouton rond avec double chevron animé (glisse vers la direction pour inviter au tir).
    const float d = std::min(r.w * 0.72f, H * 0.13f) * (hover ? 1.08f : 1.0f) * (1.0f + 0.1f * flash);
    const float cx = r.x + r.w / 2;
    const SDL_FRect disc{cx - d / 2, arrowY - d / 2, d, d};
    shapes::FillRound(renderer_, {disc.x, disc.y + d * 0.07f, d, d}, d / 2, black, 0.5f);  // ombre portée
    shapes::FillRound(renderer_, disc, d / 2, hover ? theme_.accent : Color{16, 18, 26, 255}, hover ? 1.0f : 0.92f);
    if (!hover) shapes::StrokeRound(renderer_, disc, d / 2, std::max(2.0f, d * 0.05f), theme_.accent, 0.95f);
    if (flash > 0) shapes::FillRound(renderer_, disc, d / 2, white, 0.55f * flash);
    const float dir = left ? -1.0f : 1.0f;
    const float cycle = std::fmod(now / 900.0f, 1.0f);                    // 0 -> 1 en boucle
    const float slide = (hover ? cycle : 0.5f + 0.5f * std::sin(now / 380.0f)) * d * 0.07f;
    const Color cc = hover ? Color{14, 14, 20, 255} : theme_.accent;
    const float s = d * 0.17f, t = d * 0.085f;
    shapes::Chevron(renderer_, cx + dir * (d * 0.08f + slide), arrowY, s, t, left, cc, 1.0f);
    shapes::Chevron(renderer_, cx + dir * (-d * 0.1f + slide), arrowY, s, t, left, cc, 0.4f);

    // Libellé sous le bouton
    TTF_Font* small = text_->Font(theme_.fontBody, (int)(theme_.smallSize * H));
    float y = disc.y + d + H * 0.018f;
    y += text_->Draw(small, label, cx, y, hover ? theme_.text : theme_.textDim, Align::Center, (int)r.w).y;

    // Aperçu du jeu voisin : carte avec fond assombri, logo et nom.
    if (previewIndex < 0 || previewIndex >= (int)items_.size()) return;
    const WheelItem& it = items_[(size_t)previewIndex];
    const float cw = r.w * 0.84f, ch = cw * 0.78f;
    SDL_FRect card{cx - cw / 2, y + H * 0.03f, cw, ch};
    if (card.y + ch + H * 0.05f > r.y + r.h) return;  // pas la place
    shapes::FillRound(renderer_, card, H * 0.01f, Color{12, 14, 20, 255}, 0.9f);
    SDL_FRect in{card.x + 3, card.y + 3, card.w - 6, card.h - 6};
    if (it.kind == ItemKind::Game)
        if (const Game* g = lib_.FindGame(it.id))
            if (SDL_Texture* fan = textures_->Get(lib_.FanartFor(*g))) {
                DrawImageFit(fan, in, hover ? 0.6f : 0.4f, 0, true);
                FillRect(in, black, 0.2f);
            }
    DrawItemVisual(it, {in.x + in.w * 0.08f, in.y + in.h * 0.1f, in.w * 0.84f, in.h * 0.8f}, hover ? 1.0f : 0.75f, 0.0, false);
    shapes::StrokeRound(renderer_, card, H * 0.01f, 1.5f, hover ? theme_.accent : white, hover ? 0.8f : 0.12f);
    // Nom sur une ligne, raccourci au besoin (sans couper un caractère UTF-8).
    std::string name = ItemTitle(it);
    const float maxW = r.w * 0.92f;
    if (text_->Measure(small, name).x > maxW) {
        while (!name.empty() && text_->Measure(small, name + "…").x > maxW) {
            name.pop_back();
            while (!name.empty() && ((unsigned char)name.back() & 0xC0) == 0x80) name.pop_back();
            if (!name.empty() && ((unsigned char)name.back() & 0xC0) == 0xC0) name.pop_back();
        }
        name += "…";
    }
    text_->Draw(small, name, cx, card.y + ch + H * 0.01f, theme_.textDim, Align::Center, 0, hover ? 1.0f : 0.8f);
}

void App::DrawGunBrowse(float W, float H) {
    tileRects_.clear();
    const int n = (int)items_.size();
    const float top = H * 0.095f, bottom = H * 0.97f;
    const float bandW = W * 0.1f, gap = W * 0.015f;
    gunLeftRect_ = {gap, top, bandW, bottom - top};
    gunRightRect_ = {W - gap - bandW, top, bandW, bottom - top};
    const float cx0 = gunLeftRect_.x + bandW + gap, cw = gunRightRect_.x - gap - cx0;
    TTF_Font* title = text_->Font(theme_.fontTitle, (int)(theme_.titleSize * H));
    TTF_Font* body = text_->Font(theme_.fontBody, (int)(theme_.bodySize * H));
    TTF_Font* small = text_->Font(theme_.fontBody, (int)(theme_.smallSize * H));
    const Color black{0, 0, 0, 255}, white{255, 255, 255, 255};
    if (n == 0) {
        gunCenterRect_ = {};
        text_->Draw(title, "Catégorie vide", W / 2, H * 0.45f, theme_.text, Align::Center);
        return;
    }
    const int sel = wheel_.Selected();
    const uint64_t now = SDL_GetTicks();
    const float fade = std::min(1.0f, (now - selectionChangedAt_) / 200.0f);
    // Ombre douce sous un panneau (quelques couches translucides décalées vers le bas).
    auto shadow = [&](const SDL_FRect& r, float rad) {
        for (int i = 1; i <= 4; ++i) {
            float e = i * H * 0.004f;
            shapes::FillRound(renderer_, {r.x - e, r.y - e + H * 0.008f, r.w + 2 * e, r.h + 2 * e}, rad + e, black, 0.07f);
        }
    };

    if (Mosaic()) {
        const int cols = settings_.gun.mosaicCols, rows = settings_.gun.mosaicRows, per = cols * rows;
        const int start = sel / per * per, pages = (n + per - 1) / per, page = start / per;
        const float gridTop = top, gridH = bottom - top - H * 0.075f, tg = W * 0.01f;
        const float midY = gridTop + gridH / 2;
        DrawArrowBand(gunLeftRect_, true, -1, "Page précédente", midY);
        DrawArrowBand(gunRightRect_, false, -1, "Page suivante", midY);
        gunCenterRect_ = {};
        const float tw = (cw - tg * (cols - 1)) / cols, th = (gridH - tg * (rows - 1)) / rows;
        const float rad = H * 0.012f;
        for (int i = 0; i < per && start + i < n; ++i) {
            const int idx = start + i;
            SDL_FRect r{cx0 + (i % cols) * (tw + tg), gridTop + (i / cols) * (th + tg), tw, th};
            tileRects_.push_back({r, idx});
            const WheelItem& it = items_[(size_t)idx];
            const bool hover = Inside(r, pointer_) && state_ == State::Browse;
            SDL_FRect d = r;  // case visée : légèrement agrandie
            if (hover) d = {r.x - r.w * 0.025f, r.y - r.h * 0.025f, r.w * 1.05f, r.h * 1.05f};
            shadow(d, rad);
            shapes::FillRound(renderer_, d, rad, Color{14, 16, 22, 255});
            SDL_FRect in{d.x + 4, d.y + 4, d.w - 8, d.h - 8};
            bool hasFan = false;
            if (it.kind == ItemKind::Game)
                if (const Game* g = lib_.FindGame(it.id))
                    if (SDL_Texture* fan = textures_->Get(lib_.FanartFor(*g))) {
                        DrawImageFit(fan, in, hover ? 0.8f : 0.5f, 0, true);
                        hasFan = true;
                    }
            if (hasFan) shapes::Gradient(renderer_, in, black, 0.0f, black, 0.55f, false);
            DrawItemVisual(it, {in.x + in.w * 0.08f, in.y + in.h * 0.12f, in.w * 0.84f, in.h * 0.76f}, 1.0f, 0.0, true);
            if (idx == sel) shapes::StrokeRound(renderer_, d, rad, std::max(3.0f, H * 0.005f), theme_.accent);
            else if (hover) shapes::StrokeRound(renderer_, d, rad, 2.0f, white, 0.85f);
            else shapes::StrokeRound(renderer_, d, rad, 1.0f, white, 0.08f);
        }
        // Pages : une pastille par page (la page courante allongée), puis l'aide.
        const float py = bottom - H * 0.01f;
        if (pages > 1 && pages <= 20) {
            const float dot = H * 0.011f, dg = dot * 0.9f, longW = dot * 3.0f;
            float total = (pages - 1) * (dot + dg) + longW, x = W / 2 - total / 2;
            for (int p = 0; p < pages; ++p) {
                float w = p == page ? longW : dot;
                shapes::FillRound(renderer_, {x, py - dot / 2, w, dot}, dot / 2, p == page ? theme_.accent : white,
                                  p == page ? 1.0f : 0.3f);
                x += w + dg;
            }
        }
        text_->Draw(small, pages > 1 ? std::format("Page {} / {}  ·  tirez sur un jeu pour l'afficher en grand", page + 1, pages)
                                     : std::string("Tirez sur un jeu pour l'afficher en grand"),
                    W / 2, gridTop + gridH + H * 0.012f, theme_.textDim, Align::Center);
        return;
    }

    // Grande image : média au centre, nom, infos et résumé dessous, flèches sur les côtés (aperçu des voisins).
    const float mediaH = (bottom - top) * 0.72f;
    gunCenterRect_ = {cx0, top, cw, mediaH};
    DrawArrowBand(gunLeftRect_, true, n > 1 ? (sel - 1 + n) % n : -1, "Précédent", top + mediaH / 2);
    DrawArrowBand(gunRightRect_, false, n > 1 ? (sel + 1) % n : -1, "Suivant", top + mediaH / 2);
    const bool hoverMedia = Inside(gunCenterRect_, pointer_) && state_ == State::Browse;
    const float rad = H * 0.012f;
    shadow(gunCenterRect_, rad);
    shapes::FillRound(renderer_, gunCenterRect_, rad, Color{10, 12, 16, 255});
    const WheelItem& it = items_[(size_t)sel];
    const SDL_FRect media{cx0 + 5, top + 5, cw - 10, mediaH - 10};
    DrawMediaBox(it, media, fade);
    // Bas de l'image assombri : le logo et le bouton restent lisibles sur une image claire.
    shapes::Gradient(renderer_, {media.x, media.y + media.h * 0.6f, media.w, media.h * 0.4f}, black, 0.0f, black, 0.55f, false);
    if (it.kind == ItemKind::Game)
        if (SDL_Texture* logo = textures_->Get(ItemLogoPath(it)))
            DrawImageFit(logo, {cx0 + cw * 0.025f, top + mediaH * 0.7f, cw * 0.28f, mediaH * 0.26f}, fade);
    shapes::StrokeRound(renderer_, gunCenterRect_, rad, hoverMedia ? std::max(3.0f, H * 0.005f) : 1.5f,
                        hoverMedia ? theme_.accent : white, hoverMedia ? 1.0f : 0.12f);

    // Bouton « Jouer » sur l'image. Visée : il s'allume ; la jauge montre le délai de garde avant qu'un tir lance.
    if (state_ == State::Browse) {
        const char* verb = it.kind == ItemKind::Game ? "Tirez pour jouer" : "Tirez pour ouvrir";
        const SDL_FPoint ts = text_->Measure(body, verb);
        const float ph = H * 0.06f, tw = ts.x;
        const float pw = tw + ph * 1.6f;
        const SDL_FRect pill{media.x + media.w / 2 - pw / 2, media.y + media.h - ph - H * 0.025f, pw, ph};
        const float guard = std::clamp(Elapsed(now, selectionChangedAt_) / (float)std::max(1, settings_.gun.launchGuardMs), 0.0f, 1.0f);
        const float pulse = 0.75f + 0.25f * std::sin(now / 400.0f);
        shapes::FillRound(renderer_, pill, ph / 2, black, 0.6f);
        if (hoverMedia) {
            shapes::FillRound(renderer_, {pill.x, pill.y, pill.w * guard, pill.h}, ph / 2, theme_.accent, guard < 1.0f ? 0.55f : 1.0f);
        }
        shapes::StrokeRound(renderer_, pill, ph / 2, 2.0f, theme_.accent, hoverMedia ? 1.0f : pulse);
        const Color fg = hoverMedia && guard >= 1.0f ? Color{14, 14, 20, 255} : theme_.text;
        shapes::Play(renderer_, pill.x + ph * 0.62f, pill.y + ph / 2, ph * 0.42f, hoverMedia && guard >= 1.0f ? fg : theme_.accent);
        text_->Draw(body, verb, pill.x + ph * 1.05f, pill.y + (ph - ts.y) / 2, fg);
    }

    // Nom, infos, début du résumé ; à droite la position dans la liste.
    const float infoW = cw * 0.7f;
    float y = top + mediaH + H * 0.018f;
    y += text_->Draw(title, ItemTitle(it), cx0, y, theme_.text, Align::Left, (int)infoW).y;
    std::string meta, summary;
    if (it.kind == ItemKind::Game) {
        if (const Game* g = lib_.FindGame(it.id)) {
            auto add = [&](const std::string& s) {
                if (s.empty()) return;
                if (!meta.empty()) meta += "   ·   ";
                meta += s;
            };
            if (g->year > 0) add(std::to_string(g->year));
            add(g->publisher);
            add(g->genre);
            summary = g->description;
            for (char& ch : summary)  // résumé sur des lignes continues
                if (ch == '\n' || ch == '\r' || ch == '\t') ch = ' ';
            for (size_t k; (k = summary.find("  ")) != std::string::npos;) summary.erase(k, 1);
        }
    } else {
        meta = std::format("{} jeu(x)", lib_.CountGames(it.id));
    }
    if (!meta.empty()) y += text_->Draw(body, meta, cx0, y + 2, theme_.accent, Align::Left, (int)infoW).y + 6;
    if (!summary.empty() && y + theme_.smallSize * H * 2.6f < bottom) {
        // Résumé limité à environ deux lignes, coupé à un mot.
        const size_t maxChars = (size_t)std::max(40.0f, infoW / (theme_.smallSize * H * 0.5f) * 1.9f);
        if (summary.size() > maxChars) {
            size_t cut = summary.rfind(' ', maxChars);
            if (cut == std::string::npos) {  // pas d'espace : coupe sans casser un caractère UTF-8
                cut = maxChars;
                while (cut > 0 && ((unsigned char)summary[cut] & 0xC0) == 0x80) --cut;
            }
            summary = summary.substr(0, cut) + "…";
        }
        text_->Draw(small, summary, cx0, y, theme_.textDim, Align::Left, (int)infoW);
    }
    // Position : « 9 / 10 » et barre de progression.
    const float rx = cx0 + cw, ry = top + mediaH + H * 0.03f, bw = cw * 0.18f;
    text_->Draw(body, std::format("{} / {}", sel + 1, n), rx, ry, theme_.text, Align::Right);
    const float by = ry + theme_.bodySize * H * 1.6f, bh = std::max(3.0f, H * 0.006f);
    shapes::FillRound(renderer_, {rx - bw, by, bw, bh}, bh / 2, white, 0.15f);
    shapes::FillRound(renderer_, {rx - bw, by, bw * (sel + 1) / (float)n, bh}, bh / 2, theme_.accent);
}

void App::UpdateHolds(uint64_t now) {
    // Clic droit maintenu (pistolet) : menu de la borne, comme un appui long sur Retour.
    if (gunBackDownAt_ && !gunBackHoldFired_ && Elapsed(now, gunBackDownAt_) >= (uint64_t)input_.Timing().holdMs) {
        gunBackHoldFired_ = true;
        if (state_ == State::Browse) OpenMenu();
    }
    // Combinaison pour quitter : Retour maintenu 5 secondes (bouton ou clic droit), confirmation ensuite.
    bool held = input_.ActionHeld(Action::Back) || gunBackDownAt_ != 0;
    if (held && !admin_->IsOpen() && (state_ == State::Browse || state_ == State::Menu)) {
        if (!backHeldSince_) backHeldSince_ = now;
        else if (Elapsed(now, backHeldSince_) >= 5000) {
            state_ = State::QuitConfirm;
            backHeldSince_ = 0;
            gunBackDownAt_ = 0;
            input_.Clear();
            logx::Info("Retour maintenu 5 s : demande de fermeture");
        }
    } else {
        backHeldSince_ = 0;
    }
}

// ---------------------------------------------------------------- Profils

void App::ActivateProfile(const std::string& wanted) {
    if (!profileName_.empty()) SaveNavState();  // état du profil quitté
    std::vector<std::string> list = Profile::List(paths_.ProfilesDir());
    std::string name = wanted;
    if (std::find(list.begin(), list.end(), name) == list.end()) name = list.empty() ? "Racecab" : list.front();
    if (!profile_.Load(paths_.ProfileFile(name))) {
        profile_ = Profile{};
        profile_.Save(paths_.ProfileFile(name));
    }
    profile_.name = name;
    profileName_ = name;
    settings_.profile = name;
    settings_.Save(paths_.SettingsFile());

    theme_.Load(paths_.ThemesDir(), profile_.theme);
    input_.ResetDefaults();
    if (!input_.Load(paths_.InputFileFor(name))) input_.Save(paths_.InputFileFor(name));
    lib_.SetProfileFilter(profile_.hiddenCategories, profile_.filterGames, profile_.showAutoCategories);
    lib_.SetSort(profile_.sort);
    if (video_) video_->Stop();

    items_.clear();
    lastSelection_.clear();
    NavState nav;
    if (nav.Load(paths_.StateFileFor(name)) && lib_.NodeExists(nav.node))
        EnterNode(nav.node, WheelItem{(ItemKind)nav.itemKind, nav.itemId});
    else
        EnterNode(Library::kAll, std::nullopt);
    idleSince_ = SDL_GetTicks();
    logx::Info("Profil actif : {} (thème {}, {} catégorie(s) masquée(s))", name, profile_.theme, profile_.hiddenCategories.size());
}

void App::SaveProfile() {
    profile_.Save(paths_.ProfileFile(profileName_));
    lib_.SetProfileFilter(profile_.hiddenCategories, profile_.filterGames, profile_.showAutoCategories);
    lib_.SetSort(profile_.sort);
    RefreshItems();
}

void App::OpenProfileSelect() {
    profileList_ = ProfilesForMode();
    if (profileList_.empty()) return;
    auto it = std::find(profileList_.begin(), profileList_.end(), profileName_);
    profileIndex_ = it == profileList_.end() ? 0 : (int)(it - profileList_.begin());
    state_ = State::ProfileSelect;
}

void App::ProfileSelectAction(Action a) {
    int n = (int)profileList_.size();
    switch (a) {
        case Action::Prev:
        case Action::CatPrev: profileIndex_ = (profileIndex_ + n - 1) % n; break;
        case Action::Next:
        case Action::CatNext: profileIndex_ = (profileIndex_ + 1) % n; break;
        case Action::Select:
            state_ = State::Browse;
            if (profileList_[(size_t)profileIndex_] != profileName_) {
                ActivateProfile(profileList_[(size_t)profileIndex_]);
                Toast("Profil : " + profileName_);
            }
            break;
        case Action::Back:
        case Action::Menu: state_ = State::Browse; break;
        default: break;
    }
}

// ---------------------------------------------------------------- Mode démonstration (attract)

void App::UpdateAttract(uint64_t now) {
    // L'inactivité ne compte que sur l'écran de navigation (pas dans un menu, l'administration ou un jeu).
    if (admin_->IsOpen() || (state_ != State::Browse && state_ != State::Attract)) {
        idleSince_ = now;
        return;
    }
    const AttractSettings& a = profile_.attract;
    if (state_ == State::Browse) {
        // now peut être légèrement antérieur à idleSince_ (horodaté pendant la frame) : pas de soustraction négative.
        if (a.enabled && Elapsed(now, idleSince_) >= (uint64_t)a.idleSeconds * 1000) StartAttract(now);
        return;
    }
    if (state_ != State::Attract || now < attractNextAt_ || items_.size() < 2) return;
    if (a.random) {
        int cur = wheel_.Selected(), n = (int)items_.size();
        int target = std::uniform_int_distribution<int>(0, n - 2)(rng_);
        if (target >= cur) ++target;  // jamais le même jeu deux fois de suite
        int delta = target - cur;
        if (delta > n / 2) delta -= n;  // chemin le plus court sur la roue
        if (delta < -n / 2) delta += n;
        wheel_.Move(delta);
    } else {
        wheel_.Move(1);
    }
    selectionChangedAt_ = now;
    attractNextAt_ = now + (uint64_t)a.displaySeconds * 1000;
    if (const Game* g = lib_.FindGame(items_[(size_t)wheel_.Selected()].id)) logx::Info("Démonstration : {}", g->name);
}

void App::StartAttract(uint64_t now) {
    std::vector<int64_t> ids = lib_.AttractGames(profile_.attract.categories);
    if (ids.empty()) {  // rien à montrer : on réessaiera après un nouveau délai
        idleSince_ = now;
        return;
    }
    std::sort(ids.begin(), ids.end(), [&](int64_t x, int64_t y) {
        return util::ToLower(lib_.FindGame(x)->name) < util::ToLower(lib_.FindGame(y)->name);
    });
    attractSavedNode_ = node_;
    attractSavedItem_.reset();
    if (WheelItem* it = SelectedItem()) attractSavedItem_ = *it;
    items_.clear();
    for (int64_t id : ids) items_.push_back({ItemKind::Game, id});
    int start = profile_.attract.random ? std::uniform_int_distribution<int>(0, (int)items_.size() - 1)(rng_) : 0;
    wheel_.Reset((int)items_.size(), start);
    video_->Stop();
    state_ = State::Attract;
    selectionChangedAt_ = now;
    attractNextAt_ = now + (uint64_t)profile_.attract.displaySeconds * 1000;
    logx::Info("Mode démonstration : {} jeu(x)", items_.size());
}

void App::ExitAttract() {
    // Reprise immédiate, sans animation : liste et sélection d'avant la démo.
    state_ = State::Browse;
    video_->Stop();
    node_ = lib_.NodeExists(attractSavedNode_) ? attractSavedNode_ : Library::kAll;
    items_ = lib_.Items(node_);
    int idx = 0;
    if (attractSavedItem_) {
        auto f = std::find(items_.begin(), items_.end(), *attractSavedItem_);
        if (f != items_.end()) idx = (int)(f - items_.begin());
    }
    wheel_.Reset((int)items_.size(), idx);
    const uint64_t now = SDL_GetTicks();
    selectionChangedAt_ = now;
    idleSince_ = now;
    input_.Clear();
    input_.BlockUntil(now + 250);
    logx::Info("Mode démonstration interrompu");
}

void App::HandleInput(const InputEvent& ev) {
    if (ev.hold) {
        // Maintenir Retour : menu de la borne (utile s'il n'y a pas de bouton Menu dédié)
        if (ev.action == Action::Back && state_ == State::Browse) OpenMenu();
        return;
    }
    HandleAction(ev.action);
}

// ---------------------------------------------------------------- Menu de la borne

void App::OpenMenu() {
    if (state_ != State::Browse) return;
    menu_.clear();
    if (WheelItem* it = SelectedItem(); it && it->kind == ItemKind::Game) {
        int64_t id = it->id;
        const Game* g = lib_.FindGame(id);
        menu_.push_back({g && g->favorite ? "Retirer des favoris" : "Ajouter aux favoris", [this, id] {
                             if (lib_.ToggleFavorite(id) && node_ == Library::kFavorites) RefreshItems();
                         }});
    }
    menu_.push_back({"Aller à une lettre", [this] { OpenLetterSelect(); }});
    menu_.push_back({std::string("Tri : ") + SortLabel(profile_.sort) + " (changer)", [this] { CycleSort(); }});
    if (ProfilesForMode().size() > 1)
        menu_.push_back({"Profil : " + profileName_ + " (changer)", [this] { OpenProfileSelect(); }});
    menu_.push_back({"Configurer les commandes (assistant)", [this] { StartWizard(); }});
    menu_.push_back({std::string("Affichage : ") + (theme_.wheel.mode == "strip" ? "bande -> roue" : "roue -> bande"), [this] {
                         theme_.wheel.mode = theme_.wheel.mode == "strip" ? "wheel" : "strip";
                         theme_.Save();
                     }});
    menu_.push_back({"Administration (clavier / souris)", [this] { admin_->Open(); }});
    if (!settings_.kiosk)  // kiosque : quitter seulement par Retour maintenu 5 s ou l'administration
        menu_.push_back({"Quitter le frontend", [this] { state_ = State::QuitConfirm; }});
    menu_.push_back({"Fermer le menu", [] {}});
    menuIndex_ = 0;
    state_ = State::Menu;
}

void App::CycleSort() {
    static const char* kOrder[] = {"name", "year", "played", "count"};
    size_t i = 0;
    while (i < 4 && profile_.sort != kOrder[i]) ++i;
    profile_.sort = kOrder[(i + 1) % 4];
    SaveProfile();
    Toast(std::string("Tri : ") + SortLabel(profile_.sort));
}

void App::OpenLetterSelect() {
    letters_.clear();
    for (const WheelItem& it : items_) {
        char c = LetterOf(ItemTitle(it));
        if (std::find(letters_.begin(), letters_.end(), c) == letters_.end()) letters_.push_back(c);
    }
    if (letters_.size() < 2) {
        Toast("Une seule lettre dans cette liste");
        return;
    }
    std::sort(letters_.begin(), letters_.end());
    letterIndex_ = 0;
    if (WheelItem* it = SelectedItem()) {
        auto f = std::find(letters_.begin(), letters_.end(), LetterOf(ItemTitle(*it)));
        if (f != letters_.end()) letterIndex_ = (int)(f - letters_.begin());
    }
    state_ = State::LetterSelect;
}

void App::LetterSelectAction(Action a) {
    int n = (int)letters_.size();
    switch (a) {
        case Action::Prev:
        case Action::CatPrev: letterIndex_ = (letterIndex_ + n - 1) % n; break;
        case Action::Next:
        case Action::CatNext: letterIndex_ = (letterIndex_ + 1) % n; break;
        case Action::Select: {
            state_ = State::Browse;
            char c = letters_[(size_t)letterIndex_];
            for (size_t i = 0; i < items_.size(); ++i)
                if (LetterOf(ItemTitle(items_[i])) == c) {
                    wheel_.Reset((int)items_.size(), (int)i);
                    selectionChangedAt_ = SDL_GetTicks();
                    break;
                }
            break;
        }
        case Action::Back:
        case Action::Menu: state_ = State::Browse; break;
        default: break;
    }
}

void App::MenuAction(Action a) {
    int n = (int)menu_.size();
    switch (a) {
        case Action::Prev:
        case Action::CatPrev: menuIndex_ = (menuIndex_ + n - 1) % n; break;
        case Action::Next:
        case Action::CatNext: menuIndex_ = (menuIndex_ + 1) % n; break;
        case Action::Select: {
            state_ = State::Browse;  // l'entrée peut changer d'état (assistant, quitter...)
            auto run = menu_[(size_t)menuIndex_].run;
            run();
            break;
        }
        case Action::Back:
        case Action::Menu: state_ = State::Browse; break;
        default: break;
    }
}

// ---------------------------------------------------------------- Assistant de configuration

void App::StartWizard() {
    if (admin_->IsOpen()) admin_->Close();
    wiz_ = Wizard{};
    wiz_.backup = input_.Bindings();
    wiz_.since = SDL_GetTicks();
    state_ = State::Wizard;
    logx::Info("Assistant de configuration des commandes démarré");
}

void App::WizardAbort() {
    input_.EndCapture();
    input_.SetBindings(wiz_.backup);
    state_ = State::Browse;
    input_.BlockUntil(SDL_GetTicks() + 300);
    Toast("Configuration des commandes annulée");
    logx::Info("Assistant annulé : mapping précédent conservé");
}

void App::WizardUpdate(uint64_t now) {
    constexpr uint64_t kListenMs = 10000, kShowMs = 800, kReleaseMaxMs = 3000;
    switch (wiz_.phase) {
        case Wizard::Phase::WaitRelease:
            // On attend que tout soit relâché pour ne pas réutiliser l'appui précédent.
            if (!input_.AnyRawActive() || Elapsed(now, wiz_.since) > kReleaseMaxMs) {
                input_.BeginCapture();
                wiz_.phase = Wizard::Phase::Listen;
                wiz_.since = now;
            }
            break;
        case Wizard::Phase::Listen: {
            Action cur = (Action)wiz_.step;
            if (auto b = input_.TakeCaptured()) {
                b->action = cur;
                auto dup = std::find_if(wiz_.results.begin(), wiz_.results.end(), [&](const Binding& r) { return r.SameInput(*b); });
                if (dup != wiz_.results.end()) {
                    wiz_.warning = std::string("Déjà utilisée pour « ") + ActionLabel(dup->action) + " » : choisissez une autre commande.";
                    wiz_.phase = Wizard::Phase::WaitRelease;
                    wiz_.since = now;
                    break;
                }
                wiz_.warning.clear();
                wiz_.results.push_back(*b);
                wiz_.got = b;
                wiz_.phase = Wizard::Phase::Show;
                wiz_.since = now;
            } else if (Elapsed(now, wiz_.since) > kListenMs) {
                wiz_.warning.clear();
                wiz_.got.reset();  // rien : on conserve l'existant
                wiz_.phase = Wizard::Phase::Show;
                wiz_.since = now;
            }
            break;
        }
        case Wizard::Phase::Show:
            if (Elapsed(now, wiz_.since) < kShowMs) break;
            input_.EndCapture();
            if (++wiz_.step >= (int)Action::Count) {
                // Application en une fois : la navigation n'est jamais à moitié reconfigurée.
                input_.SetBindings(wiz_.backup);
                for (const Binding& b : wiz_.results) input_.ReplaceFamily(b);
                input_.Save(paths_.InputFileFor(profileName_));
                state_ = State::Browse;
                input_.BlockUntil(now + 500);
                Toast(std::format("Commandes enregistrées ({} modifiée(s))", wiz_.results.size()));
                logx::Info("Assistant terminé : {} association(s) modifiée(s)", wiz_.results.size());
                return;
            }
            wiz_.phase = Wizard::Phase::WaitRelease;
            wiz_.since = now;
            wiz_.got.reset();
            break;
    }
}

void App::HandleAction(Action a) {
    if (state_ == State::Menu) {
        MenuAction(a);
        return;
    }
    if (state_ == State::ProfileSelect) {
        ProfileSelectAction(a);
        return;
    }
    if (state_ == State::LetterSelect) {
        LetterSelectAction(a);
        return;
    }
    if (state_ == State::Wizard) return;  // l'assistant lit les entrées brutes
    if (state_ == State::QuitConfirm) {
        if (a == Action::Select) running_ = false;
        else if (a == Action::Back || a == Action::Quit) state_ = State::Browse;
        return;
    }
    if (state_ == State::Launching) {  // Retour : retire l'écran de chargement sans attendre la fenêtre du jeu
        Launcher::Phase ph = launcher_.CurrentPhase();
        if (a == Action::Back && (ph == Launcher::Phase::Running || ph == Launcher::Phase::Watching))
            LeaveLaunchScreen(nullptr, "Retour");
        return;
    }
    if (state_ != State::Browse) return;

    switch (a) {
        case Action::Prev:
        case Action::Next:
            wheel_.Move(a == Action::Next ? 1 : -1);
            selectionChangedAt_ = SDL_GetTicks();
            break;
        case Action::CatPrev:
        case Action::CatNext: {
            if (node_ == Library::kRoot) break;
            std::vector<int64_t> sib = lib_.Siblings(node_);
            if (sib.size() < 2) break;
            auto pos = std::find(sib.begin(), sib.end(), node_) - sib.begin();
            int64_t n = (int64_t)sib.size();
            int64_t next = sib[(size_t)(((pos + (a == Action::CatNext ? 1 : -1)) % n + n) % n)];
            EnterNode(next, std::nullopt);
            break;
        }
        case Action::Select: {
            WheelItem* it = SelectedItem();
            if (!it) break;
            if (Mosaic()) {  // mosaïque : le jeu choisi passe en grand, un second Valider le lance
                SetGunView("single");
                break;
            }
            if (it->kind == ItemKind::Category) EnterNode(it->id, std::nullopt);
            else LaunchGame(it->id);
            break;
        }
        case Action::Back:
            if (node_ != Library::kRoot) EnterNode(lib_.ParentOf(node_), WheelItem{ItemKind::Category, node_});
            break;
        case Action::Favorite: {
            WheelItem* it = SelectedItem();
            if (!it || it->kind != ItemKind::Game) break;
            if (lib_.ToggleFavorite(it->id)) {
                const Game* g = lib_.FindGame(it->id);
                Toast(g->favorite ? "Ajouté aux favoris" : "Retiré des favoris");
                if (node_ == Library::kFavorites) RefreshItems();
            }
            break;
        }
        case Action::Menu: OpenMenu(); break;
        case Action::Quit: state_ = State::QuitConfirm; break;
        default: break;
    }
}

WheelItem* App::SelectedItem() {
    if (items_.empty()) return nullptr;
    return &items_[(size_t)wheel_.Selected()];
}

void App::EnterNode(int64_t node, std::optional<WheelItem> select) {
    if (WheelItem* cur = SelectedItem()) lastSelection_[node_] = *cur;
    node_ = node;
    items_ = lib_.Items(node);
    if (!select) {
        auto it = lastSelection_.find(node);
        if (it != lastSelection_.end()) select = it->second;
    }
    int idx = 0;
    if (select) {
        auto f = std::find(items_.begin(), items_.end(), *select);
        if (f != items_.end()) idx = (int)(f - items_.begin());
    }
    wheel_.Reset((int)items_.size(), idx);
    selectionChangedAt_ = SDL_GetTicks();
}

void App::RefreshItems() {
    std::optional<WheelItem> cur;
    if (WheelItem* it = SelectedItem()) cur = *it;
    int oldIdx = wheel_.Selected();
    if (!lib_.NodeExists(node_)) node_ = Library::kAll;
    items_ = lib_.Items(node_);
    int idx = std::min(oldIdx, std::max(0, (int)items_.size() - 1));
    if (cur) {
        auto f = std::find(items_.begin(), items_.end(), *cur);
        if (f != items_.end()) idx = (int)(f - items_.begin());
    }
    wheel_.Reset((int)items_.size(), idx);
}

void App::SaveNavState() {
    if (profileName_.empty()) return;
    NavState s;
    if (state_ == State::Attract) {  // pendant la démo, on garde la position de l'utilisateur
        s.node = attractSavedNode_;
        if (attractSavedItem_) {
            s.itemKind = (int)attractSavedItem_->kind;
            s.itemId = attractSavedItem_->id;
        }
    } else {
        s.node = node_;
        if (WheelItem* it = SelectedItem()) {
            s.itemKind = (int)it->kind;
            s.itemId = it->id;
        }
    }
    s.Save(paths_.StateFileFor(profileName_));
}

void App::LaunchGame(int64_t id) {
    const Game* g = lib_.FindGame(id);
    if (!g || launcher_.Busy()) return;
    if (admin_->IsOpen()) admin_->Close();
    SaveNavState();
    // La vidéo du jeu continue en fond de l'écran de chargement (son baissé), puis s'arrête dès que
    // la fenêtre du jeu apparaît : aucun décodage ni son pendant le jeu.
    if (video_ && !video_->Path().empty()) {
        if (video_->Path() == lib_.VideoFor(*g) && profile_.video.enabled)
            video_->SetVolume(profile_.video.volume * 0.5f, profile_.video.mute);
        else
            video_->Stop();
    }
    launchingId_ = id;
    state_ = State::Launching;
    stateSince_ = SDL_GetTicks();
    nextWindowScan_ = 0;
    fgAtLaunch_ = GetForegroundWindow();
    // Au-dessus de tout pendant le chargement, y compris d'une console qui s'ouvrirait quand même.
    if (HWND me = NativeWindow(window_)) SetWindowPos(me, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    launcher_.Start(*g);
}

void App::OnGameFinished(const LaunchResult& r) {
    bool wasInGame = state_ == State::InGame;
    state_ = State::Browse;
    if (HWND me = NativeWindow(window_)) SetWindowPos(me, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (r.ok) lib_.RecordPlay(r.gameId, r.seconds, profileName_);
    else Toast(r.error, true);
    if (wasInGame) RestoreForeground();
    input_.Clear();
    // Garde : un bouton encore enfoncé à la sortie du jeu ne doit pas relancer ou naviguer.
    input_.BlockUntil(SDL_GetTicks() + (uint64_t)input_.Timing().returnGuardMs);
    RefreshItems();
    SaveNavState();
    logx::Info("Navigation restaurée");
}

const char* App::Version() { return kVersion; }

bool App::InstallUpdate(std::string& err) {
    if (!updater_.LaunchInstall(paths_.exeDir, paths_.dataDir, sys::AutoStartEnabled(), settings_.kiosk, err)) return false;
    SaveNavState();
    running_ = false;  // code 0 : la surveillance éventuelle s'arrête, le script installe puis relance
    return true;
}

void App::LeaveLaunchScreen(void* gameWindow, const char* why) {
    state_ = State::InGame;
    if (video_) video_->Stop();
    HWND me = NativeWindow(window_), game = (HWND)gameWindow;
    if (me) SetWindowPos(me, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (game) logx::Info("Fenêtre du jeu : {}", DescribeWindow(game));
    if (game) {  // le frontend est au premier plan : il peut le céder au jeu
        SetForegroundWindow(game);
        BringWindowToTop(game);
    }
    if (settings_.minimizeDuringGame) SDL_MinimizeWindow(window_);
    if (game && GetForegroundWindow() != game) SetForegroundWindow(game);
    logx::Info("Écran de chargement retiré ({}) après {} ms : frontend en veille pendant le jeu", why,
               Elapsed(SDL_GetTicks(), stateSince_));
}

void App::RestoreForeground() {
    SDL_RestoreWindow(window_);
    if (settings_.fullscreen) SDL_SetWindowFullscreen(window_, true);
    SDL_RaiseWindow(window_);
    // Windows limite SetForegroundWindow : on s'attache temporairement au thread de la fenêtre active.
    HWND hwnd = NativeWindow(window_);
    HWND fg = GetForegroundWindow();
    if (hwnd && fg != hwnd) {
        DWORD fgThread = GetWindowThreadProcessId(fg, nullptr), me = GetCurrentThreadId();
        if (fgThread && fgThread != me) AttachThreadInput(fgThread, me, TRUE);
        ShowWindow(hwnd, SW_RESTORE);
        BringWindowToTop(hwnd);
        SetForegroundWindow(hwnd);
        if (fgThread && fgThread != me) AttachThreadInput(fgThread, me, FALSE);
    }
    if (settings_.hideCursor) SDL_HideCursor();
}

void App::Toast(const std::string& msg, bool error) {
    toast_ = msg;
    toastError_ = error;
    toastUntil_ = SDL_GetTicks() + (error ? 6000 : 2000);
}

void App::Update(float dt) {
    if (state_ == State::Attract) {  // défilement de démonstration, à vitesse réglable
        WheelTheme t = theme_.wheel;
        t.animSpeed *= std::max(0.1f, profile_.attract.speed);
        wheel_.Update(dt, t);
    } else {
        wheel_.Update(dt, theme_.wheel);
    }
}

// ---------------------------------------------------------------- Vidéo

void App::UpdateVideo(uint64_t now) {
    const VideoSettings& v = profile_.video;
    const bool attract = state_ == State::Attract;
    std::string want;
    bool showing = (state_ == State::Browse || state_ == State::Menu || state_ == State::QuitConfirm || attract) && !Mosaic();
    bool enabled = v.enabled && (!attract || profile_.attract.video);
    if (enabled && showing)
        if (WheelItem* it = SelectedItem(); it && it->kind == ItemKind::Game)
            if (const Game* g = lib_.FindGame(it->id)) want = lib_.VideoFor(*g);
    // Écran de chargement : vidéo du jeu lancé en fond, démarrée tout de suite.
    const bool loading = state_ == State::Launching && launcher_.CurrentPhase() != Launcher::Phase::Done;
    if (loading && v.enabled)
        if (const Game* g = lib_.FindGame(launchingId_)) want = lib_.VideoFor(*g);

    for (const auto& m : pendingMoves_)
        if (m.to == want) want.clear();  // nouvelle vidéo pas encore en place
    if (want != video_->Path()) {
        if (!video_->Path().empty()) video_->Stop();  // changement de sélection : arrêt immédiat (non bloquant)
        // Démarrage seulement quand la sélection est stable : rien n'est décodé pendant le défilement.
        uint64_t delay = attract ? 0 : (uint64_t)v.delayMs;
        bool stable = loading || (Elapsed(now, selectionChangedAt_) >= delay && !wheel_.Animating());
        if (!want.empty() && stable && !video_->Failed(want))
            video_->Open(want, {v.loop, attract ? profile_.attract.volume : loading ? v.volume * 0.5f : v.volume,
                                attract ? profile_.attract.mute : v.mute, v.hwDecode});
    }
}

// ---------------------------------------------------------------- Récupération des médias

void App::SaveScraperConfig() {
    scraperCfg_.Save(paths_.ScraperFile());
    scraper_.SetConfig(scraperCfg_);
}

std::optional<Provider> App::PreferredProvider() {
    // Source préférée si elle est utilisable, sinon la première disponible (LaunchBox ne demande pas de compte).
    bool ss = scraperCfg_.HasScreenScraper(), sg = scraperCfg_.HasSgdb(), lb = scraper_.LaunchBoxReady();
    if (scraperCfg_.preferred == "launchbox" && lb) return Provider::LaunchBox;
    if (scraperCfg_.preferred == "screenscraper" && ss) return Provider::ScreenScraper;
    if (scraperCfg_.preferred == "steamgriddb" && sg) return Provider::SteamGridDB;
    if (lb) return Provider::LaunchBox;
    if (ss) return Provider::ScreenScraper;
    if (sg) return Provider::SteamGridDB;
    return std::nullopt;
}

bool App::IsIncomplete(const Game& g) {
    if (lib_.LogoFor(g).empty() || lib_.FanartFor(g).empty()) return true;
    // La vidéo ne compte que si une source capable d'en fournir est configurée : ScreenScraper, ou LaunchBox
    // avec yt-dlp installé.
    if (!scraperCfg_.wantVideo || !lib_.VideoFor(g).empty()) return false;
    return scraperCfg_.HasScreenScraper() ||
           (scraperCfg_.lbAutoVideo && scraper_.LaunchBoxReady() && !ytvideo::FindYtDlp(scraperCfg_).empty());
}

std::vector<int64_t> App::IncompleteGames() {
    std::vector<int64_t> ids;
    for (const Game& g : lib_.Games())
        if (IsIncomplete(g)) ids.push_back(g.id);
    return ids;
}

void App::CompleteGame(int64_t id, bool batch) {
    const Game* g = lib_.FindGame(id);
    if (!g) return;
    CompleteRequest req;
    req.gameId = id;
    req.name = g->name;
    req.dir = lib_.MediaDirFor(id);
    req.current = {g->publisher, g->genre, g->description, g->platform, g->year, !g->description.empty()};
    req.batch = batch;
    scraper_.Complete(req);
}

void App::StartBatch(const std::vector<int64_t>& ids) {
    auto order = scraper_.CascadeOrder();
    if (order.empty() || !scraperCfg_.enabled) {
        batchStatus_ = "Aucune source disponible (base LaunchBox à télécharger, clé SteamGridDB ou identifiants "
                       "ScreenScraper) ou récupération désactivée.";
        return;
    }
    std::string names;
    for (Provider p : order) names += (names.empty() ? "" : " > ") + std::string(ProviderName(p));
    batchFetched_ = batchAmbiguous_ = batchNone_ = batchErrors_ = 0;
    for (int64_t id : ids) CompleteGame(id, true);
    batchStatus_ = std::format("{} jeu(x) en file ({})", ids.size(), names);
    logx::Info("Récupération groupée : {} jeu(x), sources {}", ids.size(), names);
}

void App::HandleComplete(CompleteResult r) {
    const Game* g = lib_.FindGame(r.gameId);
    if (!g) return;
    ApplyFetch(r.fetch);
    bool propose = !r.toConfirm.candidates.empty() && !r.stillMissing.empty();
    if (!r.batch) {
        if (propose) lastSearch_ = std::move(r.toConfirm);
        return;
    }
    if (r.Obtained()) ++batchFetched_;
    else if (!r.errors.empty()) ++batchErrors_;
    if (propose) {
        ++batchAmbiguous_;
        toConfirm_.push_back(std::move(r.toConfirm));
    } else if (!r.Obtained() && r.errors.empty()) {
        ++batchNone_;
    }
    batchStatus_ = std::format("Série : {} jeu(x) complété(s), {} à confirmer, {} sans nouveau média, {} erreur(s)",
                               batchFetched_, batchAmbiguous_, batchNone_, batchErrors_);
}

void App::PollScraper() {
    while (auto s = scraper_.PollSearch()) HandleSearch(std::move(*s));
    while (auto f = scraper_.PollFetch()) ApplyFetch(*f);
    while (auto c = scraper_.PollComplete()) HandleComplete(std::move(*c));
}

void App::HandleSearch(SearchResult r) {
    if (!r.batch) {
        lastSearch_ = std::move(r);
        return;
    }
    const Game* g = lib_.FindGame(r.gameId);
    if (!g) return;
    if (!r.ok) {
        ++batchErrors_;
        // Identifiants refusés ou quota atteint : inutile de continuer la série.
        if (r.error.find("HTTP 4") != std::string::npos || r.error.find("login") != std::string::npos ||
            r.error.find("non renseign") != std::string::npos || r.error.find("LaunchBox") != std::string::npos) {
            scraper_.CancelAll();
            batchStatus_ = "Série interrompue : " + r.error;
            return;
        }
    } else {
        // Association automatique uniquement si UN seul résultat porte exactement le même nom (ou nom alternatif),
        // et jamais pour un résultat hors des systèmes arcade.
        std::vector<const ScrapeCandidate*> exact;
        for (const auto& c : r.candidates)
            if (c.autoOk && (c.exact || Scraper::Normalize(c.name) == Scraper::Normalize(g->name))) exact.push_back(&c);
        if (exact.size() == 1) {
            scraper_.Fetch(r.gameId, *exact[0], lib_.MediaDirFor(r.gameId), false);
            ++batchFetched_;
        } else if (!r.candidates.empty()) {
            ++batchAmbiguous_;
            toConfirm_.push_back(std::move(r));
        } else {
            ++batchNone_;
        }
    }
    batchStatus_ = std::format("Série : {} correspondance(s) exacte(s), {} à confirmer, {} sans résultat, {} erreur(s)",
                               batchFetched_, batchAmbiguous_, batchNone_, batchErrors_);
}

void App::ProcessPendingMoves(uint64_t now) {
    for (size_t i = 0; i < pendingMoves_.size();) {
        PendingMove& m = pendingMoves_[i];
        if (video_->Path() == m.to) video_->Stop();
        if (MoveFileExW(util::Widen(m.from).c_str(), util::Widen(m.to).c_str(), MOVEFILE_REPLACE_EXISTING)) {
            logx::Info("Vidéo remplacée après arrêt de la lecture : {}", m.to);
            crops_.Forget(m.to);
            pendingMoves_.erase(pendingMoves_.begin() + (long long)i);
            continue;
        }
        if (now > m.deadline) {
            logx::Error("Remplacement de {} impossible : {}", m.to, GetLastError());
            lastFetchMessage_ = m.game + " : vidéo téléchargée mais impossible de remplacer l'ancienne (fichier utilisé)";
            std::error_code ec;
            fs::remove(util::U8Path(m.from), ec);
            pendingMoves_.erase(pendingMoves_.begin() + (long long)i);
            continue;
        }
        ++i;
    }
}

void App::ApplyFetch(const FetchResult& r) {
    const Game* g = lib_.FindGame(r.gameId);
    if (!g) return;
    lastFetchMessage_ = g->name + " : " + r.message;
    if (!r.pendingFrom.empty()) {
        // La vidéo actuelle est en cours de lecture : on l'arrête, le fichier est remplacé dès qu'il est libéré.
        if (video_->Path() == r.pendingTo) video_->Stop();
        pendingMoves_.push_back({r.pendingFrom, r.pendingTo, g->name, SDL_GetTicks() + 8000});
    }
    for (const std::string& f : r.written) {
        textures_->Invalidate(f);
        crops_.Forget(f);
        if (f == video_->Path()) video_->Stop();
    }
    // Métadonnées : seulement les champs vides (les saisies manuelles ne sont jamais écrasées).
    Game copy = *g;
    bool changed = false;
    auto fill = [&](std::string& dst, const std::string& src) {
        if (dst.empty() && !src.empty()) {
            dst = src;
            changed = true;
        }
    };
    fill(copy.publisher, r.meta.publisher);
    fill(copy.genre, r.meta.genre);
    fill(copy.description, r.meta.description);
    fill(copy.platform, r.meta.platform);
    if (!copy.year && r.meta.year) {
        copy.year = r.meta.year;
        changed = true;
    }
    // Vidéo YouTube choisie à la main : elle remplace le fichier vidéo indiqué dans la fiche.
    if (r.replaceVideo && r.ok) {
        if (!copy.videoPath.empty()) copy.videoPath.clear();
        copy.videoUrl = r.videoUrl;  // lien gardé : export / import, nouveau téléchargement
        copy.videoStart = r.videoStart;
        copy.videoDuration = r.videoDuration;
        changed = true;
    }
    if (changed) lib_.UpdateGame(copy);
    if (!admin_->IsOpen() && !r.written.empty()) Toast("Médias mis à jour : " + g->name);
}

// ---------------------------------------------------------------- Rendu

void App::FillRect(const SDL_FRect& r, Color c, float alpha) {
    SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, (Uint8)std::clamp(c.a * alpha, 0.0f, 255.0f));
    SDL_RenderFillRect(renderer_, &r);
}

void App::DrawImageFit(SDL_Texture* t, const SDL_FRect& box, float alpha, double angle, bool cover,
                       const SDL_FRect* srcRect) {
    float fw, fh;
    SDL_GetTextureSize(t, &fw, &fh);
    if (fw <= 0 || fh <= 0) return;
    const SDL_FRect base = srcRect ? *srcRect : SDL_FRect{0, 0, fw, fh};  // partie de l'image à montrer
    const float tw = base.w, th = base.h;
    if (tw <= 0 || th <= 0) return;
    SDL_SetTextureAlphaModFloat(t, std::clamp(alpha, 0.0f, 1.0f));
    if (cover) {
        float k = std::max(box.w / tw, box.h / th);
        SDL_FRect src{base.x + (tw - box.w / k) / 2, base.y + (th - box.h / k) / 2, box.w / k, box.h / k};
        SDL_RenderTextureRotated(renderer_, t, &src, &box, angle, nullptr, SDL_FLIP_NONE);
    } else {
        float k = std::min(box.w / tw, box.h / th);
        SDL_FRect dst{box.x + (box.w - tw * k) / 2, box.y + (box.h - th * k) / 2, tw * k, th * k};
        SDL_RenderTextureRotated(renderer_, t, srcRect ? &base : nullptr, &dst, angle, nullptr, SDL_FLIP_NONE);
    }
}

bool App::VideoSrcRect(SDL_FRect& r) {
    if (!videoTex_ || !profile_.video.autoCrop) return false;
    CropRect c;
    if (!crops_.Get(video_->Path(), c) || c.Full()) return false;
    float tw = 0, th = 0;
    SDL_GetTextureSize(videoTex_, &tw, &th);
    r = {c.x * tw, c.y * th, c.w * tw, c.h * th};
    return r.w > 0 && r.h > 0;
}

std::string App::ItemTitle(const WheelItem& it) const {
    if (it.kind == ItemKind::Category) return lib_.NodeName(it.id);
    const Game* g = lib_.FindGame(it.id);
    return g ? g->name : "?";
}

std::string App::ItemLogoPath(const WheelItem& it) const {
    if (it.kind != ItemKind::Game) return {};
    const Game* g = lib_.FindGame(it.id);
    return g ? lib_.LogoFor(*g) : std::string();
}

void App::Render() {
    int iw, ih;
    SDL_GetRenderOutputSize(renderer_, &iw, &ih);
    const float W = (float)iw, H = (float)ih;
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    pointerArea_ = {W, H};

    // Préchargement : logos des voisins (sélection d'abord), puis fonds proches.
    wheel_.PrefetchIndices(theme_.wheel.visible, theme_.wheel, prefetch_);
    for (size_t i = 0; i < prefetch_.size(); ++i) {
        const WheelItem& it = items_[(size_t)prefetch_[i]];
        textures_->Prefetch(ItemLogoPath(it));
        if (i < 5 && it.kind == ItemKind::Game)
            if (const Game* g = lib_.FindGame(it.id)) textures_->Prefetch(lib_.FanartFor(*g));
    }

    videoTex_ = video_->Frame();  // appelé à chaque frame : cadence la vidéo et libère les sessions arrêtées
    DrawBackground(W, H);
    DrawHeader(W, H);
    if (GunLayout()) {
        DrawGunBrowse(W, H);
    } else {
        DrawMedia(W, H);
        DrawInfo(W, H);
        DrawWheel(W, H);
    }
    DrawOverlay(W, H);
    if (GunMode()) DrawGunOverlay(W, H);
    if (admin_->IsOpen()) admin_->Render();

    MaybeScreenshot(SDL_GetTicks());
    if (recorder_) {
        const uint64_t t = SDL_GetTicks();
        recorder_->Capture(renderer_, t);
        if (recordStopAt_ && t >= recordStopAt_) {
            recorder_->Stop();
            recorder_.reset();
            running_ = false;
        }
    }
    SDL_RenderPresent(renderer_);
    textures_->EndFrame();
    text_->EndFrame();
}

void App::DrawBackground(float W, float H) {
    auto fc = [](Color c) { return SDL_FColor{c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, 1.0f}; };
    SDL_Vertex v[4] = {{{0, 0}, fc(theme_.backgroundTop), {0, 0}},
                       {{W, 0}, fc(theme_.backgroundTop), {0, 0}},
                       {{W, H}, fc(theme_.backgroundBottom), {0, 0}},
                       {{0, H}, fc(theme_.backgroundBottom), {0, 0}}};
    int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(renderer_, nullptr, v, 4, idx, 6);
    if (!theme_.backgroundImage.empty())  // image de fond du thème
        if (SDL_Texture* bg = textures_->Get(theme_.ResolveFile(theme_.backgroundImage)))
            DrawImageFit(bg, {0, 0, W, H}, 1.0f, 0, true);

    WheelItem* it = SelectedItem();
    if (it && it->kind == ItemKind::Game && theme_.fanartBackgroundAlpha > 0) {
        if (const Game* g = lib_.FindGame(it->id))
            if (SDL_Texture* t = textures_->Get(lib_.FanartFor(*g))) {
                float fade = std::min(1.0f, (SDL_GetTicks() - selectionChangedAt_) / 300.0f);
                DrawImageFit(t, {0, 0, W, H}, theme_.fanartBackgroundAlpha * fade, 0, true);
            }
    }
}

void App::DrawHeader(float W, float H) {
    const RectF& r = theme_.headerRect;
    TTF_Font* f = text_->Font(theme_.fontBody, (int)(theme_.smallSize * H));
    if (state_ == State::Attract) {
        text_->Draw(f, "Démonstration", r.x * W, r.y * H, theme_.accent);
        return;
    }
    if (!theme_.showHeader) return;
    text_->Draw(f, profileName_ + "   \xC2\xB7   " + lib_.NodePath(node_), r.x * W, r.y * H, theme_.textDim);
    float right = (r.x + r.w) * W;
    if (GunMode() && state_ != State::Attract)  // boutons du pistolet à droite
        right = W * (1.0f - 0.012f - 0.09f * (GunLayout() ? 3 : 2) - 0.01f * (GunLayout() ? 2 : 1) - 0.015f);
    if (!items_.empty() && !(GunLayout() && !Mosaic()))
        text_->Draw(f, std::format("{} / {}", wheel_.Selected() + 1, items_.size()), right, r.y * H, theme_.textDim,
                    Align::Right);
}

void App::DrawMedia(float W, float H) {
    const RectF& r = theme_.mediaRect;
    SDL_FRect box{r.x * W, r.y * H, r.w * W, r.h * H};
    WheelItem* it = SelectedItem();
    if (!it) return;
    FillRect(box, theme_.panel);
    SDL_FRect inner{box.x + 8, box.y + 8, box.w - 16, box.h - 16};
    float fade = std::min(1.0f, (SDL_GetTicks() - selectionChangedAt_) / 200.0f);
    DrawMediaBox(*it, inner, fade);
}

void App::DrawBlurFill(SDL_Texture* src, const SDL_FRect& box, const SDL_FRect* srcRect) {
    // Flou bon marché sur la carte graphique : l'image, recadrée à la forme du cadre, est réduite au 1/10
    // puis au 1/40 avec filtrage linéaire, puis agrandie : chaque réduction moyenne les pixels voisins.
    float fw = 0, fh = 0;
    SDL_GetTextureSize(src, &fw, &fh);
    const SDL_FRect base = srcRect ? *srcRect : SDL_FRect{0, 0, fw, fh};
    const float tw = base.w, th = base.h;
    if (tw <= 0 || th <= 0 || box.w < 8 || box.h < 8) return;
    auto target = [&](SDL_Texture*& t, int& w, int& h, int nw, int nh) {
        nw = std::max(4, nw);
        nh = std::max(4, nh);
        if (t && w == nw && h == nh) return t;
        if (t) SDL_DestroyTexture(t);
        t = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, nw, nh);
        if (t) SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
        w = nw;
        h = nh;
        return t;
    };
    SDL_Texture* a = target(blurA_, blurAw_, blurAh_, (int)(box.w / 10), (int)(box.h / 10));
    SDL_Texture* b = target(blurB_, blurBw_, blurBh_, (int)(box.w / 40), (int)(box.h / 40));
    if (!a || !b) {
        FillRect(box, {0, 0, 0, 255});
        return;
    }
    // Recadrage « couvrir » : la partie centrale de l'image qui a la forme du cadre.
    const float k = std::max(box.w / tw, box.h / th);
    const SDL_FRect crop{base.x + (tw - box.w / k) / 2, base.y + (th - box.h / k) / 2, box.w / k, box.h / k};
    SDL_Texture* prev = SDL_GetRenderTarget(renderer_);
    SDL_SetRenderTarget(renderer_, a);
    SDL_SetTextureAlphaModFloat(src, 1.0f);
    SDL_RenderTexture(renderer_, src, &crop, nullptr);
    SDL_SetRenderTarget(renderer_, b);
    SDL_RenderTexture(renderer_, a, nullptr, nullptr);
    SDL_SetRenderTarget(renderer_, prev);
    // Agrandissement légèrement débordant (bords non flous masqués), puis assombrissement.
    SDL_FRect big{box.x - box.w * 0.03f, box.y - box.h * 0.03f, box.w * 1.06f, box.h * 1.06f};
    // Même rectangle en pixels entiers pour le découpage et l'assombrissement (sinon une colonne de bord reste claire).
    const int x0 = (int)std::ceil(box.x), y0 = (int)std::ceil(box.y);
    const int x1 = (int)std::floor(box.x + box.w), y1 = (int)std::floor(box.y + box.h);
    SDL_Rect clip{x0, y0, x1 - x0, y1 - y0};
    const bool hadClip = SDL_RenderClipEnabled(renderer_);
    SDL_Rect oldClip{};
    if (hadClip) SDL_GetRenderClipRect(renderer_, &oldClip);
    SDL_SetRenderClipRect(renderer_, &clip);
    SDL_RenderTexture(renderer_, b, nullptr, &big);
    SDL_SetRenderClipRect(renderer_, hadClip ? &oldClip : nullptr);
    FillRect({(float)x0, (float)y0, (float)(x1 - x0), (float)(y1 - y0)}, {0, 0, 0, 255}, 0.45f);
}

void App::DrawMediaBox(const WheelItem& item, const SDL_FRect& inner, float fade) {
    const float H = pointerArea_.y;
    const WheelItem* it = &item;
    if (it->kind == ItemKind::Game) {
        const Game* g = lib_.FindGame(it->id);
        if (!g) return;
        // Vidéo si elle est prête, sinon fanart / capture, sinon logo en grand (visuel de remplacement).
        if (videoTex_ && !video_->Path().empty()) {
            // Zone utile (bandes noires de la vidéo retirées si détectées), sinon l'image entière.
            SDL_FRect src{};
            const bool cropped = VideoSrcRect(src);
            float tw = 0, th = 0;
            SDL_GetTextureSize(videoTex_, &tw, &th);
            if (cropped) {
                tw = src.w;
                th = src.h;
            }
            // Forme différente du cadre (écart > 3 %) : fond flou plutôt que des bandes noires.
            const bool differs = tw > 0 && th > 0 && std::abs((tw / th) / (inner.w / inner.h) - 1.0f) > 0.03f;
            if (profile_.video.blurFill && differs) DrawBlurFill(videoTex_, inner, cropped ? &src : nullptr);
            else FillRect(inner, {0, 0, 0, 255});
            DrawImageFit(videoTex_, inner, 1.0f, 0, false, cropped ? &src : nullptr);
            return;
        }
        if (SDL_Texture* fan = textures_->Get(lib_.FanartFor(*g))) {
            DrawImageFit(fan, inner, fade, 0, true);
            return;
        }
        if (SDL_Texture* logo = textures_->Get(lib_.LogoFor(*g))) {
            SDL_FRect lb{inner.x + inner.w * 0.1f, inner.y + inner.h * 0.15f, inner.w * 0.8f, inner.h * 0.7f};
            DrawImageFit(logo, lb, fade);
            return;
        }
    }
    TTF_Font* big = text_->Font(theme_.fontTitle, (int)(theme_.titleSize * H * 1.4f));
    text_->DrawFit(big, ItemTitle(*it), {inner.x + inner.w * 0.08f, inner.y, inner.w * 0.84f, inner.h},
                   it->kind == ItemKind::Category ? theme_.accent : theme_.textDim, (int)(inner.w * 0.84f), fade);
}

void App::DrawInfo(float W, float H) {
    const RectF& r = theme_.infoRect;
    const float x = r.x * W, w = r.w * W;
    float y = r.y * H;
    const float bottom = (r.y + r.h) * H;
    TTF_Font* title = text_->Font(theme_.fontTitle, (int)(theme_.titleSize * H));
    TTF_Font* body = text_->Font(theme_.fontBody, (int)(theme_.bodySize * H));
    TTF_Font* small = text_->Font(theme_.fontBody, (int)(theme_.smallSize * H));

    WheelItem* it = SelectedItem();
    if (!it) {
        text_->Draw(title, "Catégorie vide", x, y, theme_.text, Align::Left, (int)w);
        text_->Draw(body, "Ajoutez des jeux depuis l'administration (F1 au clavier, ou bouton Menu).", x,
                    y + theme_.titleSize * H * 1.6f, theme_.textDim, Align::Left, (int)w);
        return;
    }

    if (it->kind == ItemKind::Category) {
        y += text_->Draw(title, lib_.NodeName(it->id), x, y, theme_.text, Align::Left, (int)w).y + 10;
        int games = lib_.CountGames(it->id);
        int subs = 0;
        for (int64_t c : lib_.ChildCategories(it->id)) subs += c > 0 ? 1 : 0;
        std::string line = std::format("{} jeu{}", games, games > 1 ? "x" : "");
        if (subs) line += std::format("   ·   {} sous-catégorie{}", subs, subs > 1 ? "s" : "");
        y += text_->Draw(body, line, x, y, theme_.accent).y + 20;
        text_->Draw(small, "Valider pour ouvrir  ·  Retour pour remonter", x, y, theme_.textDim);
        return;
    }

    const Game* g = lib_.FindGame(it->id);
    if (!g) return;
    y += text_->Draw(title, g->name, x, y, theme_.text, Align::Left, (int)w).y;
    if (!g->altName.empty()) y += text_->Draw(body, g->altName, x, y, theme_.textDim, Align::Left, (int)w).y;
    y += 8;

    std::string meta;
    auto add = [&](const std::string& s) {
        if (s.empty()) return;
        if (!meta.empty()) meta += "   ·   ";
        meta += s;
    };
    if (g->year > 0) add(std::to_string(g->year));
    add(g->publisher);
    add(g->genre);
    add(g->platform);
    if (theme_.showMeta && !meta.empty()) y += text_->Draw(body, meta, x, y, theme_.accent, Align::Left, (int)w).y + 14;

    float statsH = theme_.showStats ? theme_.smallSize * H * 1.8f : 0;
    if (theme_.showDescription && !g->description.empty()) {
        SDL_Rect clip{(int)x, (int)y, (int)w, (int)std::max(0.0f, bottom - statsH - y)};
        SDL_SetRenderClipRect(renderer_, &clip);
        text_->Draw(body, g->description, x, y, theme_.text, Align::Left, (int)w);
        SDL_SetRenderClipRect(renderer_, nullptr);
    }

    if (theme_.showStats) {
        std::string stats;
        if (g->favorite) stats += "\xE2\x98\x85 Favori   ·   ";  // ★
        stats += g->playCount ? std::format("Lancé {} fois   ·   {}", g->playCount, util::FormatDuration(g->playSeconds))
                              : std::string("Jamais lancé");
        text_->Draw(small, stats, x, bottom - theme_.smallSize * H * 1.4f, theme_.textDim);
    }
}

void App::DrawItemVisual(const WheelItem& it, const SDL_FRect& r, float alpha, double angle, bool selected) {
    std::string logo = ItemLogoPath(it);
    if (SDL_Texture* t = textures_->Get(logo)) {
        DrawImageFit(t, r, alpha, angle);
        return;
    }
    // Visuel de remplacement : nom du jeu ou de la catégorie.
    bool loading = !logo.empty() && !textures_->Failed(logo);
    TTF_Font* f = text_->Font(theme_.fontTitle, 48);  // rendu une fois en 48 px, puis mis à l'échelle
    Color c = it.kind == ItemKind::Category ? theme_.accent : (selected ? theme_.text : theme_.textDim);
    text_->DrawFit(f, ItemTitle(it), r, c, 520, alpha * (loading ? 0.35f : 1.0f), angle);
}

void App::DrawWheel(float W, float H) {
    const WheelTheme& t = theme_.wheel;
    SDL_FRect band{0, t.y * H - t.bandHeight * H / 2, W, t.bandHeight * H};
    FillRect(band, t.bandColor);
    SDL_FRect line{0, band.y, W, 2};
    FillRect(line, theme_.accent, 0.6f);

    wheel_.Layout(W, H, t, slots_);
    for (const Wheel::Slot& s : slots_) {
        bool sel = s.focus > 0.5f;
        DrawItemVisual(items_[(size_t)s.index], s.rect, s.alpha, s.angle, sel);
    }
    // Repère de sélection sous le logo central
    if (!items_.empty()) {
        float sw = t.itemW * W * t.selectedScale * 0.6f;
        SDL_FRect mark{W / 2 - sw / 2, band.y + band.h - 10, sw, 4};
        FillRect(mark, theme_.accent);
    }
}

void App::DrawMenu(float W, float H) {
    FillRect({0, 0, W, H}, {0, 0, 0, 170});
    TTF_Font* title = text_->Font(theme_.fontTitle, (int)(theme_.titleSize * H * 0.8f));
    TTF_Font* body = text_->Font(theme_.fontBody, (int)(theme_.bodySize * H * 1.15f));
    TTF_Font* small = text_->Font(theme_.fontBody, (int)(theme_.smallSize * H));
    const float lineH = theme_.bodySize * H * 2.1f;
    const float boxW = W * 0.42f, boxH = lineH * (menu_.size() + 2.6f);
    SDL_FRect box{(W - boxW) / 2, (H - boxH) / 2, boxW, boxH};
    FillRect(box, {12, 14, 24, 245});
    FillRect({box.x, box.y, box.w, 3}, theme_.accent);
    text_->Draw(title, "Menu", W / 2, box.y + lineH * 0.3f, theme_.text, Align::Center);
    float y = box.y + lineH * 1.5f;
    menuBox_ = box;
    menuRects_.clear();
    for (size_t i = 0; i < menu_.size(); ++i) {
        bool sel = (int)i == menuIndex_;
        menuRects_.push_back({box.x + 12, y - lineH * 0.12f, box.w - 24, lineH * 0.9f});
        if (sel) FillRect(menuRects_.back(), theme_.accent, 0.25f);
        text_->Draw(body, menu_[i].label, box.x + 36, y, sel ? theme_.text : theme_.textDim);
        y += lineH;
    }
    text_->Draw(small, GunMode() ? "Tirez sur un choix  ·  tir à côté ou recharge : fermer"
                                 : "Gauche/droite ou haut/bas : choisir  ·  Valider  ·  Retour : fermer",
                W / 2, box.y + box.h + 12, theme_.textDim, Align::Center);
}

void App::DrawProfileSelect(float W, float H) {
    FillRect({0, 0, W, H}, {0, 0, 0, 200});
    TTF_Font* title = text_->Font(theme_.fontTitle, (int)(theme_.titleSize * H * 0.8f));
    TTF_Font* body = text_->Font(theme_.fontBody, (int)(theme_.bodySize * H * 1.15f));
    TTF_Font* small = text_->Font(theme_.fontBody, (int)(theme_.smallSize * H));
    const float lineH = theme_.bodySize * H * 2.1f;
    const float boxW = W * 0.4f, boxH = lineH * (profileList_.size() + 2.6f);
    SDL_FRect box{(W - boxW) / 2, (H - boxH) / 2, boxW, boxH};
    FillRect(box, {12, 14, 24, 245});
    FillRect({box.x, box.y, box.w, 3}, theme_.accent);
    text_->Draw(title, "Profil de la borne", W / 2, box.y + lineH * 0.3f, theme_.text, Align::Center);
    float y = box.y + lineH * 1.5f;
    profileRects_.clear();
    for (size_t i = 0; i < profileList_.size(); ++i) {
        bool sel = (int)i == profileIndex_;
        profileRects_.push_back({box.x + 12, y - lineH * 0.12f, box.w - 24, lineH * 0.9f});
        if (sel) FillRect(profileRects_.back(), theme_.accent, 0.25f);
        std::string label = profileList_[i] + (profileList_[i] == profileName_ ? "   (actif)" : "");
        text_->Draw(body, label, box.x + 36, y, sel ? theme_.text : theme_.textDim);
        y += lineH;
    }
    text_->Draw(small, "Choisir  ·  Valider  ·  Retour : annuler", W / 2, box.y + box.h + 12, theme_.textDim, Align::Center);
}

void App::DrawWizard(float W, float H) {
    FillRect({0, 0, W, H}, {5, 6, 12, 252});
    TTF_Font* title = text_->Font(theme_.fontTitle, (int)(theme_.titleSize * H));
    TTF_Font* big = text_->Font(theme_.fontTitle, (int)(theme_.titleSize * H * 1.3f));
    TTF_Font* body = text_->Font(theme_.fontBody, (int)(theme_.bodySize * H));
    TTF_Font* small = text_->Font(theme_.fontBody, (int)(theme_.smallSize * H));
    const uint64_t now = SDL_GetTicks();
    const int total = (int)Action::Count;
    const Action cur = (Action)std::min(wiz_.step, total - 1);

    text_->Draw(title, "Configuration des commandes", W / 2, H * 0.08f, theme_.text, Align::Center);
    text_->Draw(body, std::format("Étape {} / {}", wiz_.step + 1, total), W / 2, H * 0.17f, theme_.textDim, Align::Center);
    SDL_FRect bar{W * 0.25f, H * 0.215f, W * 0.5f, 6};
    FillRect(bar, {255, 255, 255, 40});
    FillRect({bar.x, bar.y, bar.w * (float)wiz_.step / total, bar.h}, theme_.accent);

    text_->Draw(big, ActionLabel(cur), W / 2, H * 0.30f, theme_.accent, Align::Center);

    if (wiz_.phase == Wizard::Phase::Show) {
        std::string msg = wiz_.got ? "Enregistré : " + wiz_.got->Describe() : std::string("Aucun appui : commande actuelle conservée");
        text_->Draw(body, msg, W / 2, H * 0.46f, wiz_.got ? Color{120, 230, 120, 255} : theme_.textDim, Align::Center);
    } else if (wiz_.phase == Wizard::Phase::WaitRelease) {
        text_->Draw(body, "Relâchez toutes les commandes…", W / 2, H * 0.46f, theme_.textDim, Align::Center);
    } else {
        text_->Draw(body, "Appuyez sur la commande à utiliser (bouton, volant, pédale, touche)", W / 2, H * 0.46f,
                    theme_.text, Align::Center);
        float left = 1.0f - std::min(1.0f, Elapsed(now, wiz_.since) / 10000.0f);
        SDL_FRect tb{W * 0.35f, H * 0.53f, W * 0.3f, 4};
        FillRect(tb, {255, 255, 255, 30});
        FillRect({tb.x, tb.y, tb.w * left, tb.h}, theme_.textDim);
    }
    if (!wiz_.warning.empty()) text_->Draw(body, wiz_.warning, W / 2, H * 0.58f, {255, 110, 100, 255}, Align::Center);

    // Associations actuelles de cette action
    std::string curList;
    for (const Binding& b : wiz_.backup)
        if (b.action == cur) curList += (curList.empty() ? "" : "\n") + b.Describe();
    text_->Draw(small, "Actuellement :", W * 0.25f, H * 0.65f, theme_.textDim);
    text_->Draw(small, curList.empty() ? "(rien)" : curList, W * 0.25f, H * 0.65f + theme_.smallSize * H * 1.5f,
                theme_.textDim, Align::Left, (int)(W * 0.5f));
    text_->Draw(small, "Sans appui pendant 10 s, la commande actuelle est conservée.   Échap (clavier) : annuler.",
                W / 2, H * 0.94f, theme_.textDim, Align::Center);
}

void App::DrawOverlay(float W, float H) {
    const uint64_t now = SDL_GetTicks();
    TTF_Font* title = text_->Font(theme_.fontTitle, (int)(theme_.titleSize * H));
    TTF_Font* body = text_->Font(theme_.fontBody, (int)(theme_.bodySize * H));

    if (now < toastUntil_ && !toast_.empty()) {
        SDL_FPoint sz = text_->Measure(body, toast_, (int)(W * 0.6f));
        SDL_FRect box{W / 2 - sz.x / 2 - 24, theme_.wheel.y * H - theme_.wheel.bandHeight * H / 2 - sz.y - 50,
                      sz.x + 48, sz.y + 24};
        FillRect(box, toastError_ ? Color{150, 20, 20, 230} : Color{0, 0, 0, 210});
        text_->Draw(body, toast_, W / 2, box.y + 12, theme_.text, Align::Center, (int)(W * 0.6f));
    }

    if (state_ == State::Launching || state_ == State::Returning) {
        const Game* g = lib_.FindGame(launchingId_);
        // Écran de lancement : vidéo du jeu (ou son fond) assombrie, logo, étape en cours.
        const bool vid = g && videoTex_ && !video_->Path().empty() && video_->Path() == lib_.VideoFor(*g);
        if (vid) {
            FillRect({0, 0, W, H}, {0, 0, 0, 255});
            SDL_FRect src{};
            const bool cropped = VideoSrcRect(src);
            DrawImageFit(videoTex_, {0, 0, W, H}, 1.0f, 0, true, cropped ? &src : nullptr);
        } else if (SDL_Texture* fan = g ? textures_->Get(lib_.FanartFor(*g)) : nullptr)
            DrawImageFit(fan, {0, 0, W, H}, 1.0f, 0, true);
        FillRect({0, 0, W, H}, {0, 0, 0, (uint8_t)(vid ? 150 : 215)});
        std::string name = g ? g->name : "";
        SDL_FRect logoBox{W * 0.3f, H * 0.25f, W * 0.4f, H * 0.3f};
        if (SDL_Texture* t = g ? textures_->Get(lib_.LogoFor(*g)) : nullptr) DrawImageFit(t, logoBox, 1.0f);
        else text_->DrawFit(title, name, logoBox, theme_.text, (int)logoBox.w, 1.0f);
        text_->Draw(body, PhaseLabel(launcher_.CurrentPhase()), W / 2, H * 0.62f, theme_.accent, Align::Center);
    }

    if (state_ == State::Menu) DrawMenu(W, H);
    if (state_ == State::ProfileSelect) DrawProfileSelect(W, H);
    if (state_ == State::LetterSelect) DrawLetterSelect(W, H);
    if (state_ == State::Attract) {
        // Invitation qui pulse au-dessus de la roue (purement visuelle : n'affecte pas la reprise).
        float pulse = 0.55f + 0.45f * std::sin(now / 450.0f);
        float y = theme_.wheel.y * H - theme_.wheel.bandHeight * H / 2 - theme_.bodySize * H * 2.4f;
        text_->Draw(body, "Appuyez sur un bouton pour jouer", W / 2, y, theme_.text, Align::Center, 0, pulse);
    }
    if (state_ == State::Wizard) DrawWizard(W, H);

    if (state_ == State::QuitConfirm) {
        FillRect({0, 0, W, H}, {0, 0, 0, 200});
        text_->Draw(title, "Quitter le frontend ?", W / 2, H * 0.4f, theme_.text, Align::Center);
        if (GunMode()) {
            float bw = W * 0.14f, bh = H * 0.09f, y = H * 0.4f + theme_.titleSize * H * 1.8f;
            quitYesRect_ = {W / 2 - bw - W * 0.02f, y, bw, bh};
            quitNoRect_ = {W / 2 + W * 0.02f, y, bw, bh};
            FillRect(quitYesRect_, theme_.accent, 0.35f);
            FillRect(quitNoRect_, theme_.panel, 1.0f);
            text_->Draw(body, "Oui", quitYesRect_.x + bw / 2, y + bh / 2 - theme_.bodySize * H * 0.6f, theme_.text, Align::Center);
            text_->Draw(body, "Non", quitNoRect_.x + bw / 2, y + bh / 2 - theme_.bodySize * H * 0.6f, theme_.text, Align::Center);
        } else {
            text_->Draw(body, "Valider : oui      Retour : non", W / 2, H * 0.4f + theme_.titleSize * H * 1.6f,
                        theme_.textDim, Align::Center);
        }
    }
}

void App::DrawLetterSelect(float W, float H) {
    FillRect({0, 0, W, H}, {0, 0, 0, 200});
    TTF_Font* title = text_->Font(theme_.fontTitle, (int)(theme_.titleSize * H * 0.8f));
    TTF_Font* small = text_->Font(theme_.fontBody, (int)(theme_.smallSize * H));
    const size_t n = letters_.size();
    const float cell = std::min(W * 0.92f / (float)n, H * 0.1f), gap = cell * 0.12f;
    TTF_Font* big = text_->Font(theme_.fontTitle, (int)(cell * 0.55f));
    float x = (W - cell * n) / 2, y = H * 0.45f;
    text_->Draw(title, "Aller à la lettre", W / 2, y - theme_.titleSize * H * 1.8f, theme_.text, Align::Center);
    letterRects_.clear();
    for (size_t i = 0; i < n; ++i) {
        SDL_FRect r{x + i * cell + gap / 2, y, cell - gap, cell - gap};
        letterRects_.push_back(r);
        bool sel = (int)i == letterIndex_;
        FillRect(r, sel ? theme_.accent : theme_.panel, sel ? 0.6f : 1.0f);
        text_->Draw(big, std::string(1, letters_[i]), r.x + r.w / 2, r.y + r.h * 0.18f, sel ? theme_.text : theme_.textDim,
                    Align::Center);
    }
    text_->Draw(small, GunMode() ? "Tirez sur une lettre  ·  tir à côté : annuler"
                                 : "Gauche/droite : choisir  ·  Valider : aller  ·  Retour : annuler",
                W / 2, y + cell + H * 0.03f, theme_.textDim, Align::Center);
}

void App::DrawGunOverlay(float W, float H) {
    // Boutons à l'écran (navigation sans autre bouton que la gâchette) : Retour, vue, Menu, en haut à droite.
    if (state_ == State::Browse && !admin_->IsOpen()) {
        TTF_Font* f = text_->Font(theme_.fontBody, (int)(theme_.bodySize * H));
        float bh = H * 0.065f, bw = W * 0.09f, y = H * 0.012f;
        gunMenuRect_ = {W - bw - W * 0.012f, y, bw, bh};
        gunViewRect_ = GunLayout() ? SDL_FRect{gunMenuRect_.x - bw - W * 0.01f, y, bw, bh} : SDL_FRect{};
        gunBackRect_ = {(GunLayout() ? gunViewRect_.x : gunMenuRect_.x) - bw - W * 0.01f, y, bw, bh};
        struct Btn {
            SDL_FRect r;
            const char* label;
            shapes::Icon icon;
        };
        std::vector<Btn> buttons = {{gunBackRect_, "Retour", shapes::Icon::Back}, {gunMenuRect_, "Menu", shapes::Icon::Menu}};
        if (GunLayout())
            buttons.push_back({gunViewRect_, Mosaic() ? "Grand" : "Mosaïque", Mosaic() ? shapes::Icon::Single : shapes::Icon::Grid});
        for (const auto& [r, label, icon] : buttons) {
            const bool hover = Inside(r, pointer_);
            const float rad = r.h * 0.3f;
            const Color fg = hover ? Color{14, 14, 20, 255} : theme_.text;
            shapes::FillRound(renderer_, {r.x, r.y + r.h * 0.06f, r.w, r.h}, rad, {0, 0, 0, 255}, 0.4f);
            shapes::FillRound(renderer_, r, rad, hover ? theme_.accent : Color{16, 18, 26, 255}, hover ? 1.0f : 0.88f);
            if (!hover) shapes::StrokeRound(renderer_, r, rad, 1.5f, theme_.accent, 0.6f);
            const SDL_FPoint ts = text_->Measure(f, label);
            const float is = r.h * 0.34f, total = is + r.h * 0.22f + ts.x;
            const float x0 = r.x + (r.w - total) / 2;
            shapes::DrawIcon(renderer_, icon, x0 + is / 2, r.y + r.h / 2, is, hover ? fg : theme_.accent);
            text_->Draw(f, label, x0 + is + r.h * 0.22f, r.y + (r.h - ts.y) / 2, fg);
        }
    } else {
        gunBackRect_ = gunMenuRect_ = gunViewRect_ = {};
    }
    // Bordure (pistolets Sinden, qui repèrent l'écran grâce à elle) ; inutile avec les RS3 (LED infrarouges).
    if (settings_.gun.borderPercent > 0 && !admin_->IsOpen()) {
        float b = H * settings_.gun.borderPercent / 100.0f;
        Color c{(uint8_t)settings_.gun.borderColor[0], (uint8_t)settings_.gun.borderColor[1], (uint8_t)settings_.gun.borderColor[2], 255};
        FillRect({0, 0, W, b}, c);
        FillRect({0, H - b, W, b}, c);
        FillRect({0, 0, b, H}, c);
        FillRect({W - b, 0, b, H}, c);
    }
    // Viseur, s'il a bougé ces 5 dernières secondes (pas pendant la démonstration).
    if (settings_.gun.crosshair && !admin_->IsOpen() && state_ != State::Attract && pointer_.x >= 0 &&
        Elapsed(SDL_GetTicks(), pointerSeenAt_) < 5000) {
        // Couleur contrastée (blanc par défaut) et contour noir épais : visible sur n'importe quel fond.
        const std::string& cc = settings_.gun.crosshairColor;
        const Color col = cc == "theme"  ? theme_.accent
                          : cc == "yellow" ? Color{255, 230, 40, 255}
                          : cc == "green"  ? Color{60, 255, 90, 255}
                          : cc == "red"    ? Color{255, 50, 50, 255}
                                           : Color{255, 255, 255, 255};
        const float k = settings_.gun.crosshairSize / 100.0f;
        const float s = H * 0.03f * k, t = std::max(2.0f, H * 0.0038f * k), px = pointer_.x, py = pointer_.y;
        const float o = std::max(2.0f, t * 0.6f);  // épaisseur du contour
        const Color dark{0, 0, 0, 255};
        const SDL_FRect ring{px - s * 0.62f, py - s * 0.62f, s * 1.24f, s * 1.24f};
        shapes::StrokeRound(renderer_, {ring.x - o, ring.y - o, ring.w + 2 * o, ring.h + 2 * o}, ring.w / 2 + o, t + 2 * o, dark, 0.9f);
        shapes::StrokeRound(renderer_, ring, ring.w / 2, t, col);
        const float in = s * 0.35f, len = s - in;  // traits extérieurs, interrompus au centre
        const SDL_FRect ticks[4] = {{px - s, py - t / 2, len, t}, {px + in, py - t / 2, len, t},
                                    {px - t / 2, py - s, t, len}, {px - t / 2, py + in, t, len}};
        for (const auto& r : ticks) FillRect({r.x - o, r.y - o, r.w + 2 * o, r.h + 2 * o}, dark, 0.9f);
        for (const auto& r : ticks) FillRect(r, col);
        shapes::FillRound(renderer_, {px - t - o, py - t - o, 2 * (t + o), 2 * (t + o)}, t + o, dark, 0.9f);
        shapes::FillRound(renderer_, {px - t, py - t, 2 * t, 2 * t}, t, col);
    }
}
