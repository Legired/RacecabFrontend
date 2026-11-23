#include "theme/Theme.h"

#include <windows.h>

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>

#include "core/Log.h"

using nlohmann::json;

namespace {

json ToJ(const Color& c) { return json::array({c.r, c.g, c.b, c.a}); }
json ToJ(const RectF& r) { return json::array({r.x, r.y, r.w, r.h}); }

void FromJ(const json& j, Color& c) {
    if (!j.is_array() || j.size() < 3) return;
    c.r = j[0].get<uint8_t>();
    c.g = j[1].get<uint8_t>();
    c.b = j[2].get<uint8_t>();
    c.a = j.size() > 3 ? j[3].get<uint8_t>() : 255;
}
void FromJ(const json& j, RectF& r) {
    if (!j.is_array() || j.size() < 4) return;
    r = {j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>()};
}
template <class T> void FromJ(const json& j, T& v) { v = j.get<T>(); }

template <class T> void Get(const json& j, const char* k, T& v) {
    if (!j.contains(k)) return;
    try {
        FromJ(j.at(k), v);
    } catch (const std::exception& e) {
        logx::Warn("Thème : valeur invalide pour '{}' ({})", k, e.what());
    }
}

}  // namespace

bool Theme::Load(const fs::path& themesDir, const std::string& themeName) {
    name = themeName;
    dir = themesDir / util::U8Path(themeName);
    fs::path file = dir / L"theme.json";
    std::error_code ec;
    if (!fs::exists(file, ec)) {
        logx::Info("Thème '{}' absent : création avec les valeurs par défaut", themeName);
        return Save();
    }
    std::ifstream f(file, std::ios::binary);
    json j;
    try {
        j = json::parse(f, nullptr, true, true);
    } catch (const std::exception& e) {
        logx::Error("Thème illisible ({}) : valeurs par défaut utilisées", e.what());
        return false;
    }
    Get(j, "backgroundTop", backgroundTop);
    Get(j, "backgroundBottom", backgroundBottom);
    Get(j, "panel", panel);
    Get(j, "accent", accent);
    Get(j, "text", text);
    Get(j, "textDim", textDim);
    Get(j, "fontTitle", fontTitle);
    Get(j, "fontBody", fontBody);
    Get(j, "titleSize", titleSize);
    Get(j, "bodySize", bodySize);
    Get(j, "smallSize", smallSize);
    Get(j, "fanartBackgroundAlpha", fanartBackgroundAlpha);
    Get(j, "headerRect", headerRect);
    Get(j, "mediaRect", mediaRect);
    Get(j, "infoRect", infoRect);
    Get(j, "showStats", showStats);
    Get(j, "showHeader", showHeader);
    Get(j, "showMeta", showMeta);
    Get(j, "showDescription", showDescription);
    Get(j, "backgroundImage", backgroundImage);
    if (j.contains("wheel")) {
        const json& w = j["wheel"];
        Get(w, "mode", wheel.mode);
        Get(w, "y", wheel.y);
        Get(w, "visible", wheel.visible);
        Get(w, "itemW", wheel.itemW);
        Get(w, "itemH", wheel.itemH);
        Get(w, "spacing", wheel.spacing);
        Get(w, "selectedScale", wheel.selectedScale);
        Get(w, "sideScale", wheel.sideScale);
        Get(w, "falloff", wheel.falloff);
        Get(w, "curvature", wheel.curvature);
        Get(w, "tilt", wheel.tilt);
        Get(w, "sideAlpha", wheel.sideAlpha);
        Get(w, "animSpeed", wheel.animSpeed);
        Get(w, "bandHeight", wheel.bandHeight);
        Get(w, "bandColor", wheel.bandColor);
    }
    if (wheel.visible < 1) wheel.visible = 1;
    logx::Info("Thème chargé : {}", themeName);
    return true;
}

bool Theme::Save() const {
    json w = {{"mode", wheel.mode},       {"y", wheel.y},
              {"visible", wheel.visible}, {"itemW", wheel.itemW},
              {"itemH", wheel.itemH},     {"spacing", wheel.spacing},
              {"selectedScale", wheel.selectedScale}, {"sideScale", wheel.sideScale},
              {"falloff", wheel.falloff}, {"curvature", wheel.curvature},
              {"tilt", wheel.tilt},       {"sideAlpha", wheel.sideAlpha},
              {"animSpeed", wheel.animSpeed}, {"bandHeight", wheel.bandHeight},
              {"bandColor", ToJ(wheel.bandColor)}};
    json j = {{"backgroundTop", ToJ(backgroundTop)},
              {"backgroundBottom", ToJ(backgroundBottom)},
              {"panel", ToJ(panel)},
              {"accent", ToJ(accent)},
              {"text", ToJ(text)},
              {"textDim", ToJ(textDim)},
              {"fontTitle", fontTitle},
              {"fontBody", fontBody},
              {"titleSize", titleSize},
              {"bodySize", bodySize},
              {"smallSize", smallSize},
              {"fanartBackgroundAlpha", fanartBackgroundAlpha},
              {"headerRect", ToJ(headerRect)},
              {"mediaRect", ToJ(mediaRect)},
              {"infoRect", ToJ(infoRect)},
              {"showStats", showStats},
              {"showHeader", showHeader},
              {"showMeta", showMeta},
              {"showDescription", showDescription},
              {"backgroundImage", backgroundImage},
              {"wheel", w}};
    std::error_code ec;
    fs::create_directories(dir, ec);
    fs::path file = dir / L"theme.json", tmp = dir / L"theme.json.tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << j.dump(2);
    }
    return MoveFileExW(tmp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

std::vector<std::string> Theme::List(const fs::path& themesDir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(themesDir, ec))
        if (e.is_directory() && fs::exists(e.path() / L"theme.json", ec)) out.push_back(util::PathU8(e.path().filename()));
    std::sort(out.begin(), out.end());
    return out;
}

void Theme::EnsureBuiltins(const fs::path& themesDir) {
    std::error_code ec;
    if (!fs::exists(themesDir / L"default" / L"theme.json", ec)) {
        Theme t;
        t.name = "default";
        t.dir = themesDir / L"default";
        t.Save();
    }
    if (!fs::exists(themesDir / L"neon" / L"theme.json", ec)) {
        // Deuxième thème fourni : autre disposition (média centré, infos dessous, bande simple de 9 logos).
        Theme t;
        t.name = "neon";
        t.dir = themesDir / L"neon";
        t.backgroundTop = {14, 0, 32, 255};
        t.backgroundBottom = {0, 0, 0, 255};
        t.panel = {0, 230, 255, 22};
        t.accent = {0, 235, 255, 255};
        t.text = {255, 255, 255, 255};
        t.textDim = {175, 150, 215, 255};
        t.fontTitle = "C:/Windows/Fonts/bahnschrift.ttf";
        t.fontBody = "C:/Windows/Fonts/bahnschrift.ttf";
        t.titleSize = 0.045f;
        t.bodySize = 0.022f;
        t.fanartBackgroundAlpha = 0.15f;
        t.mediaRect = {0.22f, 0.07f, 0.56f, 0.50f};
        t.infoRect = {0.06f, 0.60f, 0.88f, 0.17f};
        t.showDescription = false;
        t.wheel.mode = "strip";
        t.wheel.y = 0.885f;
        t.wheel.visible = 9;
        t.wheel.itemW = 0.09f;
        t.wheel.itemH = 0.10f;
        t.wheel.spacing = 0.12f;
        t.wheel.selectedScale = 1.55f;
        t.wheel.sideScale = 0.8f;
        t.wheel.sideAlpha = 0.45f;
        t.wheel.bandHeight = 0.2f;
        t.wheel.bandColor = {0, 0, 0, 200};
        t.Save();
    }
}

std::string Theme::ResolveFile(const std::string& p) const {
    if (p.empty()) return p;
    fs::path pp = util::U8Path(p);
    if (pp.is_relative()) pp = dir / pp;
    return util::PathU8(pp);
}
