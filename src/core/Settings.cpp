#include "core/Settings.h"

#include <windows.h>

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>

#include "core/Log.h"

using nlohmann::json;

namespace {

bool ReadJson(const fs::path& file, json& out) {
    std::ifstream f(file, std::ios::binary);
    if (!f) return false;
    try {
        out = json::parse(f, nullptr, true, true);
        return true;
    } catch (const std::exception& e) {
        logx::Error("JSON invalide dans {} : {}", util::PathU8(file), e.what());
        return false;
    }
}

// Écriture atomique : fichier temporaire puis remplacement, pour ne jamais laisser un fichier tronqué.
bool WriteJson(const fs::path& file, const json& j) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    fs::path tmp = file;
    tmp += L".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << j.dump(2);
        if (!f) return false;
    }
    if (!MoveFileExW(tmp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        logx::Error("Impossible d'écrire {}", util::PathU8(file));
        return false;
    }
    return true;
}

template <class T> void Get(const json& j, const char* key, T& v) {
    if (j.contains(key)) {
        try {
            v = j.at(key).get<T>();
        } catch (...) {
        }
    }
}

}  // namespace

bool Settings::Load(const fs::path& file) {
    json j;
    if (!ReadJson(file, j)) return false;
    Get(j, "theme", theme);
    Get(j, "fullscreen", fullscreen);
    Get(j, "windowW", windowW);
    Get(j, "windowH", windowH);
    Get(j, "vsync", vsync);
    Get(j, "hideCursor", hideCursor);
    Get(j, "minimizeDuringGame", minimizeDuringGame);
    Get(j, "launchScreenMaxSec", launchScreenMaxSec);
    launchScreenMaxSec = std::clamp(launchScreenMaxSec, 5, 300);
    Get(j, "textureCacheMB", textureCacheMB);
    Get(j, "maxImageSide", maxImageSide);
    Get(j, "profile", profile);
    Get(j, "askProfileAtStartup", askProfileAtStartup);
    Get(j, "cabMode", cabMode);
    if (cabMode != "guncab") cabMode = "racecab";
    Get(j, "kiosk", kiosk);
    Get(j, "autoBackup", autoBackup);
    Get(j, "backupKeep", backupKeep);
    if (j.contains("gun")) {
        const json& g = j["gun"];
        Get(g, "crosshair", gun.crosshair);
        Get(g, "crosshairColor", gun.crosshairColor);
        Get(g, "crosshairSize", gun.crosshairSize);
        gun.crosshairSize = std::clamp(gun.crosshairSize, 60, 200);
        Get(g, "borderPercent", gun.borderPercent);
        Get(g, "launchGuardMs", gun.launchGuardMs);
        Get(g, "arcadeLayout", gun.arcadeLayout);
        Get(g, "view", gun.view);
        Get(g, "mosaicCols", gun.mosaicCols);
        Get(g, "mosaicRows", gun.mosaicRows);
        gun.mosaicCols = std::clamp(gun.mosaicCols, 2, 6);
        gun.mosaicRows = std::clamp(gun.mosaicRows, 1, 4);
        if (g.contains("borderColor") && g["borderColor"].is_array() && g["borderColor"].size() >= 3)
            for (int i = 0; i < 3; ++i) gun.borderColor[i] = g["borderColor"][i].get<int>();
    }
    if (j.contains("input")) {
        const json& i = j["input"];
        Get(i, "repeatDelayMs", input.repeatDelayMs);
        Get(i, "repeatRateMs", input.repeatRateMs);
        Get(i, "debounceMs", input.debounceMs);
        Get(i, "selectGuardMs", input.selectGuardMs);
        Get(i, "axisThreshold", input.axisThreshold);
        Get(i, "holdMs", input.holdMs);
        Get(i, "accelAfterMs", input.accelAfterMs);
        Get(i, "returnGuardMs", input.returnGuardMs);
    }
    return true;
}

bool Settings::Save(const fs::path& file) const {
    json j = {{"theme", theme},
              {"fullscreen", fullscreen},
              {"windowW", windowW},
              {"windowH", windowH},
              {"vsync", vsync},
              {"hideCursor", hideCursor},
              {"minimizeDuringGame", minimizeDuringGame},
              {"launchScreenMaxSec", launchScreenMaxSec},
              {"textureCacheMB", textureCacheMB},
              {"maxImageSide", maxImageSide},
              {"profile", profile},
              {"askProfileAtStartup", askProfileAtStartup},
              {"cabMode", cabMode},
              {"kiosk", kiosk},
              {"autoBackup", autoBackup},
              {"backupKeep", backupKeep},
              {"gun",
               {{"crosshair", gun.crosshair},
                {"crosshairColor", gun.crosshairColor},
                {"crosshairSize", gun.crosshairSize},
                {"borderPercent", gun.borderPercent},
                {"borderColor", {gun.borderColor[0], gun.borderColor[1], gun.borderColor[2]}},
                {"launchGuardMs", gun.launchGuardMs},
                {"arcadeLayout", gun.arcadeLayout},
                {"view", gun.view},
                {"mosaicCols", gun.mosaicCols},
                {"mosaicRows", gun.mosaicRows}}},
              {"input",
               {{"repeatDelayMs", input.repeatDelayMs},
                {"repeatRateMs", input.repeatRateMs},
                {"debounceMs", input.debounceMs},
                {"selectGuardMs", input.selectGuardMs},
                {"axisThreshold", input.axisThreshold},
                {"holdMs", input.holdMs},
                {"accelAfterMs", input.accelAfterMs},
                {"returnGuardMs", input.returnGuardMs}}}};
    return WriteJson(file, j);
}

bool NavState::Load(const fs::path& file) {
    json j;
    if (!ReadJson(file, j)) return false;
    Get(j, "node", node);
    Get(j, "itemKind", itemKind);
    Get(j, "itemId", itemId);
    return true;
}

bool NavState::Save(const fs::path& file) const {
    return WriteJson(file, json{{"node", node}, {"itemKind", itemKind}, {"itemId", itemId}});
}

Paths Paths::Resolve(int argc, char** argv) {
    Paths p;
    wchar_t buf[MAX_PATH * 4];
    GetModuleFileNameW(nullptr, buf, (DWORD)std::size(buf));
    p.exeDir = fs::path(buf).parent_path();

    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--data") p.dataDir = util::U8Path(argv[i + 1]);
    }
    if (p.dataDir.empty()) {
        std::ifstream f(p.exeDir / L"datadir.txt", std::ios::binary);
        std::string line;
        if (f && std::getline(f, line)) {
            if (line.size() >= 3 && (unsigned char)line[0] == 0xEF) line = line.substr(3);  // BOM
            line = util::Trim(line);
            if (!line.empty()) p.dataDir = util::U8Path(line);
        }
    }
    if (p.dataDir.empty()) p.dataDir = L"H:\\RacecabData";
    return p;
}

void Paths::EnsureDirs() const {
    std::error_code ec;
    for (const fs::path& d : {ConfigDir(), ProfilesDir(), ConfigDir() / L"input", DbFile().parent_path(), ThemesDir(),
                              MediaDir(), LogsDir()})
        fs::create_directories(d, ec);
}
