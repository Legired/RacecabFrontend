#include "admin/AdminUI.h"

#include <map>
#include <set>
#include <windows.h>
#include <psapi.h>
#include <shellapi.h>

#include <algorithm>
#include <format>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <imgui_stdlib.h>

#include "app/App.h"
#include "core/Backup.h"
#include "core/Log.h"
#include "core/ConfigShare.h"
#include "core/System.h"
#include "media/YtVideo.h"
#include "media/LaunchBox.h"
#include "profiles/Profile.h"

namespace {

struct DialogCtx {
    AdminUI* ui;
    AdminUI::Field field;
};

void SDLCALL DialogCallback(void* userdata, const char* const* files, int) {
    auto* ctx = static_cast<DialogCtx*>(userdata);
    if (files && files[0]) ctx->ui->OnDialogResult(ctx->field, files[0]);
    delete ctx;
}

void Help(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

bool Contains(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return true;
    return util::ToLower(hay).find(util::ToLower(needle)) != std::string::npos;
}

void OpenInExplorer(const std::string& path) {
    ShellExecuteW(nullptr, L"open", util::Widen(path).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

const ImVec4 kGreen{0.4f, 0.9f, 0.4f, 1}, kRed{1.0f, 0.4f, 0.35f, 1};

}  // namespace

bool AdminUI::Init(SDL_Window* w, SDL_Renderer* r) {
    window_ = w;
    renderer_ = r;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();

    int h = 1080;
    SDL_GetRenderOutputSize(r, nullptr, &h);
    float scale = std::clamp(h / 1080.0f, 0.75f, 2.5f);
    ImGui::GetStyle().ScaleAllSizes(scale);
    if (!io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf", 20.0f * scale)) io.Fonts->AddFontDefault();

    ready_ = ImGui_ImplSDL3_InitForSDLRenderer(w, r) && ImGui_ImplSDLRenderer3_Init(r);
    if (!ready_) logx::Error("Initialisation de l'interface d'administration impossible");
    return ready_;
}

void AdminUI::Shutdown() {
    scanCancel_ = true;
    if (scanThread_.joinable()) scanThread_.join();
    if (!ready_) return;
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    ready_ = false;
}

void AdminUI::Open() {
    open_ = true;
    SDL_ShowCursor();
}

void AdminUI::Close() {
    open_ = false;
    if (captureAction_ >= 0) {  // ne jamais laisser les entrées bloquées en mode capture
        app_.Inputs().EndCapture();
        captureAction_ = -1;
    }
    if (app_.Config().hideCursor) SDL_HideCursor();
}

void AdminUI::ProcessEvent(const SDL_Event& e) { ImGui_ImplSDL3_ProcessEvent(&e); }

void AdminUI::OnDialogResult(Field f, const std::string& path) {
    std::lock_guard lk(dlgMutex_);
    dlgField_ = f;
    dlgPath_ = path;
}

void AdminUI::BrowseFile(Field f, const char* filterName, const char* pattern) {
    static SDL_DialogFileFilter filters[2];
    filters[0] = {filterName, pattern};
    filters[1] = {"Tous les fichiers", "*"};
    SDL_ShowOpenFileDialog(DialogCallback, new DialogCtx{this, f}, window_, filters, 2, nullptr, false);
}

void AdminUI::SaveFile(Field f, const char* filterName, const char* pattern, const std::string& defaultName) {
    static SDL_DialogFileFilter filters[1];
    filters[0] = {filterName, pattern};
    static std::string def;
    def = defaultName;
    SDL_ShowSaveFileDialog(DialogCallback, new DialogCtx{this, f}, window_, filters, 1, def.c_str());
}

void AdminUI::RefreshImportPreview() {
    share::Preview pv;
    std::string err;
    importError_.clear();
    if (!share::ReadPreview(app_.Lib(), util::U8Path(importFile_), importOldRoot_, importNewRoot_, pv, err)) {
        importError_ = err;
        return;
    }
    importGames_ = pv.games;
    importNew_ = pv.newGames;
    importExisting_ = pv.existing;
    importLinks_ = pv.videoLinks;
    importProfilesN_ = pv.profiles;
    importMediaN_ = pv.mediaFiles;
    importMissing_ = pv.missingPaths;
    importExported_ = pv.exported;
    importChecked_ = true;
}

void AdminUI::SectionUpdates() {
    Settings& s = app_.Config();
    Updater& up = app_.Updates();
    ImGui::SeparatorText("Mises à jour");
    ImGui::Text("Version installée : %s", App::Version());
    ImGui::SameLine();
    if (ImGui::Checkbox("Vérifier au démarrage", &s.updateAutoCheck)) app_.SaveSettings();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(260);
    if (ImGui::InputText("Dépôt GitHub", &s.updateRepo)) app_.SaveSettings();
    Help("Les nouvelles versions sont cherchées dans les « releases » de ce dépôt GitHub public (propriétaire/dépôt). "
         "Rien n'est téléchargé sans ton accord. Tes jeux, médias et réglages sont conservés lors de la mise à jour.");

    const Updater::State st = up.GetState();
    ImGui::BeginDisabled(up.Busy());
    if (ImGui::Button("Rechercher une mise à jour")) up.Check(util::Trim(s.updateRepo), App::Version());
    ImGui::EndDisabled();
    ImGui::SameLine();
    const std::string msg = up.Message();
    const ImVec4 orange(1.0f, 0.8f, 0.2f, 1.0f);
    switch (st) {
        case Updater::State::Idle: ImGui::TextDisabled("pas encore vérifié"); break;
        case Updater::State::Checking: ImGui::TextColored(orange, "%s", msg.c_str()); break;
        case Updater::State::UpToDate: ImGui::TextColored(kGreen, "%s", msg.c_str()); break;
        case Updater::State::Error: ImGui::TextColored(kRed, "%s", msg.c_str()); break;
        case Updater::State::Available:
        case Updater::State::Downloading:
        case Updater::State::Ready: ImGui::TextColored(orange, "%s", msg.c_str()); break;
    }
    if (st == Updater::State::Available || st == Updater::State::Downloading || st == Updater::State::Ready) {
        ReleaseInfo r = up.Release();
        ImGui::Indent();
        ImGui::Text("%s%s", r.name.empty() ? ("Version " + r.version).c_str() : r.name.c_str(),
                    r.published.empty() ? "" : (" (publiée le " + r.published + ")").c_str());
        if (!r.notes.empty() && ImGui::TreeNode("Nouveautés de cette version")) {
            ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
            ImGui::TextUnformatted(r.notes.c_str());
            ImGui::PopTextWrapPos();
            ImGui::TreePop();
        }
        const bool installable = Updater::CanInstall(app_.Dirs().exeDir);
        if (st == Updater::State::Available && ImGui::Button("Télécharger la mise à jour")) up.Download(app_.Dirs().dataDir);
        if (st == Updater::State::Downloading) ImGui::ProgressBar(up.Progress(), ImVec2(320, 0));
        if (st == Updater::State::Ready) {
            ImGui::BeginDisabled(!installable);
            if (ImGui::Button("Installer et redémarrer le frontend")) {
                std::string err;
                if (!app_.InstallUpdate(err)) {
                    message_ = "Mise à jour impossible : " + err;
                    messageError_ = true;
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled("Le frontend se ferme, s'installe (environ 10 s), puis redémarre.");
        }
        if (!installable)
            ImGui::TextColored(orange, "Copie de développement (pas installée avec Installer.bat) : installation automatique "
                                       "désactivée, utiliser le paquet téléchargé.");
        ImGui::Unindent();
    }
}

void AdminUI::SectionShare() {
    ImGui::SeparatorText("Partager la configuration (autre borne, amis)");
    ImGui::TextWrapped("Un fichier .zip avec tes jeux (chemins, catégories, infos), les liens YouTube des vidéos, "
                       "les profils et les thèmes. L'autre borne l'importe et télécharge elle-même les vidéos.");
    ImGui::Checkbox("Inclure les images", &shareImages_);
    ImGui::SameLine();
    ImGui::Checkbox("Inclure les vidéos (fichier plus lourd)", &shareVideos_);
    ImGui::SameLine();
    ImGui::Checkbox("Inclure les profils et thèmes", &shareProfiles_);
    if (ImGui::Button("Exporter la configuration...")) {
        time_t t = time(nullptr);
        tm lt{};
        localtime_s(&lt, &t);
        char name[64];
        strftime(name, sizeof(name), "racecab-config-%Y-%m-%d.zip", &lt);
        SaveFile(Field::ShareExport, "Configuration Racecab (zip)", "zip", name);
    }
    Help("Ni l'historique, ni les statistiques, ni les réglages propres à la machine (type de borne, commandes, "
         "identifiants) ne sont exportés.");

    ImGui::SameLine();
    if (ImGui::Button("Importer une configuration...")) BrowseFile(Field::ShareImport, "Configuration Racecab (zip)", "zip");
    if (importFile_.empty()) return;

    ImGui::Indent();
    ImGui::Text("Fichier : %s", importFile_.c_str());
    if (!importError_.empty()) {
        ImGui::TextColored(kRed, "%s", importError_.c_str());
        if (ImGui::Button("Fermer")) importFile_.clear();
        ImGui::Unindent();
        return;
    }
    ImGui::Text("Exporté le %s : %d jeu(x), dont %d nouveau(x) et %d déjà présent(s) ; %d lien(s) YouTube, %d média(s), "
                "%d profil(s).", importExported_.c_str(), importGames_, importNew_, importExisting_, importLinks_,
                importMediaN_, importProfilesN_);
    ImGui::SetNextItemWidth(380);
    ImGui::InputText("Dossier des jeux dans le fichier", &importOldRoot_);
    ImGui::SetNextItemWidth(380);
    ImGui::InputText("Dossier des jeux ici", &importNewRoot_);
    ImGui::SameLine();
    if (ImGui::Button("Vérifier les chemins")) RefreshImportPreview();
    Help("Si tes jeux ne sont pas rangés au même endroit que sur l'autre borne, indique ici où ils sont : "
         "le début des chemins est remplacé (ex. D:\\Jeux -> E:\\Arcade).");
    if (!importMissing_.empty()) {
        std::string list;
        for (size_t i = 0; i < importMissing_.size() && i < 6; ++i) list += (i ? ", " : "") + importMissing_[i];
        if (importMissing_.size() > 6) list += std::format(" et {} autre(s)", importMissing_.size() - 6);
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Fichier de lancement introuvable ici pour %d jeu(x) : %s",
                           (int)importMissing_.size(), list.c_str());
        ImGui::TextDisabled("Ils sont importés quand même : chemin à corriger dans leur fiche si besoin.");
    } else {
        ImGui::TextColored(kGreen, "Tous les fichiers de lancement existent ici.");
    }
    ImGui::Checkbox("Profils et thèmes", &importProfiles_);
    ImGui::SameLine();
    ImGui::Checkbox("Images et vidéos du fichier", &importMedia_);
    ImGui::SameLine();
    ImGui::Checkbox("Télécharger les vidéos YouTube", &importVideos_);
    Help("Un jeu déjà présent (même nom ou même fichier) n'est jamais écrasé : il est seulement complété (lien vidéo, "
         "catégories, infos et médias manquants). Un profil du même nom est remplacé. Une sauvegarde est faite avant.");
    if (ImGui::Button("Importer")) {
        // Sauvegarde automatique avant import : retour possible depuis « Restaurer ».
        std::string bname, berr;
        backup::Create(app_.Dirs(), "avant-import", false, bname, berr);
        share::ImportOptions o;
        o.oldRoot = util::Trim(importOldRoot_);
        o.newRoot = util::Trim(importNewRoot_);
        o.profiles = importProfiles_;
        o.media = importMedia_;
        share::ImportResult r;
        std::string err;
        if (share::Import(app_.Lib(), app_.Dirs(), util::U8Path(importFile_), o, r, err)) {
            int queued = 0;
            if (importVideos_)
                for (int64_t id : r.needVideo)
                    if (const Game* g = app_.Lib().FindGame(id)) {
                        app_.Scrape().DownloadVideo(id, g->name, g->videoUrl, g->videoStart, app_.Lib().MediaDirFor(id),
                                                    g->videoDuration);
                        ++queued;
                    }
            app_.RefreshItems();
            if (importProfiles_) app_.ActivateProfile(app_.ProfileName());  // profil actif relu
            message_ = "Import terminé : " + r.summary +
                       (queued ? std::format(" ({} en cours de téléchargement, voir l'onglet Médias)", queued) : "");
            messageError_ = false;
            importFile_.clear();
        } else {
            message_ = "Import impossible : " + err;
            messageError_ = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Annuler l'import")) importFile_.clear();
    ImGui::Unindent();
}

// ---------------------------------------------------------------- Ajout automatique (scan d'un dossier)

void AdminUI::StartScan(const std::string& root) {
    if (scanBusy_) return;
    if (scanThread_.joinable()) scanThread_.join();
    scanRoot_ = root;
    scanRows_.clear();
    scanReady_ = false;
    scanOpen_ = true;
    scanCancel_ = false;
    scanBusy_ = true;
    const fs::path lbDb = app_.ScrapeConfig().lbDir / L"launchbox.db";
    scanThread_ = std::thread([this, root, lbDb] {
        std::vector<ScanCandidate> list;
        try {
            list = scanner::Scan(util::U8Path(root), scanCancel_, [this](const std::string& d) {
                std::lock_guard lk(scanM_);
                scanStatus_ = d;
            });
        } catch (const std::exception& e) {  // un scan ne doit jamais faire tomber le frontend
            logx::Error("Scan de {} interrompu : {}", root, e.what());
        }
        for (auto& c : list) {  // abréviations courantes : « id4 » -> « Initial D Arcade Stage 4 »
            c.rawName = c.name;
            if (std::string a = scanner::Alias(c.name); !a.empty()) c.name = a;
        }
        // Reconnaissance par la base LaunchBox locale (lecture seule, sans Internet)
        LaunchBoxDb db;
        std::string err;
        if (db.Open(lbDb, err))
            for (auto& c : list) {
                if (scanCancel_) break;
                {
                    std::lock_guard lk(scanM_);
                    scanStatus_ = "Reconnaissance : " + c.name;
                }
                auto found = db.Search(c.name, 10);
                bool exact = std::any_of(found.begin(), found.end(), [](auto& m) { return m.exact; });
                if (!exact && c.rawName != c.name) found = db.Search(c.rawName, 10);  // ex. « hummer » chez LaunchBox
                for (const auto& m : found) {
                    if (!m.exact) continue;
                    c.lbName = m.game.name;
                    c.lbYear = m.game.date.size() >= 4 ? std::atoi(m.game.date.substr(0, 4).c_str()) : 0;
                    c.lbPublisher = !m.game.publisher.empty() ? m.game.publisher : m.game.developer;
                    c.lbGenre = m.game.genres;
                    c.lbPlatform = m.game.platform;
                    c.lbOverview = m.game.overview;
                    break;
                }
            }
        std::lock_guard lk(scanM_);
        scanDone_ = std::move(list);
        scanReady_ = true;
        scanBusy_ = false;
    });
}

std::string AdminUI::CabsOf(int64_t category) {
    if (!category) return "aucune (sans catégorie)";
    Library& lib = app_.Lib();
    if (SDL_GetTicks() - cabProfilesAt_ > 2000 || cabProfiles_.empty()) {
        cabProfiles_.clear();
        std::error_code ec;
        for (auto& e : fs::directory_iterator(app_.Dirs().ProfilesDir(), ec)) {
            if (e.path().extension() != L".json") continue;
            Profile p;
            if (p.Load(e.path()) && p.mode != "none") cabProfiles_.push_back({p.mode, p.hiddenCategories});
        }
        cabProfilesAt_ = SDL_GetTicks();
    }
    bool race = false, gun = false;
    for (auto& [mode, hiddenList] : cabProfiles_) {
        bool hidden = false;
        for (int64_t h : hiddenList)
            if (h == category || lib.IsDescendant(category, h)) hidden = true;
        if (hidden) continue;
        if (mode.empty() || mode == "racecab") race = true;
        if (mode.empty() || mode == "guncab") gun = true;
    }
    if (race && gun) return "Racecab + GunCab";
    if (race) return "Racecab";
    if (gun) return "GunCab";
    return "aucune borne";
}

int64_t AdminUI::CabDefaultCategory(const std::string& cab) {
    // Catégorie visible seulement sur cette borne ; de préférence une sous-catégorie « Arcade ».
    int64_t best = 0;
    int bestScore = -1;
    for (const Category& c : app_.Lib().Categories()) {
        if (CabsOf(c.id) != cab) continue;
        int score = (app_.Lib().ChildCategories(c.id, true).empty() ? 2 : 0) +
                    (util::ToLower(c.name).find("arcade") != std::string::npos ? 1 : 0);
        if (score > bestScore) {
            best = c.id;
            bestScore = score;
        }
    }
    return best;
}

void AdminUI::WindowScan() {
    Library& lib = app_.Lib();
    // Résultats du thread : déjà présents, catégorie proposée selon le genre, cochés si reconnus
    {
        std::lock_guard lk(scanM_);
        if (scanReady_) {
            scanReady_ = false;
            const int64_t race = CabDefaultCategory("Racecab"), gun = CabDefaultCategory("GunCab");
            // Doublons dans la liste : un seul résultat par jeu (LinuxLoader > .exe > .bat sans pause > .bat avec pause)
            auto rank = [](const ScanCandidate& c) {
                return c.kind == "linuxloader" ? 4 : c.kind == "exe" ? 3 : c.runMode == RunMode::Normal ? 1 : 2;
            };
            std::map<std::string, size_t> bestOf;  // nom normalisé -> indice du meilleur résultat
            for (size_t i = 0; i < scanDone_.size(); ++i) {
                auto& c = scanDone_[i];
                // Nom officiel quand le jeu est reconnu, sauf s'il est plus court (« Initial D4 » < « Initial D Arcade Stage 4 »)
                if (!c.lbName.empty() && c.lbName.size() >= c.name.size()) c.name = c.lbName;
                std::string key = Scraper::Normalize(c.name);
                auto it = bestOf.find(key);
                if (it == bestOf.end()) bestOf[key] = i;
                else if (rank(c) > rank(scanDone_[it->second])) it->second = i;
            }
            for (size_t i = 0; i < scanDone_.size(); ++i) {
                auto& c = scanDone_[i];
                size_t best = bestOf[Scraper::Normalize(c.name)];
                if (best != i) c.dupOf = scanDone_[best].name + " (" + scanDone_[best].kind + ")";
            }
            for (auto& c : scanDone_) {
                ScanRow r;
                r.c = std::move(c);
                std::string lp = util::ToLower(r.c.launchPath), nm = Scraper::Normalize(r.c.name),
                            lbn = Scraper::Normalize(r.c.lbName), raw = Scraper::Normalize(r.c.rawName);
                // Déjà dans le frontend : même fichier + mêmes arguments, ou un des noms connus (dossier, abréviation,
                // nom officiel LaunchBox) identique au nom ou au nom alternatif d'un jeu présent
                for (const Game& g : lib.Games()) {
                    std::string gn = Scraper::Normalize(g.name), ga = Scraper::Normalize(g.altName);
                    auto same = [&](const std::string& n) { return !n.empty() && (n == gn || n == ga); };
                    if ((util::ToLower(g.launchPath) == lp && g.args == r.c.args) || same(nm) || same(lbn) || same(raw)) {
                        r.c.existing = true;
                        r.c.existingName = g.name;
                        break;
                    }
                }
                std::string genre = util::ToLower(r.c.lbGenre);
                if (genre.find("racing") != std::string::npos || genre.find("driving") != std::string::npos) r.cat = race;
                else if (genre.find("shoot") != std::string::npos || genre.find("gun") != std::string::npos) r.cat = gun;
                r.sel = !r.c.existing && r.c.dupOf.empty() && (!r.c.lbName.empty() || r.c.kind == "linuxloader");
                scanRows_.push_back(std::move(r));
            }
            scanDone_.clear();
        }
    }

    ImGui::SetNextWindowSize(ImVec2(1100, 560), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Ajout automatique de jeux", &scanOpen_)) {
        ImGui::End();
        return;
    }
    ImGui::Text("Dossier : %s", scanRoot_.c_str());
    if (scanBusy_) {
        std::string st;
        {
            std::lock_guard lk(scanM_);
            st = scanStatus_;
        }
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Recherche en cours... %s", st.c_str());
        if (ImGui::Button("Arrêter")) scanCancel_ = true;
        ImGui::End();
        return;
    }
    if (scanRows_.empty()) {
        ImGui::TextWrapped("Aucun jeu trouvé dans ce dossier.");
        if (ImGui::Button("Choisir un autre dossier...")) BrowseFolder(Field::ScanFolder);
        ImGui::End();
        return;
    }

    // Catégories (chemin lisible) pour les listes déroulantes
    std::vector<std::pair<int64_t, std::string>> cats{{0, "(aucune)"}};
    for (const Category& c : lib.Categories()) {
        std::string path = lib.NodePath(c.id);
        cats.push_back({c.id, path});
    }
    std::sort(cats.begin() + 1, cats.end(), [](auto& a, auto& b) { return a.second < b.second; });
    auto catName = [&](int64_t id) {
        for (auto& c : cats)
            if (c.first == id) return c.second;
        return std::string("(aucune)");
    };
    auto catCombo = [&](const char* label, int64_t& id) {
        bool changed = false;
        if (ImGui::BeginCombo(label, catName(id).c_str())) {
            for (auto& c : cats) {
                bool cur = c.first == id;
                if (ImGui::Selectable(c.second.c_str(), cur)) {
                    id = c.first;
                    changed = true;
                }
                if (cur) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        return changed;
    };

    int nSel = 0, nExist = 0, nLb = 0, nDup = 0;
    for (auto& r : scanRows_) {
        if (r.c.existing) r.sel = false;  // jamais d'ajout en double
        nSel += r.sel;
        nExist += r.c.existing;
        nDup += !r.c.existing && !r.c.dupOf.empty();
        nLb += !r.c.lbName.empty();
    }
    ImGui::Text("%d jeu(x) trouvé(s) : %d reconnu(s) par LaunchBox, %d déjà dans le frontend, %d doublon(s) dans la liste. "
                "%d coché(s).", (int)scanRows_.size(), nLb, nExist, nDup, nSel);
    if (ImGui::Button("Tout cocher"))
        for (auto& r : scanRows_) r.sel = !r.c.existing && r.c.dupOf.empty();
    ImGui::SameLine();
    if (ImGui::Button("Tout décocher"))
        for (auto& r : scanRows_) r.sel = false;
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220);
    catCombo("##bulk", scanBulkCat_);
    ImGui::SameLine();
    if (ImGui::Button("Appliquer aux jeux cochés"))
        for (auto& r : scanRows_)
            if (r.sel) r.cat = scanBulkCat_;
    ImGui::SameLine();
    if (ImGui::Button("Cochés -> Racecab")) {
        int64_t c = CabDefaultCategory("Racecab");
        for (auto& r : scanRows_)
            if (r.sel) r.cat = c;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cochés -> GunCab")) {
        int64_t c = CabDefaultCategory("GunCab");
        for (auto& r : scanRows_)
            if (r.sel) r.cat = c;
    }

    const float footer = ImGui::GetFrameHeightWithSpacing() * 2.4f;
    if (ImGui::BeginTable("scan", 7,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp,
                          ImVec2(0, -footer))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 24);
        ImGui::TableSetupColumn("Nom", ImGuiTableColumnFlags_WidthStretch, 2.2f);
        ImGui::TableSetupColumn("Reconnu (LaunchBox)", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("Catégorie", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn("Borne", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Lanceur", ImGuiTableColumnFlags_WidthStretch, 2.6f);
        ImGui::TableSetupColumn("Remarque", ImGuiTableColumnFlags_WidthStretch, 1.8f);
        ImGui::TableHeadersRow();
        ImGuiListClipper clip;
        clip.Begin((int)scanRows_.size());
        while (clip.Step())
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                ScanRow& r = scanRows_[(size_t)i];
                ImGui::PushID(i);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::BeginDisabled(r.c.existing);
                ImGui::Checkbox("##s", &r.sel);
                ImGui::EndDisabled();
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1);
                ImGui::InputText("##n", &r.c.name);
                ImGui::TableNextColumn();
                if (!r.c.lbName.empty()) {
                    ImGui::TextColored(kGreen, "%s", r.c.lbName.c_str());
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%d - %s - %s", r.c.lbYear, r.c.lbPublisher.c_str(), r.c.lbGenre.c_str());
                    if (r.c.name != r.c.lbName) {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Utiliser ce nom")) r.c.name = r.c.lbName;
                    }
                } else {
                    ImGui::TextDisabled("non reconnu");
                }
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1);
                catCombo("##c", r.cat);
                ImGui::TableNextColumn();
                std::string cab = CabsOf(r.cat);
                ImGui::TextColored(r.cat ? ImVec4(0.6f, 0.85f, 1.0f, 1.0f) : kRed, "%s", cab.c_str());
                ImGui::TableNextColumn();
                std::string shown = r.c.kind == "linuxloader" ? "LinuxLoader : " + r.c.args : r.c.launchPath;
                ImGui::TextUnformatted(shown.c_str());
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s %s\nDossier : %s", r.c.launchPath.c_str(), r.c.args.c_str(),
                                      r.c.workDir.empty() ? "(celui du lanceur)" : r.c.workDir.c_str());
                ImGui::TableNextColumn();
                const ImVec4 orange(1.0f, 0.8f, 0.2f, 1.0f);
                if (r.c.existing) ImGui::TextColored(orange, "déjà dans le frontend : %s", r.c.existingName.c_str());
                else if (!r.c.dupOf.empty()) ImGui::TextColored(orange, "doublon de %s", r.c.dupOf.c_str());
                else ImGui::TextDisabled("%s", r.c.note.c_str());
                if (ImGui::IsItemHovered() && !r.c.note.empty()) ImGui::SetTooltip("%s", r.c.note.c_str());
                ImGui::PopID();
            }
        ImGui::EndTable();
    }
    ImGui::Checkbox("Récupérer ensuite les logos, images et infos des jeux ajoutés", &scanFetchMedia_);
    int noCat = 0;
    for (auto& r : scanRows_) noCat += r.sel && !r.cat;
    if (noCat) {
        ImGui::SameLine();
        ImGui::TextColored(kRed, "%d jeu(x) coché(s) sans catégorie : ils n'apparaîtront sur aucune borne filtrée.", noCat);
    }
    ImGui::BeginDisabled(nSel == 0);
    if (ImGui::Button(std::format("Ajouter les {} jeu(x) coché(s)", nSel).c_str())) {
        std::vector<int64_t> added;
        int dup = 0;
        std::set<std::string> names;  // noms déjà présents ou ajoutés dans ce lot
        for (const Game& g : lib.Games()) names.insert(Scraper::Normalize(g.name));
        for (auto& r : scanRows_) {
            if (!r.sel) continue;
            if (lib.FindDuplicate(r.c.launchPath, r.c.args, 0) || !names.insert(Scraper::Normalize(r.c.name)).second) {
                ++dup;
                continue;
            }
            Game g;
            g.name = util::Trim(r.c.name);
            g.launchPath = r.c.launchPath;
            g.args = r.c.args;
            g.workDir = r.c.workDir;
            g.runMode = r.c.runMode;
            if (r.cat) g.categories.push_back(r.cat);
            g.year = r.c.lbYear;
            g.publisher = r.c.lbPublisher;
            g.platform = r.c.lbPlatform;
            g.description = r.c.lbOverview;
            std::string genre = util::ToLower(r.c.lbGenre);
            g.genre = genre.find("racing") != std::string::npos || genre.find("driving") != std::string::npos ? "Course"
                      : genre.find("shoot") != std::string::npos || genre.find("gun") != std::string::npos
                          ? "Tir"
                          : util::Trim(r.c.lbGenre.substr(0, r.c.lbGenre.find(';')));
            if (int64_t id = lib.AddGame(g)) added.push_back(id);
        }
        app_.RefreshItems();
        if (scanFetchMedia_ && !added.empty()) app_.StartBatch(added);
        message_ = std::format("{} jeu(x) ajouté(s){}{}.", added.size(), dup ? std::format(", {} doublon(s) ignoré(s)", dup) : "",
                               scanFetchMedia_ && !added.empty() ? " ; récupération des médias en cours (onglet Médias)" : "");
        messageError_ = false;
        scanRows_.clear();
        scanOpen_ = false;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Fermer")) scanOpen_ = false;
    ImGui::End();
}

void AdminUI::BrowseFolder(Field f) {
    SDL_ShowOpenFolderDialog(DialogCallback, new DialogCtx{this, f}, window_, nullptr, false);
}

void AdminUI::ApplyPendingDialog() {
    Field f;
    std::string p;
    {
        std::lock_guard lk(dlgMutex_);
        f = dlgField_;
        p = dlgPath_;
        dlgField_ = Field::None;
    }
    switch (f) {
        case Field::None: return;
        case Field::Launch:
            edit_.launchPath = p;
            if (edit_.name.empty()) {
                // Nom proposé : dossier parent (ex. H:\Arcade\InitialD\run.bat -> InitialD)
                fs::path pp = util::U8Path(p);
                std::string stem = util::PathU8(pp.stem());
                std::string low = util::ToLower(stem);
                edit_.name = (low == "run" || low == "launch" || low == "start" || low == "game")
                                 ? util::PathU8(pp.parent_path().filename())
                                 : stem;
            }
            break;
        case Field::WorkDir: edit_.workDir = p; break;
        case Field::Logo: edit_.logoPath = p; break;
        case Field::Fanart: edit_.fanartPath = p; break;
        case Field::Video: edit_.videoPath = p; break;
        case Field::ThemeBackground:
            app_.CurrentTheme().backgroundImage = p;
            app_.Textures()->Invalidate(p);
            break;
        case Field::ScanFolder:
            StartScan(p);
            break;
        case Field::ShareExport: {
            fs::path out = util::U8Path(p);
            if (out.extension().empty()) out += L".zip";
            share::ExportOptions o;
            o.images = shareImages_;
            o.videos = shareVideos_;
            o.profiles = shareProfiles_;
            std::string summary, err;
            if (share::Export(app_.Lib(), app_.Dirs(), o, out, summary, err)) {
                message_ = "Configuration exportée : " + util::PathU8(out) + " (" + summary + ")";
                messageError_ = false;
            } else {
                message_ = "Export impossible : " + err;
                messageError_ = true;
            }
            break;
        }
        case Field::ShareImport: {
            importFile_ = p;
            importChecked_ = false;
            importOldRoot_.clear();
            importNewRoot_.clear();
            RefreshImportPreview();
            if (importError_.empty()) {
                share::Preview pv;
                std::string err;
                share::ReadPreview(app_.Lib(), util::U8Path(importFile_), {}, {}, pv, err);
                importOldRoot_ = importNewRoot_ = pv.commonRoot;  // à modifier si les jeux sont ailleurs ici
            }
            break;
        }
        case Field::Import: {
            int added = 0, dup = 0;
            std::string err;
            if (app_.Lib().ImportList(util::U8Path(p), added, dup, err)) {
                message_ = std::format("Import terminé : {} jeu(x) ajouté(s), {} doublon(s) ignoré(s).", added, dup);
                messageError_ = false;
                app_.RefreshItems();
            } else {
                message_ = "Import impossible : " + err;
                messageError_ = true;
            }
            break;
        }
    }
}

void AdminUI::Render() {
    if (!ready_) return;
    ApplyPendingDialog();
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Size.x * 0.04f, vp->Size.y * 0.04f));
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x * 0.92f, vp->Size.y * 0.92f));
    bool keep = true;
    ImGui::Begin("Administration - Racecab Frontend", &keep, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
                                                                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
    if (ImGui::Button("Fermer (F1)")) keep = false;
    ImGui::SameLine();
    ImGui::TextDisabled("Données : %s", util::PathU8(app_.Dirs().dataDir).c_str());
    if (!message_.empty()) {
        ImGui::TextColored(messageError_ ? kRed : kGreen, "%s", message_.c_str());
    }
    if (ImGui::BeginTabBar("tabs")) {
        if (ImGui::BeginTabItem("Jeux", nullptr, TabFlag("Jeux"))) { TabGames(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Catégories", nullptr, TabFlag("Catégories"))) { TabCategories(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Médias", nullptr, TabFlag("Médias"))) { TabMedia(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Profils", nullptr, TabFlag("Profils"))) { TabProfiles(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Affichage", nullptr, TabFlag("Affichage"))) { TabDisplay(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Commandes", nullptr, TabFlag("Commandes"))) { TabInput(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Statistiques", nullptr, TabFlag("Statistiques"))) { TabStats(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Système", nullptr, TabFlag("Système"))) { TabSystem(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Diagnostic", nullptr, TabFlag("Diagnostic"))) { TabDiagnostic(); ImGui::EndTabItem(); }
        forceTab_.clear();
        ImGui::EndTabBar();
    }
    ImGui::End();
    if (scanOpen_) WindowScan();
    if (!keep) Close();

    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer_);
}

// ---------------------------------------------------------------- Jeux

void AdminUI::SelectGame(int64_t id) {
    const Game* g = app_.Lib().FindGame(id);
    if (!g) return;
    selectedGame_ = id;
    edit_ = *g;
    editing_ = true;
    isNew_ = false;
    // Lien YouTube mémorisé : champs préremplis
    auto mmss = [](int s) { return std::format("{}:{:02d}", s / 60, s % 60); };
    ytUrl_ = g->videoUrl;
    ytStart_ = g->videoUrl.empty() || g->videoStart <= 0 ? "" : mmss(g->videoStart);
    ytToEnd_ = !g->videoUrl.empty() && g->videoDuration == 0;
    ytDuration_ = g->videoDuration > 0 ? mmss(g->videoDuration) : "";
}

void AdminUI::NewGame() {
    edit_ = Game{};
    ytUrl_.clear();
    ytStart_.clear();
    ytDuration_.clear();
    ytToEnd_ = false;
    selectedGame_ = 0;
    editing_ = true;
    isNew_ = true;
}

bool AdminUI::SaveGame(bool allowDuplicate) {
    edit_.name = util::Trim(edit_.name);
    edit_.launchPath = util::Trim(edit_.launchPath);
    if (edit_.name.empty() || edit_.launchPath.empty()) {
        message_ = "Le nom et le fichier de lancement sont obligatoires.";
        messageError_ = true;
        return false;
    }
    if (!allowDuplicate) {
        if (auto dup = app_.Lib().FindDuplicate(edit_.launchPath, edit_.args, isNew_ ? 0 : edit_.id)) {
            ImGui::OpenPopup("Doublon");
            return false;
        }
    }
    bool ok;
    if (isNew_) {
        int64_t id = app_.Lib().AddGame(edit_);
        ok = id != 0;
        if (ok) SelectGame(id);
        if (ok && app_.ScrapeConfig().autoOnAdd) app_.StartBatch({id});  // recherche automatique à l'ajout
    } else {
        ok = app_.Lib().UpdateGame(edit_);
    }
    message_ = ok ? "Jeu enregistré." : "Erreur d'enregistrement (voir le journal).";
    messageError_ = !ok;
    if (ok) {
        if (app_.Textures()) {
            app_.Textures()->Invalidate(edit_.logoPath);
            app_.Textures()->Invalidate(edit_.fanartPath);
        }
        app_.RefreshItems();
    }
    return ok;
}

void AdminUI::TabGames() {
    Library& lib = app_.Lib();
    float leftW = ImGui::GetContentRegionAvail().x * 0.32f;
    ImGui::BeginChild("list", ImVec2(leftW, 0), ImGuiChildFlags_Borders);
    if (ImGui::Button("+ Ajouter un jeu")) NewGame();
    ImGui::SameLine();
    if (ImGui::Button("Importer une liste...")) BrowseFile(Field::Import, "Liste de jeux (JSON)", "json");
    ImGui::SameLine();
    if (ImGui::Button("Scanner un dossier...")) {
        if (scanBusy_) scanOpen_ = true;
        else BrowseFolder(Field::ScanFolder);
    }
    Help("Ajout automatique : choisis un dossier (ex. H:\\LinuxLoader). Les jeux trouvés s'affichent dans une liste : "
         "coche ceux à ajouter et choisis leur catégorie (donc la borne où ils apparaissent).");
    Help("Fichier JSON : voir docs/Import.md. Les doublons (même fichier + mêmes arguments) sont ignorés.");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", "Rechercher...", &filter_);

    std::vector<const Game*> list;
    for (const Game& g : lib.Games())
        if (Contains(g.name, filter_) || Contains(g.launchPath, filter_)) list.push_back(&g);
    std::sort(list.begin(), list.end(), [](auto a, auto b) { return util::ToLower(a->name) < util::ToLower(b->name); });
    ImGui::TextDisabled("%d jeu(x)", (int)list.size());
    ImGui::BeginChild("items");
    ImGuiListClipper clip;
    clip.Begin((int)list.size());
    while (clip.Step()) {
        for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
            const Game* g = list[(size_t)i];
            std::string label = g->name + (g->enabled ? "" : "  [désactivé]") + (util::FileExists(g->launchPath) ? "" : "  [introuvable]") +
                                "##" + std::to_string(g->id);
            if (ImGui::Selectable(label.c_str(), !isNew_ && g->id == selectedGame_)) SelectGame(g->id);
        }
    }
    ImGui::EndChild();
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("editor", ImVec2(0, 0), ImGuiChildFlags_Borders);
    if (editing_) GameEditor();
    else ImGui::TextDisabled("Sélectionnez un jeu ou ajoutez-en un.");
    ImGui::EndChild();
}

void AdminUI::CategoryChecklist(int64_t parent) {
    for (int64_t id : app_.Lib().ChildCategories(parent, true)) {
        if (id <= 0) continue;
        const Category* c = app_.Lib().FindCategory(id);
        bool has = std::find(edit_.categories.begin(), edit_.categories.end(), id) != edit_.categories.end();
        ImGui::PushID((int)id);
        if (ImGui::Checkbox(c->name.c_str(), &has)) {
            if (has) edit_.categories.push_back(id);
            else edit_.categories.erase(std::remove(edit_.categories.begin(), edit_.categories.end(), id), edit_.categories.end());
        }
        ImGui::Indent();
        CategoryChecklist(id);
        ImGui::Unindent();
        ImGui::PopID();
    }
}

void AdminUI::GameEditor() {
    ImGui::Text(isNew_ ? "Nouveau jeu" : "Modifier : %s", edit_.name.c_str());
    ImGui::Separator();
    const float fieldW = ImGui::GetContentRegionAvail().x * 0.65f;

    auto pathField = [&](const char* label, std::string& value, Field f, bool folder, const char* filterName,
                         const char* pattern, bool mustExist) {
        ImGui::PushID(label);
        ImGui::SetNextItemWidth(fieldW);
        ImGui::InputText(label, &value);
        ImGui::SameLine();
        if (ImGui::Button("Parcourir...")) folder ? BrowseFolder(f) : BrowseFile(f, filterName, pattern);
        if (!value.empty()) {
            std::error_code ec;
            bool ok = folder ? fs::is_directory(util::U8Path(value), ec) : util::FileExists(value);
            ImGui::SameLine();
            ImGui::TextColored(ok ? kGreen : kRed, ok ? "OK" : "introuvable");
        } else if (mustExist) {
            ImGui::SameLine();
            ImGui::TextColored(kRed, "obligatoire");
        }
        ImGui::PopID();
    };

    ImGui::SetNextItemWidth(fieldW);
    ImGui::InputText("Nom *", &edit_.name);
    pathField("Fichier .bat / .exe *", edit_.launchPath, Field::Launch, false, "Jeux (.bat, .cmd, .exe)", "bat;cmd;exe", true);
    ImGui::SetNextItemWidth(fieldW);
    ImGui::InputText("Arguments", &edit_.args);
    pathField("Répertoire de travail", edit_.workDir, Field::WorkDir, true, nullptr, nullptr, false);
    Help("Vide = dossier du fichier lancé.");
    const char* modes[] = {"Automatique (.bat masqué, .exe normal)", "Normal", "Réduit", "Masqué"};
    int mode = (int)edit_.runMode;
    ImGui::SetNextItemWidth(fieldW);
    if (ImGui::Combo("Mode d'exécution", &mode, modes, 4)) edit_.runMode = (RunMode)mode;
    ImGui::Checkbox("Activé", &edit_.enabled);
    ImGui::SameLine();
    ImGui::Checkbox("Favori", &edit_.favorite);

    if (ImGui::CollapsingHeader("Catégories", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (app_.Lib().Categories().empty()) ImGui::TextDisabled("Aucune catégorie : créez-en dans l'onglet Catégories.");
        CategoryChecklist(Library::kRoot);
    }
    if (ImGui::CollapsingHeader("Lancement avancé")) {
        ImGui::SetNextItemWidth(fieldW * 0.4f);
        ImGui::InputInt("Délai avant lancement (ms)", &edit_.launchDelayMs, 100, 1000);
        edit_.launchDelayMs = std::clamp(edit_.launchDelayMs, 0, 60000);
        ImGui::SetNextItemWidth(fieldW);
        ImGui::InputText("Commande avant", &edit_.preCmd);
        Help("Exécutée via cmd.exe avant le jeu, attendue jusqu'à 60 s. Ex. : changer de profil de volant.");
        ImGui::SetNextItemWidth(fieldW);
        ImGui::InputText("Commande après", &edit_.postCmd);
        Help("Exécutée après la fin réelle du jeu, attendue jusqu'à 60 s.");
        ImGui::SetNextItemWidth(fieldW * 0.5f);
        ImGui::InputText("Processus à surveiller", &edit_.watchProcess);
        Help("Facultatif. Pour les lanceurs qui démarrent le jeu hors de leur arborescence (ex. game.exe) : "
             "le frontend attend en plus la fermeture de ce processus.");
    }
    if (ImGui::CollapsingHeader("Métadonnées")) {
        ImGui::SetNextItemWidth(fieldW);
        ImGui::InputText("Nom alternatif", &edit_.altName);
        ImGui::SetNextItemWidth(fieldW * 0.3f);
        ImGui::InputInt("Année", &edit_.year, 0);
        ImGui::SetNextItemWidth(fieldW);
        ImGui::InputText("Éditeur", &edit_.publisher);
        ImGui::SetNextItemWidth(fieldW);
        ImGui::InputText("Genre", &edit_.genre);
        ImGui::SetNextItemWidth(fieldW);
        ImGui::InputText("Plateforme", &edit_.platform);
        ImGui::InputTextMultiline("Description", &edit_.description, ImVec2(fieldW, ImGui::GetTextLineHeight() * 6));
    }
    if (ImGui::CollapsingHeader("Médias")) {
        pathField("Logo (PNG transparent)", edit_.logoPath, Field::Logo, false, "Images", "png;webp;jpg;jpeg", false);
        pathField("Fond / fanart", edit_.fanartPath, Field::Fanart, false, "Images", "png;webp;jpg;jpeg", false);
        pathField("Vidéo", edit_.videoPath, Field::Video, false, "Vidéos", "mp4;mkv;avi", false);
        if (!isNew_) {
            // Vidéo depuis un lien YouTube choisi à la main (extrait téléchargé avec yt-dlp).
            const ScraperConfig& sc = app_.ScrapeConfig();
            const bool yt = !ytvideo::FindYtDlp(sc).empty();
            ImGui::SetNextItemWidth(fieldW * 0.75f);
            ImGui::InputTextWithHint("Lien YouTube", "https://www.youtube.com/watch?v=...", &ytUrl_);
            ImGui::SetNextItemWidth(90);
            ImGui::InputTextWithHint("Début", "0:00", &ytStart_);
            ImGui::SameLine();
            ImGui::BeginDisabled(ytToEnd_);
            ImGui::SetNextItemWidth(90);
            ImGui::InputTextWithHint("Durée", std::format("{}:{:02d}", sc.videoSeconds / 60, sc.videoSeconds % 60).c_str(),
                                     &ytDuration_);
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::Checkbox("Jusqu'à la fin", &ytToEnd_);
            int start = ytvideo::ParseStart(ytStart_);
            if (start < 0) start = ytvideo::StartFromUrl(ytUrl_);
            if (start < 0) start = 0;
            int duration = ytToEnd_ ? 0 : ytvideo::ParseStart(ytDuration_);  // vide = durée des réglages
            if (!ytToEnd_ && duration == 0) duration = -1;
            const std::string st = app_.Scrape().Status();
            const bool busy = st.rfind("Vidéo YouTube", 0) == 0;
            ImGui::BeginDisabled(!yt || util::Trim(ytUrl_).empty() || busy);
            if (ImGui::Button("Télécharger l'extrait")) {
                app_.Scrape().DownloadVideo(edit_.id, edit_.name, util::Trim(ytUrl_), start,
                                            app_.Dirs().MediaDir() / std::to_wstring(edit_.id), duration);
                edit_.videoPath.clear();  // la vidéo téléchargée remplace le fichier choisi
                edit_.videoUrl = util::Trim(ytUrl_);  // sinon « Enregistrer » effacerait le lien mémorisé
                edit_.videoStart = start;
                edit_.videoDuration = duration;
                const int d = duration < 0 ? sc.videoSeconds : duration;
                message_ = d == 0 ? std::format("Vidéo en cours de téléchargement (de {}:{:02d} jusqu'à la fin).", start / 60,
                                                start % 60)
                                  : std::format("Vidéo en cours de téléchargement ({}:{:02d} à partir de {}:{:02d}).", d / 60,
                                                d % 60, start / 60, start % 60);
                messageError_ = false;
            }
            ImGui::EndDisabled();
            if (!yt) ImGui::TextColored(kRed, "yt-dlp non installé : voir l'onglet Médias.");
            // État : en cours, puis résultat du dernier téléchargement de ce jeu.
            const std::string& last = app_.LastFetchMessage();
            if (busy) {
                static const char* dots[] = {"", ".", "..", "..."};
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Téléchargement en cours%s (environ 10 s à 2 min)",
                                   dots[(SDL_GetTicks() / 400) % 4]);
            } else if (!last.empty() && last.rfind(edit_.name + " : vidéo", 0) == 0) {
                const bool good = last.find("enregistr") != std::string::npos;
                ImGui::TextColored(good ? kGreen : kRed, "%s", last.c_str() + edit_.name.size() + 3);
            }
            Help("Colle le lien d'une vidéo YouTube de gameplay. Début : moment où commence l'extrait (ex. 1:30). "
                 "Durée : longueur gardée (ex. 1:51), vide = durée des réglages ; « Jusqu'à la fin » garde tout "
                 "à partir du début. "
                 "Un lien copié avec « Partager à partir de… » (t=90) donne aussi le début. L'extrait remplace la vidéo "
                 "actuelle du jeu. Usage personnel uniquement.");
        }
        if (!isNew_)
            ImGui::TextDisabled("Si vide : %s\\%lld\\logo.png, fanart.jpg", util::PathU8(app_.Dirs().MediaDir()).c_str(),
                                (long long)edit_.id);
    }
    if (!isNew_) {
        ImGui::Separator();
        ImGui::TextDisabled("Lancé %d fois  -  temps de jeu %s", edit_.playCount, util::FormatDuration(edit_.playSeconds).c_str());
    }

    ImGui::Separator();
    if (ImGui::Button("Enregistrer")) SaveGame(false);
    ImGui::SameLine();
    if (ImGui::Button("Annuler")) {
        if (isNew_) editing_ = false;
        else SelectGame(selectedGame_);
    }
    if (!isNew_) {
        ImGui::SameLine();
        if (ImGui::Button("Tester le lancement")) {
            SaveGame(false);
            app_.LaunchGame(edit_.id);
        }
        ImGui::SameLine();
        if (ImGui::Button("Retirer de la bibliothèque")) ImGui::OpenPopup("Retirer");
    }

    if (ImGui::BeginPopupModal("Doublon", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Un autre jeu utilise déjà ce fichier avec les mêmes arguments.");
        if (ImGui::Button("Enregistrer quand même")) {
            SaveGame(true);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Annuler")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("Retirer", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Retirer « %s » de la bibliothèque ?", edit_.name.c_str());
        ImGui::TextDisabled("Les fichiers du jeu ne sont PAS supprimés du disque.");
        if (ImGui::Button("Retirer")) {
            if (app_.Lib().RemoveGame(edit_.id)) {
                message_ = "Jeu retiré de la bibliothèque.";
                messageError_ = false;
                editing_ = false;
                selectedGame_ = 0;
                app_.RefreshItems();
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Annuler")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------- Profils

void AdminUI::CategoryVisibility(int64_t parent, Profile& p, int depth) {
    for (int64_t id : app_.Lib().ChildCategories(parent, true)) {
        if (id <= 0) continue;
        const Category* c = app_.Lib().FindCategory(id);
        bool visible = std::find(p.hiddenCategories.begin(), p.hiddenCategories.end(), id) == p.hiddenCategories.end();
        ImGui::PushID((int)id);
        ImGui::Indent(depth ? 20.0f : 0.0f);
        if (ImGui::Checkbox(c->name.c_str(), &visible)) {
            if (visible) p.hiddenCategories.erase(std::remove(p.hiddenCategories.begin(), p.hiddenCategories.end(), id), p.hiddenCategories.end());
            else p.hiddenCategories.push_back(id);
        }
        if (visible) CategoryVisibility(id, p, depth + 1);  // une catégorie masquée masque ses sous-catégories
        ImGui::Unindent(depth ? 20.0f : 0.0f);
        ImGui::PopID();
    }
}

void AdminUI::AttractCategories(int64_t parent, Profile& p, int depth) {
    for (int64_t id : app_.Lib().ChildCategories(parent, true)) {
        if (id <= 0 || app_.Lib().CategoryHidden(id)) continue;
        const Category* c = app_.Lib().FindCategory(id);
        auto& v = p.attract.categories;
        bool in = std::find(v.begin(), v.end(), id) != v.end();
        ImGui::PushID((int)(id + 1000000));
        ImGui::Indent(depth ? 20.0f : 0.0f);
        if (ImGui::Checkbox(c->name.c_str(), &in)) {
            if (in) v.push_back(id);
            else v.erase(std::remove(v.begin(), v.end(), id), v.end());
        }
        AttractCategories(id, p, depth + 1);
        ImGui::Unindent(depth ? 20.0f : 0.0f);
        ImGui::PopID();
    }
}

void AdminUI::TabStats() {
    Library& lib = app_.Lib();
    Database::Totals t = lib.Db().PlayTotals();
    auto date = [](int64_t unix, bool withTime) {
        if (unix <= 0) return std::string("-");
        time_t tt = (time_t)unix;
        tm lt{};
        localtime_s(&lt, &tt);
        char b[32];
        strftime(b, sizeof(b), withTime ? "%d/%m/%Y %H:%M" : "%d/%m/%Y", &lt);
        return std::string(b);
    };
    ImGui::SeparatorText("Totaux (historique)");
    ImGui::Text("%lld partie(s), %s de jeu, %lld jeu(x) différent(s)", (long long)t.plays,
                util::FormatDuration(t.seconds).c_str(), (long long)t.gamesPlayed);
    if (t.firstPlay) ImGui::TextDisabled("Depuis le %s", date(t.firstPlay, false).c_str());
    ImGui::TextDisabled("L'historique détaillé commence avec cette version ; les compteurs par jeu incluent les parties antérieures.");

    std::vector<const Game*> games;
    for (const Game& g : lib.Games())
        if (g.playCount > 0) games.push_back(&g);
    auto top = [&](const char* id, const char* title, auto key, auto value) {
        std::sort(games.begin(), games.end(), [&](auto a, auto b) { return key(a) > key(b); });
        ImGui::SeparatorText(title);
        if (games.empty()) {
            ImGui::TextDisabled("Aucun jeu lancé pour l'instant.");
            return;
        }
        if (ImGui::BeginTable(id, 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
            ImGui::TableSetupColumn("Jeu");
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 160);
            for (size_t i = 0; i < games.size() && i < 10; ++i) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%zu. %s", i + 1, games[i]->name.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(value(games[i]).c_str());
            }
            ImGui::EndTable();
        }
    };
    ImGui::Columns(2, nullptr, false);
    top("topTime", "Les plus joués (temps)", [](const Game* g) { return g->playSeconds; },
        [](const Game* g) { return util::FormatDuration(g->playSeconds); });
    ImGui::NextColumn();
    top("topCount", "Les plus lancés", [](const Game* g) { return g->playCount; },
        [](const Game* g) { return std::format("{} fois", g->playCount); });
    ImGui::Columns(1);

    ImGui::SeparatorText("Historique (100 dernières parties)");
    auto hist = lib.Db().History(100);
    if (hist.empty()) ImGui::TextDisabled("Aucune partie enregistrée depuis l'installation de cette version.");
    else if (ImGui::BeginTable("hist", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY,
                                 ImVec2(0, 260))) {
        ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_WidthFixed, 140);
        ImGui::TableSetupColumn("Jeu");
        ImGui::TableSetupColumn("Durée", ImGuiTableColumnFlags_WidthFixed, 110);
        ImGui::TableSetupColumn("Profil", ImGuiTableColumnFlags_WidthFixed, 140);
        ImGui::TableHeadersRow();
        for (const auto& h : hist) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(date(h.started, true).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(h.name.empty() ? "(jeu supprimé)" : h.name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(util::FormatDuration(h.seconds).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(h.profile.c_str());
        }
        ImGui::EndTable();
    }
    if (ImGui::Button("Effacer l'historique et remettre les compteurs à zéro")) ImGui::OpenPopup("RazStats");
    if (ImGui::BeginPopupModal("RazStats", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Effacer définitivement l'historique, les compteurs de lancements et les temps de jeu ?");
        ImGui::TextDisabled("Les jeux, favoris et médias ne sont pas touchés. Une sauvegarde permet de revenir en arrière.");
        if (ImGui::Button("Effacer")) {
            bool ok = lib.ClearStats();
            message_ = ok ? "Statistiques remises à zéro." : "Échec de la remise à zéro (voir le journal).";
            messageError_ = !ok;
            app_.RefreshItems();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Annuler")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void AdminUI::TabSystem() {
    Settings& s = app_.Config();
    ImGui::SeparatorText("Type de borne");
    int mode = app_.GunMode() ? 1 : 0;
    if (ImGui::RadioButton("Racecab (volant, boutons)", mode == 0) && mode != 0) app_.SetCabMode("racecab");
    ImGui::SameLine();
    if (ImGui::RadioButton("GunCab (pistolets)", mode == 1) && mode != 1) app_.SetCabMode("guncab");
    Help("Détermine le profil de démarrage, les profils proposés sur la borne et les commandes. En GunCab, les pistolets "
         "en mode souris (Retro Shooter RS3, Sinden, Gun4IR...) visent et tirent : gâchette = choisir, recharge / tir hors "
         "écran (clic droit) = retour, maintenu = menu. Boutons Retour et Menu affichés à l'écran.");
    if (app_.GunMode()) {
        ImGui::Indent();
        if (ImGui::Checkbox("Disposition pistolet : grande image + flèches, et mosaïque", &s.gun.arcadeLayout)) app_.SaveSettings();
        Help("Grande image (vidéo) au centre, bandes flèche gauche / droite pour changer de jeu, bouton Mosaïque pour voir "
             "plusieurs jeux. Décoché : la roue classique, comme sur la Racecab.");
        if (s.gun.arcadeLayout) {
            ImGui::SetNextItemWidth(120);
            if (ImGui::SliderInt("colonnes de la mosaïque", &s.gun.mosaicCols, 2, 6)) app_.SaveSettings();
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120);
            if (ImGui::SliderInt("lignes", &s.gun.mosaicRows, 1, 4)) app_.SaveSettings();
        }
        if (ImGui::Checkbox("Afficher le viseur", &s.gun.crosshair)) app_.SaveSettings();
        if (s.gun.crosshair) {
            static const char* kColors[] = {"white", "yellow", "green", "red", "theme"};
            const char* colorLabels[] = {"Blanc", "Jaune", "Vert", "Rouge", "Couleur du thème"};
            int ci = 0;
            for (int i = 0; i < 5; ++i)
                if (s.gun.crosshairColor == kColors[i]) ci = i;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(170);
            if (ImGui::Combo("couleur", &ci, colorLabels, 5)) {
                s.gun.crosshairColor = kColors[ci];
                app_.SaveSettings();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(150);
            if (ImGui::SliderInt("taille (%)", &s.gun.crosshairSize, 60, 200)) app_.SaveSettings();
            Help("Le viseur a toujours un contour noir. Choisir une couleur qui tranche avec le thème : blanc ou jaune "
                 "sur les thèmes néon.");
        }
        ImGui::SetNextItemWidth(200);
        if (ImGui::SliderInt("Bordure blanche (% de la hauteur)", &s.gun.borderPercent, 0, 6)) app_.SaveSettings();
        Help("Pour les pistolets Sinden, qui repèrent l'écran grâce à une bordure. Inutile avec les Retro Shooter RS3 "
             "(barre de LED infrarouges) : laisser à 0.");
        ImGui::SetNextItemWidth(200);
        if (ImGui::SliderInt("Délai avant lancement (ms)", &s.gun.launchGuardMs, 0, 2000)) app_.SaveSettings();
        Help("Tirer sur un jeu le sélectionne ; tirer à nouveau dessus le lance, seulement après ce délai "
             "(évite de lancer un jeu par deux tirs rapprochés).");
        ImGui::Unindent();
    }

    ImGui::SeparatorText("Démarrage et mode kiosque");
    bool autoStart = sys::AutoStartEnabled();
    if (ImGui::Checkbox("Lancer le frontend au démarrage de Windows", &autoStart)) {
        std::string err;
        bool ok = sys::SetAutoStart(autoStart, app_.Dirs().dataDir, err);
        message_ = ok ? (autoStart ? "Le frontend démarrera avec Windows (relancé automatiquement en cas de plantage)."
                                   : "Démarrage automatique désactivé.")
                      : "Démarrage automatique : " + err;
        messageError_ = !ok;
    }
    Help("Ajoute le frontend au démarrage de la session Windows de cet utilisateur (sans droits administrateur). "
         "Il est lancé sous surveillance : en cas de plantage, il est relancé automatiquement.");
    if (ImGui::Checkbox("Mode kiosque (borne publique)", &s.kiosk)) {
        if (s.kiosk) {
            s.fullscreen = true;
            s.hideCursor = true;
            app_.ApplyWindowSettings();
        }
        app_.SaveSettings();
    }
    Help("Plein écran imposé, curseur masqué, « Quitter » retiré du menu de la borne. Pour quitter : maintenir Retour "
         "5 secondes (puis confirmer), ou ce panneau (F1).");
    if (ImGui::Button("Quitter le frontend")) app_.RequestQuit();

    SectionUpdates();
    SectionShare();

    ImGui::SeparatorText("Sauvegarde de la configuration");
    if (ImGui::Checkbox("Sauvegarde automatique (une fois par jour, au démarrage)", &s.autoBackup)) app_.SaveSettings();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    if (ImGui::InputInt("conservées", &s.backupKeep)) {
        s.backupKeep = std::clamp(s.backupKeep, 1, 100);
        app_.SaveSettings();
    }
    if (ImGui::Button("Créer une sauvegarde maintenant")) {
        std::string name, err;
        bool ok = backup::Create(app_.Dirs(), "sauvegarde", backupWithMedia_, name, err);
        message_ = ok ? "Sauvegarde créée : " + name : "Sauvegarde impossible : " + err;
        messageError_ = !ok;
    }
    ImGui::SameLine();
    ImGui::Checkbox("avec les médias", &backupWithMedia_);
    Help("Contient la bibliothèque (jeux, catégories, statistiques), les réglages, profils, commandes et thèmes. "
         "Les médias sont facultatifs (volumineux, et récupérables à nouveau). Fichiers dans <données>\\backups.");
    ImGui::SameLine();
    if (ImGui::Button("Ouvrir le dossier")) {
        std::error_code ec;
        fs::create_directories(app_.Dirs().BackupsDir(), ec);
        ShellExecuteW(nullptr, L"open", app_.Dirs().BackupsDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    auto list = backup::List(app_.Dirs());
    if (list.empty()) ImGui::TextDisabled("Aucune sauvegarde.");
    else if (ImGui::BeginTable("backups", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY,
                                 ImVec2(0, 200))) {
        ImGui::TableSetupColumn("Sauvegarde");
        ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_WidthFixed, 140);
        ImGui::TableSetupColumn("Taille", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < list.size(); ++i) {
            ImGui::PushID((int)i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(list[i].name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(list[i].date.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%.1f Mo", list[i].size / 1048576.0);
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Restaurer")) {
                pendingRestore_ = util::PathU8(list[i].file);
                ImGui::OpenPopup("Restaurer");
            }
            if (ImGui::BeginPopupModal("Restaurer", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Restaurer « %s » ?", util::PathU8(util::U8Path(pendingRestore_).filename()).c_str());
                ImGui::TextDisabled("La configuration actuelle est d'abord sauvegardée (« avant-restauration »).\n"
                                    "Le frontend redémarre pour appliquer la restauration.");
                if (ImGui::Button("Restaurer et redémarrer")) {
                    std::string err;
                    if (backup::ScheduleRestore(app_.Dirs(), util::U8Path(pendingRestore_), err)) app_.RequestRestart();
                    else {
                        message_ = "Restauration impossible : " + err;
                        messageError_ = true;
                    }
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Annuler")) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void AdminUI::TabProfiles() {
    Profile& p = app_.CurrentProfile();
    Settings& s = app_.Config();

    ImGui::SeparatorText("Profil actif");
    std::vector<std::string> list = Profile::List(app_.Dirs().ProfilesDir());
    ImGui::SetNextItemWidth(300);
    if (ImGui::BeginCombo("Profil", app_.ProfileName().c_str())) {
        for (const std::string& n : list)
            if (ImGui::Selectable(n.c_str(), n == app_.ProfileName())) {
                app_.ActivateProfile(n);
                message_ = "Profil actif : " + n;
                messageError_ = false;
            }
        ImGui::EndCombo();
    }
    Help("Chaque profil a son thème, ses catégories visibles, son mapping des commandes, ses réglages vidéo et de démonstration.");
    if (ImGui::Checkbox("Choisir le profil au démarrage", &s.askProfileAtStartup)) app_.SaveSettings();
    ImGui::SameLine();
    ImGui::TextDisabled("(sinon : le dernier profil utilisé)");
    {
        // "none" : profil gardé mais proposé sur aucune borne (FitsMode ne l'accepte jamais).
        static const char* kModes[] = {"", "racecab", "guncab", "none"};
        const char* labels[] = {"Les deux types de borne", "Racecab seulement", "GunCab seulement", "Aucune borne (masqué)"};
        int m = p.mode == "racecab" ? 1 : p.mode == "guncab" ? 2 : p.mode == "none" ? 3 : 0;
        ImGui::SetNextItemWidth(300);
        if (ImGui::Combo("Proposé sur", &m, labels, 4)) {
            p.mode = kModes[m];
            app_.SaveProfile();
        }
        Help("Sur une borne, seuls les profils de son type (Administration > Système) sont proposés au volant ou au pistolet.");
        static const char* kSorts[] = {"name", "year", "played", "count"};
        const char* sortLabels[] = {"Nom", "Année", "Temps de jeu", "Nombre de lancements"};
        int so = 0;
        for (int i = 0; i < 4; ++i)
            if (p.sort == kSorts[i]) so = i;
        ImGui::SetNextItemWidth(300);
        if (ImGui::Combo("Tri des jeux", &so, sortLabels, 4)) {
            p.sort = kSorts[so];
            app_.SaveProfile();
        }
    }

    ImGui::SetNextItemWidth(250);
    ImGui::InputTextWithHint("##newprof", "Nom du nouveau profil", &newProfileName_);
    ImGui::SameLine();
    if (ImGui::Button("Créer (copie du profil actif)") && !util::Trim(newProfileName_).empty()) {
        std::string nn = util::Trim(newProfileName_);
        std::error_code ec;
        if (fs::exists(app_.Dirs().ProfileFile(nn), ec)) {
            message_ = "Ce profil existe déjà.";
            messageError_ = true;
        } else {
            p.Save(app_.Dirs().ProfileFile(nn));
            fs::copy_file(app_.CurrentInputFile(), app_.Dirs().InputFileFor(nn), ec);
            app_.ActivateProfile(nn);
            message_ = "Profil « " + nn + " » créé.";
            messageError_ = false;
            newProfileName_.clear();
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(list.size() <= 1);
    if (ImGui::Button("Supprimer ce profil")) ImGui::OpenPopup("SupprProfil");
    ImGui::EndDisabled();
    if (ImGui::BeginPopupModal("SupprProfil", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Supprimer le profil « %s » (réglages, mapping des commandes, état) ?", app_.ProfileName().c_str());
        ImGui::TextDisabled("La bibliothèque, les médias et les thèmes ne sont pas touchés.");
        if (ImGui::Button("Supprimer")) {
            std::string gone = app_.ProfileName();
            std::error_code ec;
            fs::remove(app_.Dirs().ProfileFile(gone), ec);
            fs::remove(app_.Dirs().InputFileFor(gone), ec);
            fs::remove(app_.Dirs().StateFileFor(gone), ec);
            auto rest = Profile::List(app_.Dirs().ProfilesDir());
            if (!rest.empty()) app_.ActivateProfile(rest.front());
            message_ = "Profil « " + gone + " » supprimé.";
            messageError_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Annuler")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SeparatorText("Catégories visibles dans ce profil");
    if (app_.Lib().Categories().empty()) ImGui::TextDisabled("Aucune catégorie.");
    CategoryVisibility(Library::kRoot, p, 0);
    ImGui::Checkbox("Ne montrer que les jeux d'une catégorie visible", &p.filterGames);
    Help("Coché : « Tous les jeux », Favoris et Récemment joués ne contiennent que les jeux rangés dans au moins une "
         "catégorie visible (ex. un profil GunCab qui ne montre que les jeux de tir). Sans effet si rien n'est masqué.");
    ImGui::Checkbox("Afficher Favoris et Récemment joués", &p.showAutoCategories);

    ImGui::SeparatorText("Mode démonstration (attract mode)");
    AttractSettings& a = p.attract;
    ImGui::Checkbox("Activé", &a.enabled);
    ImGui::SameLine();
    ImGui::Checkbox("Ordre aléatoire", &a.random);
    ImGui::SameLine();
    ImGui::Checkbox("Vidéos", &a.video);
    ImGui::SameLine();
    ImGui::Checkbox("Muet", &a.mute);
    ImGui::SetNextItemWidth(300); ImGui::SliderInt("Inactivité avant démarrage (s)", &a.idleSeconds, 10, 1800);
    ImGui::SetNextItemWidth(300); ImGui::SliderInt("Durée par jeu (s)", &a.displaySeconds, 3, 120);
    ImGui::SetNextItemWidth(300); ImGui::SliderFloat("Volume", &a.volume, 0.0f, 1.0f, "%.2f");
    ImGui::SetNextItemWidth(300); ImGui::SliderFloat("Vitesse de défilement", &a.speed, 0.1f, 2.0f, "%.1f");
    ImGui::TextDisabled("La démonstration s'arrête à la moindre commande et ne lance jamais de jeu.");
    if (ImGui::TreeNode("Catégories présentées (aucune cochée = toutes celles du profil)")) {
        AttractCategories(Library::kRoot, p, 0);
        ImGui::TreePop();
    }

    if (ImGui::Button("Enregistrer le profil")) {
        app_.SaveProfile();
        message_ = "Profil « " + app_.ProfileName() + " » enregistré.";
        messageError_ = false;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", util::PathU8(app_.Dirs().ProfileFile(app_.ProfileName())).c_str());
}

// ---------------------------------------------------------------- Médias

void AdminUI::CandidateTable(const SearchResult& r, bool& consumed) {
    if (!r.ok) {
        ImGui::TextColored(kRed, "%s", r.error.c_str());
        return;
    }
    if (r.candidates.empty()) {
        ImGui::TextDisabled("Aucun résultat pour « %s » sur %s.", r.query.c_str(), ProviderName(r.provider));
        return;
    }
    std::string id = std::format("cand{}_{}", r.gameId, (int)r.provider);
    if (ImGui::BeginTable(id.c_str(), 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Nom");
        ImGui::TableSetupColumn("Année", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Plateforme", ImGuiTableColumnFlags_WidthFixed, 220);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 110);
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < r.candidates.size(); ++i) {
            const ScrapeCandidate& c = r.candidates[i];
            ImGui::PushID((int)i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(c.name.c_str());
            if (!c.note.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("(%s)", c.note.c_str());
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(c.year.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(c.platform.c_str());
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Utiliser")) {
                app_.Scrape().Fetch(r.gameId, c, app_.Lib().MediaDirFor(r.gameId), overwriteMedia_);
                consumed = true;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void AdminUI::TabMedia() {
    ScraperConfig& cfg = app_.ScrapeConfig();
    Library& lib = app_.Lib();

    ImGui::SeparatorText("Services");
    ImGui::Checkbox("Récupération des médias autorisée", &cfg.enabled);
    ImGui::SameLine();
    ImGui::Checkbox("Rechercher automatiquement à l'ajout d'un jeu", &cfg.autoOnAdd);
    ImGui::SameLine();
    ImGui::Checkbox("Télécharger les vidéos", &cfg.wantVideo);
    static const char* kPrefIds[] = {"launchbox", "screenscraper", "steamgriddb"};
    int pref = 0;
    for (int i = 0; i < 3; ++i)
        if (cfg.preferred == kPrefIds[i]) pref = i;
    const char* prefs[] = {"LaunchBox (sans compte : métadonnées, logo, fond, capture, vidéo avec yt-dlp)",
                           "ScreenScraper (métadonnées, logo, fond, vidéo)", "SteamGridDB (logo, fond)"};
    ImGui::SetNextItemWidth(420);
    if (ImGui::Combo("Source préférée", &pref, prefs, 3)) cfg.preferred = kPrefIds[pref];
    Help("Si la source préférée n'est pas utilisable, la première disponible est prise.");

    // ---- LaunchBox
    {
        bool ready = app_.Scrape().LaunchBoxReady();
        ImGui::TextColored(ready ? kGreen : kRed, "Base LaunchBox : %s", app_.Scrape().LaunchBoxInfo().c_str());
        if (ImGui::Button("Télécharger / mettre à jour la base LaunchBox (~110 Mo)")) app_.Scrape().UpdateLaunchBox(true);
        Help("Télécharge Metadata.zip depuis gamesdb.launchbox-app.com (base gratuite, sans compte, mise à jour chaque "
             "jour) dans <données>\\cache\\launchbox, puis construit un index local : la recherche fonctionne ensuite "
             "hors ligne. Les images sont téléchargées une par une à l'usage. Les vidéos sont des liens YouTube (voir plus bas).");
        ImGui::SameLine();
        std::error_code ec;
        bool haveZip = fs::exists(cfg.lbDir / L"Metadata.zip", ec);
        ImGui::BeginDisabled(!haveZip);
        if (ImGui::Button("Reconstruire l'index (sans téléchargement)")) app_.Scrape().UpdateLaunchBox(false);
        ImGui::EndDisabled();
        std::string plats;
        for (auto& p : cfg.lbPlatforms) plats += (plats.empty() ? "" : ", ") + p;
        ImGui::SetNextItemWidth(420);
        if (ImGui::InputText("Plateformes LaunchBox gardées", &plats)) {
            cfg.lbPlatforms.clear();
            size_t a = 0;
            while (a <= plats.size()) {
                size_t b = plats.find(',', a);
                if (b == std::string::npos) b = plats.size();
                std::string t = util::Trim(plats.substr(a, b - a));
                if (!t.empty()) cfg.lbPlatforms.push_back(t);
                a = b + 1;
            }
        }
        Help("Noms exacts des plateformes LaunchBox, séparés par des virgules (ex. Arcade, Windows). "
             "Enregistrer puis reconstruire l'index pour appliquer.");

        // ---- Vidéos YouTube (liens LaunchBox) via yt-dlp
        ImGui::Spacing();
        fs::path yt = ytvideo::FindYtDlp(cfg);
        if (!yt.empty())
            ImGui::TextColored(kGreen, "Vidéos : yt-dlp trouvé (%s)", util::PathU8(yt).c_str());
        else
            ImGui::TextColored(kRed, "Vidéos : yt-dlp non installé");
        if (!yt.empty()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Mettre à jour yt-dlp")) app_.Scrape().UpdateYtDlp();
            std::string yi = app_.Scrape().YtDlpInfo();
            if (!yi.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", yi.c_str());
            }
        }
        Help("Vidéos YouTube (lien choisi dans la fiche du jeu, ou proposé par LaunchBox) téléchargées avec yt-dlp, "
             "fourni avec le frontend (sinon : github.com/yt-dlp/yt-dlp, fichier yt-dlp.exe). Si YouTube change et que "
             "les téléchargements échouent, cliquer sur « Mettre à jour yt-dlp ». Seul un extrait est gardé, sans "
             "réencodage. yt-dlp est cherché à côté du programme, dans "
             "<données>\\tools, puis dans le PATH. ffmpeg n'est pas nécessaire.\n\n"
             "Usage personnel uniquement : les conditions d'utilisation de YouTube interdisent le téléchargement, "
             "et les vidéos appartiennent à leurs auteurs.");
        ImGui::SetNextItemWidth(420);
        ImGui::InputText("Chemin de yt-dlp.exe (vide = automatique)", &cfg.ytdlpPath);
        ImGui::Checkbox("Télécharger automatiquement la vidéo proposée par LaunchBox", &cfg.lbAutoVideo);
        Help("Désactivé par défaut : les vidéos liées par LaunchBox sont de qualité très variable (menus, "
             "bandes-annonces, vidéos de 30 min). Sinon, choisir la vidéo à la main dans la fiche du jeu "
             "(onglet Jeux > Médias > Lien YouTube).");
        ImGui::SetNextItemWidth(200);
        ImGui::SliderInt("Durée de l'extrait (s)", &cfg.videoSeconds, 5, 120);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200);
        ImGui::SliderInt("Début de l'extrait (s)", &cfg.videoStartSec, 0, 120);
        Help("Le début saute les génériques des vidéos. Si la vidéo est trop courte, l'extrait est décalé vers le début.");
    }

    ImGui::SetNextItemWidth(420);
    ImGui::InputText("Clé API SteamGridDB", &cfg.sgdbKey, ImGuiInputTextFlags_Password);
    Help("Gratuite : se connecter sur steamgriddb.com, puis Préférences > API.");
    ImGui::SetNextItemWidth(200);
    ImGui::InputText("ScreenScraper : identifiant développeur", &cfg.ssDevId);
    ImGui::SetNextItemWidth(200);
    ImGui::InputText("ScreenScraper : mot de passe développeur", &cfg.ssDevPassword, ImGuiInputTextFlags_Password);
    Help("Identifiants développeur à demander sur le forum de ScreenScraper. API réservée aux logiciels gratuits ; "
         "données sous licence CC BY-NC-SA.");
    ImGui::SetNextItemWidth(200);
    ImGui::InputText("ScreenScraper : systèmes recherchés", &cfg.ssSystems);
    Help("Numéros de systèmes ScreenScraper séparés par des virgules : 269 = TeknoParrot, 75 = Arcade (comme RetroBat). "
         "Vide = tous les systèmes. Un résultat trouvé hors de ces systèmes n'est jamais associé automatiquement.");
    ImGui::SetNextItemWidth(200);
    ImGui::InputText("ScreenScraper : compte utilisateur (facultatif)", &cfg.ssUser);
    ImGui::SetNextItemWidth(200);
    ImGui::InputText("ScreenScraper : mot de passe utilisateur", &cfg.ssPassword, ImGuiInputTextFlags_Password);
    if (ImGui::Button("Enregistrer les réglages des médias")) {
        app_.SaveScraperConfig();
        message_ = "Réglages des médias enregistrés.";
        messageError_ = false;
    }
    ImGui::SameLine();
    ImGui::TextColored(cfg.HasScreenScraper() ? kGreen : kRed, "ScreenScraper %s", cfg.HasScreenScraper() ? "configuré" : "non configuré");
    ImGui::SameLine();
    ImGui::TextColored(cfg.HasSgdb() ? kGreen : kRed, "SteamGridDB %s", cfg.HasSgdb() ? "configuré" : "non configuré");

    std::string status = app_.Scrape().Status();
    ImGui::Text("En cours : %s   (%zu tâche(s) en file)", status.empty() ? "rien" : status.c_str(), app_.Scrape().Pending());
    if (!app_.LastFetchMessage().empty()) ImGui::TextWrapped("Dernier téléchargement : %s", app_.LastFetchMessage().c_str());

    // ---- Jeu sélectionné
    ImGui::SeparatorText("Jeu sélectionné (onglet Jeux)");
    const Game* g = lib.FindGame(selectedGame_);
    if (!g) {
        ImGui::TextDisabled("Sélectionnez un jeu dans l'onglet Jeux.");
    } else {
        if (searchGame_ != g->id) {
            searchGame_ = g->id;
            searchQuery_ = g->name;
        }
        auto state = [&](const char* label, const std::string& path) {
            ImGui::SameLine();
            ImGui::TextColored(path.empty() ? kRed : kGreen, "%s %s", label, path.empty() ? "absent" : "OK");
        };
        ImGui::Text("%s :", g->name.c_str());
        state("logo", lib.LogoFor(*g));
        state(" fond", lib.FanartFor(*g));
        state(" vidéo", lib.VideoFor(*g));
        ImGui::SetNextItemWidth(400);
        ImGui::InputText("Recherche", &searchQuery_);
        ImGui::SameLine();
        ImGui::BeginDisabled(app_.Scrape().CascadeOrder().empty());
        if (ImGui::Button("Toutes les sources")) app_.CompleteGame(g->id);
        ImGui::EndDisabled();
        Help("Interroge les sources dans l'ordre (source préférée, LaunchBox, ScreenScraper, SteamGridDB). "
             "Chacune ne complète que ce qui manque encore, et seulement si un résultat porte exactement le nom du jeu. "
             "Sinon, les résultats sont proposés ci-dessous.");
        ImGui::SameLine();
        ImGui::BeginDisabled(!app_.Scrape().LaunchBoxReady());
        if (ImGui::Button("LaunchBox")) app_.Scrape().Search(g->id, searchQuery_, Provider::LaunchBox);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!cfg.HasScreenScraper());
        if (ImGui::Button("ScreenScraper")) app_.Scrape().Search(g->id, searchQuery_, Provider::ScreenScraper);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!cfg.HasSgdb());
        if (ImGui::Button("SteamGridDB")) app_.Scrape().Search(g->id, searchQuery_, Provider::SteamGridDB);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Checkbox("Remplacer les médias existants", &overwriteMedia_);
        auto& last = app_.LastSearch();
        if (last && last->gameId == g->id) {
            bool consumed = false;
            CandidateTable(*last, consumed);
            if (consumed) last.reset();
        }
        if (ImGui::Button("Supprimer les médias téléchargés de ce jeu")) ImGui::OpenPopup("SupprMedia");
        Help("Supprime le dossier media\\<id> de ce jeu (logo, fond, capture, vidéo téléchargés). "
             "Les fichiers choisis à la main ailleurs sur le disque ne sont pas touchés.");
        if (ImGui::BeginPopupModal("SupprMedia", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            fs::path dir = lib.MediaDirFor(g->id);
            ImGui::Text("Supprimer définitivement le contenu de :\n%s ?", util::PathU8(dir).c_str());
            if (ImGui::Button("Supprimer")) {
                app_.Video()->Stop();
                std::error_code ec;
                for (const auto& e : fs::directory_iterator(dir, ec)) {
                    app_.Textures()->Invalidate(util::PathU8(e.path()));
                    fs::remove(e.path(), ec);
                }
                message_ = "Médias téléchargés supprimés.";
                messageError_ = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Annuler")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    // ---- Tous les jeux
    ImGui::SeparatorText("Tous les jeux");
    std::vector<int64_t> missing = app_.IncompleteGames();
    std::string order;
    for (Provider p : app_.Scrape().CascadeOrder()) order += (order.empty() ? "" : " > ") + std::string(ProviderName(p));
    std::string label = std::format("Compléter les médias des jeux incomplets ({})", missing.size());
    ImGui::BeginDisabled(missing.empty());
    if (ImGui::Button(label.c_str())) app_.StartBatch(missing);
    ImGui::EndDisabled();
    Help("Jeu incomplet : sans logo, sans fond, ou sans vidéo si ScreenScraper est configuré. Toutes les sources "
         "disponibles sont essayées dans l'ordre ; chacune ne complète que ce qui manque. Téléchargement automatique "
         "uniquement si un seul résultat porte exactement le même nom (ou nom alternatif) ; les autres cas sont listés "
         "ci-dessous pour que vous choisissiez.");
    ImGui::SameLine();
    ImGui::TextDisabled("Sources : %s", order.empty() ? "aucune" : order.c_str());
    ImGui::SameLine();
    if (ImGui::Button("Tout annuler")) app_.Scrape().CancelAll();
    if (!app_.BatchStatus().empty()) ImGui::TextWrapped("%s", app_.BatchStatus().c_str());

    auto& pending = app_.ToConfirm();
    if (!pending.empty()) {
        ImGui::Text("À confirmer : %zu jeu(x)", pending.size());
        for (size_t i = 0; i < pending.size(); ++i) {
            const Game* pg = lib.FindGame(pending[i].gameId);
            ImGui::PushID((int)i);
            bool consumed = false;
            if (ImGui::TreeNode(pg ? pg->name.c_str() : "?")) {
                CandidateTable(pending[i], consumed);
                if (ImGui::SmallButton("Ignorer")) consumed = true;
                ImGui::TreePop();
            }
            ImGui::PopID();
            if (consumed) {
                pending.erase(pending.begin() + i);
                break;
            }
        }
    }
}

// ---------------------------------------------------------------- Catégories

void AdminUI::CategoryTree(int64_t parent) {
    for (int64_t id : app_.Lib().ChildCategories(parent, true)) {
        if (id <= 0) continue;
        const Category* c = app_.Lib().FindCategory(id);
        bool leaf = true;
        for (const Category& o : app_.Lib().Categories())
            if (o.parentId == id) leaf = false;
        ImGuiTreeNodeFlags fl = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (leaf) fl |= ImGuiTreeNodeFlags_Leaf;
        if (id == selectedCat_) fl |= ImGuiTreeNodeFlags_Selected;
        std::string label = std::format("{}  ({})##{}", c->name, app_.Lib().CountGames(id), id);
        bool open = ImGui::TreeNodeEx(label.c_str(), fl);
        if (ImGui::IsItemClicked() || (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown))) {
            selectedCat_ = id;
            catName_ = c->name;
        }
        if (open) {
            CategoryTree(id);
            ImGui::TreePop();
        }
    }
}

void AdminUI::TabCategories() {
    Library& lib = app_.Lib();
    ImGui::BeginChild("tree", ImVec2(ImGui::GetContentRegionAvail().x * 0.4f, 0), ImGuiChildFlags_Borders);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
    ImGui::InputTextWithHint("##newroot", "Nom de la catégorie", &newCatName_);
    ImGui::SameLine();
    if (ImGui::Button("+ À la racine") && !util::Trim(newCatName_).empty()) {
        selectedCat_ = lib.AddCategory(util::Trim(newCatName_), Library::kRoot);
        catName_ = util::Trim(newCatName_);
        newCatName_.clear();
        app_.RefreshItems();
    }
    ImGui::TextDisabled("Automatiques : Tous les jeux, Favoris, Récemment joués");
    ImGui::Separator();
    CategoryTree(Library::kRoot);
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("catedit", ImVec2(0, 0), ImGuiChildFlags_Borders);
    const Category* c = lib.FindCategory(selectedCat_);
    if (!c) {
        ImGui::TextDisabled("Sélectionnez une catégorie.");
        ImGui::EndChild();
        return;
    }
    Category cur = *c;  // copie : la liste peut changer pendant les actions ci-dessous
    ImGui::Text("Catégorie : %s", cur.name.c_str());
    ImGui::Separator();
    ImGui::SetNextItemWidth(300);
    ImGui::InputText("##rename", &catName_);
    ImGui::SameLine();
    if (ImGui::Button("Renommer") && !util::Trim(catName_).empty()) {
        lib.RenameCategory(cur.id, util::Trim(catName_));
        app_.RefreshItems();
    }

    ImGui::SetNextItemWidth(300);
    static std::string subName;
    ImGui::InputTextWithHint("##sub", "Nom de la sous-catégorie", &subName);
    ImGui::SameLine();
    if (ImGui::Button("+ Sous-catégorie") && !util::Trim(subName).empty()) {
        lib.AddCategory(util::Trim(subName), cur.id);
        subName.clear();
        app_.RefreshItems();
    }

    // Déplacement : racine ou toute catégorie qui n'est ni elle-même ni une descendante
    std::string parentName = cur.parentId ? lib.NodeName(cur.parentId) : "(racine)";
    ImGui::SetNextItemWidth(300);
    if (ImGui::BeginCombo("Déplacer sous", parentName.c_str())) {
        if (ImGui::Selectable("(racine)", cur.parentId == 0)) {
            lib.MoveCategory(cur.id, Library::kRoot);
            app_.RefreshItems();
        }
        for (const Category& o : lib.Categories()) {
            if (o.id == cur.id || lib.IsDescendant(o.id, cur.id)) continue;
            std::string l = lib.NodePath(o.id) + "##mv" + std::to_string(o.id);
            if (ImGui::Selectable(l.c_str(), o.id == cur.parentId)) {
                lib.MoveCategory(cur.id, o.id);
                app_.RefreshItems();
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::Button("Monter")) { lib.ShiftCategory(cur.id, -1); app_.RefreshItems(); }
    ImGui::SameLine();
    if (ImGui::Button("Descendre")) { lib.ShiftCategory(cur.id, +1); app_.RefreshItems(); }
    ImGui::Separator();
    if (ImGui::Button("Supprimer la catégorie")) ImGui::OpenPopup("SupprCat");
    if (ImGui::BeginPopupModal("SupprCat", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Supprimer « %s » et ses sous-catégories ?", cur.name.c_str());
        ImGui::TextDisabled("Les jeux restent dans la bibliothèque (seule leur affectation est retirée).");
        if (ImGui::Button("Supprimer")) {
            lib.DeleteCategory(cur.id);
            selectedCat_ = 0;
            app_.RefreshItems();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Annuler")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------- Affichage

void AdminUI::TabDisplay() {
    Theme& t = app_.CurrentTheme();
    WheelTheme& w = t.wheel;
    Profile& prof = app_.CurrentProfile();

    ImGui::SeparatorText("Thème du profil");
    ImGui::Text("Profil actif : %s", app_.ProfileName().c_str());
    std::vector<std::string> themes = Theme::List(app_.Dirs().ThemesDir());
    ImGui::SetNextItemWidth(300);
    if (ImGui::BeginCombo("Thème", prof.theme.c_str())) {
        for (const std::string& name : themes)
            if (ImGui::Selectable(name.c_str(), name == prof.theme)) {
                prof.theme = name;
                t.Load(app_.Dirs().ThemesDir(), name);
                app_.SaveProfile();
                message_ = "Thème « " + name + " » appliqué au profil " + app_.ProfileName() + ".";
                messageError_ = false;
            }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint("##dup", "Nom du nouveau thème", &newThemeName_);
    ImGui::SameLine();
    if (ImGui::Button("Dupliquer le thème") && !util::Trim(newThemeName_).empty()) {
        std::string nn = util::Trim(newThemeName_);
        fs::path dst = app_.Dirs().ThemesDir() / util::U8Path(nn);
        std::error_code ec;
        if (fs::exists(dst, ec)) {
            message_ = "Un thème porte déjà ce nom.";
            messageError_ = true;
        } else {
            fs::copy(t.dir, dst, fs::copy_options::recursive, ec);
            if (ec) {
                message_ = "Copie impossible : " + ec.message();
                messageError_ = true;
            } else {
                prof.theme = nn;
                t.Load(app_.Dirs().ThemesDir(), nn);
                app_.SaveProfile();
                message_ = "Thème « " + nn + " » créé et appliqué : les modifications ci-dessous le concernent.";
                messageError_ = false;
                newThemeName_.clear();
            }
        }
    }
    Help("Un thème est un dossier dans themes\\ contenant theme.json (et éventuellement images et polices). "
         "Il se modifie ici ou directement dans le fichier, sans toucher au programme.");

    if (ImGui::CollapsingHeader("Couleurs, polices et image de fond")) {
        auto color = [](const char* label, Color& c) {
            float f[4] = {c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f};
            if (ImGui::ColorEdit4(label, f, ImGuiColorEditFlags_AlphaBar))
                c = {(uint8_t)(f[0] * 255), (uint8_t)(f[1] * 255), (uint8_t)(f[2] * 255), (uint8_t)(f[3] * 255)};
        };
        color("Fond (haut)", t.backgroundTop);
        color("Fond (bas)", t.backgroundBottom);
        color("Panneaux", t.panel);
        color("Accent", t.accent);
        color("Texte", t.text);
        color("Texte secondaire", t.textDim);
        color("Bandeau de la roue", w.bandColor);
        ImGui::SetNextItemWidth(400);
        ImGui::InputText("Police des titres", &t.fontTitle);
        ImGui::SetNextItemWidth(400);
        ImGui::InputText("Police du texte", &t.fontBody);
        ImGui::SetNextItemWidth(300); ImGui::SliderFloat("Taille des titres", &t.titleSize, 0.02f, 0.1f);
        ImGui::SetNextItemWidth(300); ImGui::SliderFloat("Taille du texte", &t.bodySize, 0.012f, 0.05f);
        ImGui::SetNextItemWidth(400);
        ImGui::InputText("Image de fond", &t.backgroundImage);
        ImGui::SameLine();
        if (ImGui::Button("Parcourir...##bg")) BrowseFile(Field::ThemeBackground, "Images", "png;jpg;jpeg");
        Help("Chemin absolu, ou relatif au dossier du thème (ex. fond.jpg placé dans le dossier du thème).");
    }
    if (ImGui::CollapsingHeader("Disposition et informations affichées")) {
        auto rect = [](const char* label, RectF& r) {
            float v[4] = {r.x, r.y, r.w, r.h};
            ImGui::SetNextItemWidth(400);
            if (ImGui::SliderFloat4(label, v, 0.0f, 1.0f, "%.2f")) r = {v[0], v[1], v[2], v[3]};
        };
        rect("Zone média (x, y, l, h)", t.mediaRect);
        rect("Zone infos (x, y, l, h)", t.infoRect);
        rect("En-tête (x, y, l, h)", t.headerRect);
        ImGui::Checkbox("En-tête", &t.showHeader);
        ImGui::SameLine();
        ImGui::Checkbox("Année / éditeur / genre", &t.showMeta);
        ImGui::SameLine();
        ImGui::Checkbox("Description", &t.showDescription);
        ImGui::SameLine();
        ImGui::Checkbox("Statistiques", &t.showStats);
    }
    ImGui::SeparatorText("Roue");
    int mode = w.mode == "strip" ? 1 : 0;
    const char* modes[] = {"Roue (arc, rotation, échelle)", "Bande horizontale simple"};
    ImGui::SetNextItemWidth(400);
    if (ImGui::Combo("Mode", &mode, modes, 2)) w.mode = mode ? "strip" : "wheel";
    ImGui::SetNextItemWidth(400); ImGui::SliderInt("Logos visibles", &w.visible, 3, 15);
    ImGui::SetNextItemWidth(400); ImGui::SliderFloat("Position verticale", &w.y, 0.1f, 0.95f);
    ImGui::SetNextItemWidth(400); ImGui::SliderFloat("Largeur d'un logo", &w.itemW, 0.05f, 0.4f);
    ImGui::SetNextItemWidth(400); ImGui::SliderFloat("Hauteur d'un logo", &w.itemH, 0.05f, 0.4f);
    ImGui::SetNextItemWidth(400); ImGui::SliderFloat("Espacement", &w.spacing, 0.05f, 0.4f);
    ImGui::SetNextItemWidth(400); ImGui::SliderFloat("Échelle sélection", &w.selectedScale, 1.0f, 2.5f);
    ImGui::SetNextItemWidth(400); ImGui::SliderFloat("Échelle voisins", &w.sideScale, 0.3f, 1.5f);
    ImGui::SetNextItemWidth(400); ImGui::SliderFloat("Réduction par cran", &w.falloff, 0.6f, 1.0f);
    ImGui::SetNextItemWidth(400); ImGui::SliderFloat("Courbure", &w.curvature, 0.0f, 0.1f);
    ImGui::SetNextItemWidth(400); ImGui::SliderFloat("Inclinaison (°)", &w.tilt, 0.0f, 20.0f);
    ImGui::SetNextItemWidth(400); ImGui::SliderFloat("Opacité voisins", &w.sideAlpha, 0.1f, 1.0f);
    ImGui::SetNextItemWidth(400); ImGui::SliderFloat("Vitesse d'animation", &w.animSpeed, 4.0f, 40.0f);
    ImGui::SetNextItemWidth(400); ImGui::SliderFloat("Hauteur du bandeau", &w.bandHeight, 0.0f, 0.5f);
    ImGui::SeparatorText("Écran");
    ImGui::SetNextItemWidth(400); ImGui::SliderFloat("Opacité du fond (fanart)", &t.fanartBackgroundAlpha, 0.0f, 1.0f);
    if (ImGui::Button("Enregistrer le thème")) {
        bool ok = t.Save();
        message_ = ok ? "Thème enregistré." : "Impossible d'enregistrer le thème.";
        messageError_ = !ok;
    }
    ImGui::SameLine();
    if (ImGui::Button("Recharger depuis le fichier")) t.Load(app_.Dirs().ThemesDir(), t.name);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", util::PathU8(t.dir / L"theme.json").c_str());

    Settings& s = app_.Config();
    VideoSettings& vs = prof.video;
    ImGui::SeparatorText("Vidéo (réglage du profil)");
    ImGui::Checkbox("Lecture des vidéos", &vs.enabled);
    ImGui::SameLine();
    ImGui::Checkbox("En boucle", &vs.loop);
    ImGui::SameLine();
    bool vol = ImGui::Checkbox("Muet", &vs.mute);
    ImGui::SameLine();
    ImGui::Checkbox("Décodage matériel (D3D11VA)", &vs.hwDecode);
    ImGui::Checkbox("Retirer les bandes noires des vidéos", &vs.autoCrop);
    Help("Certaines vidéos ont des bandes noires dans l'image elle-même. Elles sont détectées automatiquement "
         "(5 images analysées une fois par vidéo, en tâche de fond) et masquées à l'affichage. Le fichier n'est pas modifié.");
    ImGui::Checkbox("Fond flou autour de la vidéo", &vs.blurFill);
    Help("Quand la vidéo n'a pas la forme du cadre, les bandes sont remplies par la vidéo elle-même, agrandie, floue "
         "et assombrie (aucune déformation, rien n'est coupé). Décoché : bandes noires.");
    ImGui::SetNextItemWidth(400);
    vol |= ImGui::SliderFloat("Volume", &vs.volume, 0.0f, 1.0f, "%.2f");
    if (vol) app_.Video()->SetVolume(vs.volume, vs.mute);
    ImGui::SetNextItemWidth(400);
    ImGui::SliderInt("Délai avant lecture (ms)", &vs.delayMs, 0, 2000);
    Help("La vidéo ne démarre que lorsque la sélection reste immobile pendant ce délai : rien n'est décodé pendant le défilement.");
    if (ImGui::Button("Enregistrer les réglages vidéo du profil")) {
        app_.SaveProfile();
        message_ = "Réglages vidéo enregistrés dans le profil " + app_.ProfileName() + ".";
        messageError_ = false;
    }
    if (!app_.Video()->DecoderInfo().empty()) ImGui::TextDisabled("Dernier décodeur : %s", app_.Video()->DecoderInfo().c_str());
    if (!app_.Video()->LastError().empty()) ImGui::TextColored(kRed, "Dernière erreur : %s", app_.Video()->LastError().c_str());

    ImGui::SeparatorText("Fenêtre");
    bool apply = false;
    apply |= ImGui::Checkbox("Plein écran", &s.fullscreen);
    apply |= ImGui::Checkbox("Synchronisation verticale (VSync)", &s.vsync);
    ImGui::Checkbox("Masquer le curseur hors administration", &s.hideCursor);
    ImGui::Checkbox("Réduire le frontend pendant le jeu", &s.minimizeDuringGame);
    ImGui::SetNextItemWidth(200);
    ImGui::SliderInt("Écran de chargement : durée maximale (s)", &s.launchScreenMaxSec, 5, 300);
    Help("Au lancement d'un jeu, l'écran de chargement reste au premier plan jusqu'à l'apparition de la vraie "
         "fenêtre du jeu (la fenêtre cmd des .bat est masquée). Si aucune fenêtre n'est repérée, il s'efface "
         "après cette durée. Retour pendant le chargement l'efface tout de suite.");
    if (apply) app_.ApplyWindowSettings();
    if (ImGui::Button("Enregistrer les paramètres")) {
        app_.SaveSettings();
        message_ = "Paramètres enregistrés.";
        messageError_ = false;
    }
}

// ---------------------------------------------------------------- Commandes

void AdminUI::TabInput() {
    Input& in = app_.Inputs();
    InputTiming& t = in.Timing();
    ImGui::SeparatorText("Périphériques détectés");
    auto devs = in.DeviceNames();
    if (devs.empty()) ImGui::TextDisabled("Aucun joystick ni manette détecté (le clavier est toujours actif).");
    for (auto& d : devs) ImGui::BulletText("%s", d.c_str());
    ImGui::Text("Dernière entrée détectée : %s", in.LastRawInput().empty() ? "-" : in.LastRawInput().c_str());
    Help("Appuyez sur un bouton du volant ou de la borne pour voir comment Windows le présente (utile pour vJoy / BackForceFeeder).");

    ImGui::SeparatorText("Réglages");
    ImGui::SetNextItemWidth(300); ImGui::SliderInt("Délai avant répétition (ms)", &t.repeatDelayMs, 100, 1000);
    ImGui::SetNextItemWidth(300); ImGui::SliderInt("Intervalle de répétition (ms)", &t.repeatRateMs, 20, 300);
    ImGui::SetNextItemWidth(300); ImGui::SliderInt("Anti-rebond (ms)", &t.debounceMs, 0, 200);
    ImGui::SetNextItemWidth(300); ImGui::SliderInt("Anti double validation (ms)", &t.selectGuardMs, 0, 2000);
    ImGui::SetNextItemWidth(300); ImGui::SliderFloat("Seuil des axes", &t.axisThreshold, 0.2f, 0.95f);
    if (ImGui::Button("Enregistrer les réglages")) {
        app_.SaveSettings();
        message_ = "Réglages des commandes enregistrés.";
        messageError_ = false;
    }

    ImGui::SetNextItemWidth(300); ImGui::SliderInt("Appui long (ms)", &t.holdMs, 400, 3000);
    Help("Maintenir Retour pendant cette durée ouvre le menu de la borne.");
    ImGui::SetNextItemWidth(300); ImGui::SliderInt("Accélération après (ms, 0 = jamais)", &t.accelAfterMs, 0, 5000);
    ImGui::SetNextItemWidth(300); ImGui::SliderInt("Garde au retour d'un jeu (ms)", &t.returnGuardMs, 0, 3000);
    Help("Les commandes sont ignorées pendant ce délai après la fermeture d'un jeu (évite de relancer par erreur).");

    ImGui::SeparatorText("Associations");
    if (ImGui::Button("Assistant au volant (plein écran)")) app_.StartWizard();
    Help("Assigne toutes les actions l'une après l'autre en appuyant sur les commandes de la borne. Échap au clavier pour annuler.");
    ImGui::SameLine();
    ImGui::Checkbox("Lier au périphérique détecté", &captureLinkDevice_);
    Help("Coché : l'entrée ne réagit que sur ce périphérique (ex. vJoy Device). Décoché : sur n'importe quel joystick.");
    ImGui::TextDisabled("Fichier : %s", util::PathU8(app_.CurrentInputFile()).c_str());

    // Assignation en cours
    if (captureAction_ >= 0) {
        if (auto b = in.TakeCaptured()) {
            in.EndCapture();
            if (b->type == Binding::Type::Key && b->code == SDLK_ESCAPE) {
                message_ = "Assignation annulée.";
                messageError_ = false;
            } else {
                b->action = (Action)captureAction_;
                if (!captureLinkDevice_) b->device.clear();
                // L'entrée est retirée des autres actions avant d'être ajoutée à celle-ci.
                std::vector<Binding> list;
                for (const Binding& o : in.Bindings()) {
                    bool overlaps = o.type == b->type && o.code == b->code && o.dir == b->dir &&
                                    (o.device.empty() || b->device.empty() || o.device == b->device);
                    if (!overlaps) list.push_back(o);
                }
                list.push_back(*b);
                in.SetBindings(std::move(list));
                in.Save(app_.CurrentInputFile());
                message_ = std::string("Assigné à « ") + ActionLabel(b->action) + " » : " + b->Describe();
                messageError_ = false;
            }
            captureAction_ = -1;
        }
    }

    bool removed = false;
    if (ImGui::BeginTable("bind", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 14))) {
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 260);
        ImGui::TableSetupColumn("Entrées");
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 230);
        ImGui::TableHeadersRow();
        for (int a = 0; a < (int)Action::Count && !removed; ++a) {
            ImGui::PushID(a);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(ActionLabel((Action)a));
            ImGui::TableNextColumn();
            const auto& list = in.Bindings();
            for (size_t i = 0; i < list.size() && !removed; ++i) {
                if ((int)list[i].action != a) continue;
                ImGui::PushID((int)i);
                if (ImGui::SmallButton("x")) {
                    in.RemoveBinding(i);
                    in.Save(app_.CurrentInputFile());
                    removed = true;
                }
                ImGui::SameLine();
                if (!removed) ImGui::TextUnformatted(list[i].Describe().c_str());
                ImGui::PopID();
            }
            ImGui::TableNextColumn();
            if (captureAction_ == a) {
                ImGui::TextColored(kGreen, "Appuyez... (Échap : annuler)");
            } else if (ImGui::Button("Ajouter par détection") && captureAction_ < 0) {
                captureAction_ = a;
                in.BeginCapture();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (ImGui::Button("Réinitialiser le mapping")) ImGui::OpenPopup("ResetMap");
    if (ImGui::BeginPopupModal("ResetMap", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Revenir aux associations par défaut ?");
        if (ImGui::Button("Oui")) {
            in.ResetDefaults();
            in.Save(app_.CurrentInputFile());
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Non")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------- Diagnostic

void AdminUI::TabDiagnostic() {
    // Mesures réelles du processus
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));
    FILETIME c, e, k, u, now;
    GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
    GetSystemTimeAsFileTime(&now);
    auto ft = [](FILETIME f) { return ((uint64_t)f.dwHighDateTime << 32) | f.dwLowDateTime; };
    uint64_t wall = ft(now), proc = ft(k) + ft(u);
    if (cpuLastWall_ && wall - cpuLastWall_ > 5'000'000) {  // 0,5 s
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        cpuPercent_ = 100.0f * (float)(proc - cpuLastProc_) / (float)(wall - cpuLastWall_) / (float)si.dwNumberOfProcessors;
        cpuLastWall_ = wall;
        cpuLastProc_ = proc;
    } else if (!cpuLastWall_) {
        cpuLastWall_ = wall;
        cpuLastProc_ = proc;
    }

    ImGui::SeparatorText("Performances (mesurées)");
    float ms = app_.FrameMsAvg();
    ImGui::Text("Images par seconde : %.0f  (%.2f ms par image)", ms > 0 ? 1000.0f / ms : 0.0f, ms);
    ImGui::Text("Temps de rendu max (dernière seconde) : %.2f ms", app_.FrameMsMax());
    ImGui::Text("CPU du frontend : %.1f %% (tous cœurs)", cpuPercent_);
    ImGui::Text("Mémoire : %.1f Mo (working set), %.1f Mo privés", pmc.WorkingSetSize / 1048576.0, pmc.PrivateUsage / 1048576.0);
    ImGui::TextDisabled("Remarque : ces valeurs incluent l'interface d'administration ouverte.");
    if (TextureCache* tc = app_.Textures())
        ImGui::Text("Textures en cache : %zu (%.1f Mo / %d Mo), en attente : %zu", tc->Count(), tc->BytesUsed() / 1048576.0,
                    app_.Config().textureCacheMB, tc->Pending());
    if (TextRenderer* tr = app_.Texts()) ImGui::Text("Textes en cache : %zu", tr->CacheSize());

    ImGui::SeparatorText("Bibliothèque");
    ImGui::Text("%zu jeux, %zu catégories", app_.Lib().Games().size(), app_.Lib().Categories().size());
    int missing = 0;
    for (const Game& g : app_.Lib().Games())
        if (!util::FileExists(g.launchPath)) ++missing;
    ImGui::TextColored(missing ? kRed : kGreen, "Fichiers de lancement introuvables : %d", missing);

    ImGui::SeparatorText("Fichiers");
    ImGui::Text("Journal : %s", logx::FilePath().c_str());
    if (ImGui::Button("Ouvrir le dossier de données")) OpenInExplorer(util::PathU8(app_.Dirs().dataDir));
    ImGui::SameLine();
    if (ImGui::Button("Ouvrir le journal")) OpenInExplorer(logx::FilePath());
    ImGui::Separator();
    if (ImGui::Button("Quitter le frontend")) app_.RequestQuit();
}
