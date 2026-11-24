#include "profiles/Profile.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>

#include "core/Log.h"

using nlohmann::json;

namespace {
template <class T> void Get(const json& j, const char* k, T& v) {
    if (!j.is_object() || !j.contains(k)) return;
    try {
        v = j.at(k).get<T>();
    } catch (...) {
        logx::Warn("Profil : valeur invalide pour '{}'", k);
    }
}
}  // namespace

bool Profile::Load(const fs::path& file) {
    std::ifstream f(file, std::ios::binary);
    if (!f) return false;
    json j;
    try {
        j = json::parse(f, nullptr, true, true);
    } catch (const std::exception& e) {
        logx::Error("Profil illisible {} : {}", util::PathU8(file), e.what());
        return false;
    }
    *this = Profile{};
    name = util::PathU8(file.stem());
    Get(j, "theme", theme);
    if (j.contains("mode")) Get(j, "mode", mode);
    else mode = name == "GunCab" ? "guncab" : name == "Racecab" ? "racecab" : "";  // profils d'avant les modes
    Get(j, "sort", sort);
    Get(j, "hiddenCategories", hiddenCategories);
    Get(j, "filterGames", filterGames);
    Get(j, "showAutoCategories", showAutoCategories);
    if (j.contains("video")) {
        const json& v = j["video"];
        Get(v, "enabled", video.enabled);
        Get(v, "loop", video.loop);
        Get(v, "volume", video.volume);
        Get(v, "mute", video.mute);
        Get(v, "delayMs", video.delayMs);
        Get(v, "hwDecode", video.hwDecode);
        Get(v, "blurFill", video.blurFill);
        Get(v, "autoCrop", video.autoCrop);
    }
    if (j.contains("attract")) {
        const json& a = j["attract"];
        Get(a, "enabled", attract.enabled);
        Get(a, "idleSeconds", attract.idleSeconds);
        Get(a, "displaySeconds", attract.displaySeconds);
        Get(a, "random", attract.random);
        Get(a, "categories", attract.categories);
        Get(a, "video", attract.video);
        Get(a, "mute", attract.mute);
        Get(a, "speed", attract.speed);
    }
    // Profils d'avant la courbe de volume perceptive (0.6.2) : volumes enregistrés comme gains linéaires.
    // Conversion unique pour garder le même niveau sonore : curseur = racine cubique de l'ancien gain.
    if (j.value("volumeCurve", 1) < 2) {
        if (j.contains("video") && j["video"].contains("volume"))
            video.volume = std::cbrt(std::clamp(video.volume, 0.0f, 1.0f));
    }
    attract.idleSeconds = std::max(5, attract.idleSeconds);
    attract.displaySeconds = std::max(2, attract.displaySeconds);
    return true;
}

bool Profile::Save(const fs::path& file) const {
    json j = {{"theme", theme},
              {"volumeCurve", 2},
              {"mode", mode},
              {"sort", sort},
              {"hiddenCategories", hiddenCategories},
              {"filterGames", filterGames},
              {"showAutoCategories", showAutoCategories},
              {"video",
               {{"enabled", video.enabled},
                {"loop", video.loop},
                {"volume", video.volume},
                {"mute", video.mute},
                {"delayMs", video.delayMs},
                {"hwDecode", video.hwDecode},
                {"blurFill", video.blurFill},
                {"autoCrop", video.autoCrop}}},
              {"attract",
               {{"enabled", attract.enabled},
                {"idleSeconds", attract.idleSeconds},
                {"displaySeconds", attract.displaySeconds},
                {"random", attract.random},
                {"categories", attract.categories},
                {"video", attract.video},
                {"mute", attract.mute},
                {"speed", attract.speed}}}};
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream f(file, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << j.dump(2);
    return (bool)f;
}

std::vector<std::string> Profile::List(const fs::path& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (e.is_regular_file() && util::ToLower(util::PathU8(e.path().extension())) == ".json")
            out.push_back(util::PathU8(e.path().stem()));
    std::sort(out.begin(), out.end());
    return out;
}

void Profile::EnsureDefaults(const fs::path& dir) {
    if (!List(dir).empty()) return;
    struct Def {
        const char* name;
        const char* theme;
        const char* mode;
    };
    for (Def d : {Def{"Racecab", "default", "racecab"}, Def{"GunCab", "neon", "guncab"},
                  Def{"Arcade classique", "default", ""}, Def{"Flipper", "neon", ""}, Def{"Air Hockey", "default", ""}}) {
        Profile p;
        p.name = d.name;
        p.theme = d.theme;
        p.mode = d.mode;
        p.Save(dir / (util::Widen(d.name) + L".json"));
    }
    logx::Info("Profils par défaut créés : Racecab, GunCab, Arcade classique, Flipper, Air Hockey");
}
