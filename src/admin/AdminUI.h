#pragma once
#include <SDL3/SDL.h>
#include <imgui.h>

#include <mutex>
#include <string>

#include "library/Model.h"

class App;

// Menu d'administration (Dear ImGui) : souris/clavier, également navigable à la manette.
class AdminUI {
public:
    explicit AdminUI(App& app) : app_(app) {}
    bool Init(SDL_Window* w, SDL_Renderer* r);
    void Shutdown();

    bool IsOpen() const { return open_; }
    void Open();
    void Close();
    void Toggle() { open_ ? Close() : Open(); }
    void ProcessEvent(const SDL_Event& e);
    void Render();

    // Résultat d'une boîte de dialogue native (appelé depuis n'importe quel thread)
    enum class Field { None, Launch, WorkDir, Logo, Fanart, Video, Import, ThemeBackground, ShareExport, ShareImport };
    void OnDialogResult(Field f, const std::string& path);

    // Ouvre l'administration sur un onglet donné (tests automatisés).
    void OpenTab(const std::string& name) {
        Open();
        forceTab_ = name;
    }

private:
    void TabGames();
    void TabCategories();
    void TabDisplay();
    void TabInput();
    void TabDiagnostic();
    void TabMedia();
    void TabProfiles();
    void TabStats();
    void TabSystem();
    void CategoryVisibility(int64_t parent, struct Profile& p, int depth);
    void AttractCategories(int64_t parent, struct Profile& p, int depth);
    void CandidateTable(const struct SearchResult& r, bool& consumed);
    void GameEditor();
    void CategoryChecklist(int64_t parent);
    void CategoryTree(int64_t parent);
    void BrowseFile(Field f, const char* filterName, const char* pattern);
    void BrowseFolder(Field f);
    void SelectGame(int64_t id);
    void NewGame();
    bool SaveGame(bool allowDuplicate);
    void ApplyPendingDialog();

    App& app_;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    bool ready_ = false;
    bool open_ = false;

    // Éditeur de jeu
    std::string filter_;
    int64_t selectedGame_ = 0;
    bool editing_ = false;
    bool isNew_ = false;
    Game edit_;
    std::string message_;
    bool messageError_ = false;

    // Catégories
    int64_t selectedCat_ = 0;
    std::string catName_;
    std::string newCatName_;

    // Dialogue natif en attente
    std::mutex dlgMutex_;
    Field dlgField_ = Field::None;
    std::string dlgPath_;

    std::string newThemeName_, newProfileName_;

    // Médias
    std::string searchQuery_;
    std::string ytUrl_, ytStart_, ytDuration_;  // vidéo YouTube choisie à la main (fiche du jeu)
    // Partage de la configuration (export / import)
    bool shareImages_ = true, shareVideos_ = false, shareProfiles_ = true;
    std::string importFile_, importOldRoot_, importNewRoot_, importError_;
    bool importProfiles_ = true, importMedia_ = true, importVideos_ = true, importChecked_ = false;
    int importGames_ = 0, importNew_ = 0, importExisting_ = 0, importLinks_ = 0, importProfilesN_ = 0, importMediaN_ = 0;
    std::vector<std::string> importMissing_;
    std::string importExported_;
    void SaveFile(Field f, const char* filterName, const char* pattern, const std::string& defaultName);
    void RefreshImportPreview();
    void SectionShare();
    bool ytToEnd_ = false;
    int64_t searchGame_ = 0;
    bool overwriteMedia_ = false;

    // Assignation par détection (-1 = inactive)
    int captureAction_ = -1;
    bool captureLinkDevice_ = true;

    // Mesures CPU
    uint64_t cpuLastWall_ = 0, cpuLastProc_ = 0;
    float cpuPercent_ = 0;
    std::string forceTab_;
    int TabFlag(const char* name) const { return forceTab_ == name ? ImGuiTabItemFlags_SetSelected : 0; }
    bool backupWithMedia_ = false;
    std::string pendingRestore_;  // sauvegarde choisie pour une restauration (confirmation)
};
