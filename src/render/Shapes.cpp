#include "render/Shapes.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace shapes {
namespace {

constexpr int kSeg = 8;  // segments par quart de cercle
constexpr float kPi = 3.14159265f;

SDL_FColor FC(Color c, float a) {
    return {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, std::clamp(c.a / 255.0f * a, 0.0f, 1.0f)};
}

// Contour d'un rectangle arrondi, dans le sens horaire à partir du coin haut gauche (même nombre de points
// quel que soit le rayon, pour pouvoir relier contour extérieur et intérieur).
void RoundPts(const SDL_FRect& rc, float rad, std::vector<SDL_FPoint>& out) {
    rad = std::clamp(rad, 0.0f, std::min(rc.w, rc.h) / 2);
    const float cx[4] = {rc.x + rad, rc.x + rc.w - rad, rc.x + rc.w - rad, rc.x + rad};
    const float cy[4] = {rc.y + rad, rc.y + rad, rc.y + rc.h - rad, rc.y + rc.h - rad};
    out.clear();
    for (int k = 0; k < 4; ++k)
        for (int i = 0; i <= kSeg; ++i) {
            float a = kPi * (1.0f + 0.5f * k) + (kPi / 2) * i / kSeg;
            out.push_back({cx[k] + rad * std::cos(a), cy[k] + rad * std::sin(a)});
        }
}

void Quad(SDL_Renderer* r, SDL_FPoint a, SDL_FPoint b, SDL_FPoint c, SDL_FPoint d, SDL_FColor col) {
    SDL_Vertex v[4] = {{a, col, {}}, {b, col, {}}, {c, col, {}}, {d, col, {}}};
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(r, nullptr, v, 4, idx, 6);
}

}  // namespace

void FillRound(SDL_Renderer* r, const SDL_FRect& rc, float radius, Color c, float alpha) {
    if (rc.w <= 0 || rc.h <= 0) return;
    std::vector<SDL_FPoint> pts;
    RoundPts(rc, radius, pts);
    const SDL_FColor col = FC(c, alpha);
    std::vector<SDL_Vertex> v;
    v.reserve(pts.size() + 1);
    v.push_back({{rc.x + rc.w / 2, rc.y + rc.h / 2}, col, {}});
    for (auto p : pts) v.push_back({p, col, {}});
    std::vector<int> idx;
    const int n = (int)pts.size();
    for (int i = 0; i < n; ++i) {
        idx.push_back(0);
        idx.push_back(1 + i);
        idx.push_back(1 + (i + 1) % n);
    }
    SDL_RenderGeometry(r, nullptr, v.data(), (int)v.size(), idx.data(), (int)idx.size());
}

void FillRoundGradientH(SDL_Renderer* r, const SDL_FRect& rc, float radius, Color c0, float a0, Color c1, float a1) {
    if (rc.w <= 0 || rc.h <= 0) return;
    std::vector<SDL_FPoint> pts;
    RoundPts(rc, radius, pts);
    const SDL_FColor f0 = FC(c0, a0), f1 = FC(c1, a1);
    auto at = [&](float x) {  // couleur interpolée selon la position horizontale (dégradé exact : linéaire par triangle)
        float t = std::clamp((x - rc.x) / rc.w, 0.0f, 1.0f);
        return SDL_FColor{f0.r + (f1.r - f0.r) * t, f0.g + (f1.g - f0.g) * t, f0.b + (f1.b - f0.b) * t, f0.a + (f1.a - f0.a) * t};
    };
    std::vector<SDL_Vertex> v;
    v.push_back({{rc.x + rc.w / 2, rc.y + rc.h / 2}, at(rc.x + rc.w / 2), {}});
    for (auto p : pts) v.push_back({p, at(p.x), {}});
    std::vector<int> idx;
    const int n = (int)pts.size();
    for (int i = 0; i < n; ++i) idx.insert(idx.end(), {0, 1 + i, 1 + (i + 1) % n});
    SDL_RenderGeometry(r, nullptr, v.data(), (int)v.size(), idx.data(), (int)idx.size());
}

void StrokeRound(SDL_Renderer* r, const SDL_FRect& rc, float radius, float thick, Color c, float alpha) {
    if (rc.w <= 0 || rc.h <= 0 || thick <= 0) return;
    std::vector<SDL_FPoint> outer, inner;
    RoundPts(rc, radius, outer);
    RoundPts({rc.x + thick, rc.y + thick, rc.w - 2 * thick, rc.h - 2 * thick}, radius - thick, inner);
    const SDL_FColor col = FC(c, alpha);
    const int n = (int)outer.size();
    std::vector<SDL_Vertex> v;
    v.reserve(n * 2);
    for (int i = 0; i < n; ++i) {
        v.push_back({outer[i], col, {}});
        v.push_back({inner[i], col, {}});
    }
    std::vector<int> idx;
    for (int i = 0; i < n; ++i) {
        int a = 2 * i, b = 2 * i + 1, c2 = 2 * ((i + 1) % n), d = c2 + 1;
        idx.insert(idx.end(), {a, b, c2, b, d, c2});
    }
    SDL_RenderGeometry(r, nullptr, v.data(), (int)v.size(), idx.data(), (int)idx.size());
}

void Gradient(SDL_Renderer* r, const SDL_FRect& rc, Color c0, float a0, Color c1, float a1, bool horizontal) {
    const SDL_FColor f0 = FC(c0, a0), f1 = FC(c1, a1);
    SDL_Vertex v[4] = {
        {{rc.x, rc.y}, f0, {}},
        {{rc.x + rc.w, rc.y}, horizontal ? f1 : f0, {}},
        {{rc.x + rc.w, rc.y + rc.h}, f1, {}},
        {{rc.x, rc.y + rc.h}, horizontal ? f0 : f1, {}},
    };
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(r, nullptr, v, 4, idx, 6);
}

void Chevron(SDL_Renderer* r, float cx, float cy, float s, float t, bool left, Color c, float alpha) {
    // Chevron « > » formé de deux bras (quadrilatères convexes), puis miroir pour « < ».
    const float x0 = cx - (s + t) / 2;
    auto X = [&](float x) { return left ? 2 * cx - x : x; };
    const SDL_FColor col = FC(c, alpha);
    Quad(r, {X(x0), cy - s}, {X(x0 + t), cy - s}, {X(x0 + s + t), cy}, {X(x0 + s), cy}, col);
    Quad(r, {X(x0 + s), cy}, {X(x0 + s + t), cy}, {X(x0 + t), cy + s}, {X(x0), cy + s}, col);
}

void Play(SDL_Renderer* r, float cx, float cy, float s, Color c, float alpha) {
    const SDL_FColor col = FC(c, alpha);
    SDL_Vertex v[3] = {{{cx - s * 0.38f, cy - s / 2}, col, {}},
                       {{cx + s * 0.5f, cy}, col, {}},
                       {{cx - s * 0.38f, cy + s / 2}, col, {}}};
    SDL_RenderGeometry(r, nullptr, v, 3, nullptr, 0);
}

void DrawIcon(SDL_Renderer* r, Icon icon, float cx, float cy, float s, Color c, float alpha) {
    const float h = s / 2;
    switch (icon) {
        case Icon::Back:
            Chevron(r, cx, cy, h * 0.8f, s * 0.16f, true, c, alpha);
            break;
        case Icon::Grid: {
            const float g = s * 0.12f, w = (s - g) / 2;
            for (int i = 0; i < 4; ++i)
                FillRound(r, {cx - h + (i % 2) * (w + g), cy - h + (i / 2) * (w + g), w, w}, s * 0.06f, c, alpha);
            break;
        }
        case Icon::Single:
            StrokeRound(r, {cx - h, cy - h * 0.75f, s, s * 0.75f}, s * 0.08f, std::max(2.0f, s * 0.12f), c, alpha);
            break;
        case Icon::Menu:
            for (int i = 0; i < 3; ++i)
                FillRound(r, {cx - h, cy - h * 0.8f + i * h * 0.7f, s, s * 0.15f}, s * 0.075f, c, alpha);
            break;
    }
}

}  // namespace shapes
