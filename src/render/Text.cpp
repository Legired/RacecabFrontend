#include "render/Text.h"

#include <algorithm>
#include <format>

#include "core/Log.h"

TextRenderer::~TextRenderer() {
    for (auto& [_, c] : cache_)
        if (c.tex) SDL_DestroyTexture(c.tex);
    for (auto& [_, f] : fonts_)
        if (f) TTF_CloseFont(f);
}

TTF_Font* TextRenderer::Font(const std::string& path, int px) {
    px = std::max(6, px);
    auto key = std::make_pair(path, px);
    auto it = fonts_.find(key);
    if (it != fonts_.end()) return it->second;
    TTF_Font* f = TTF_OpenFont(path.c_str(), (float)px);
    if (!f) {
        logx::Warn("Police introuvable : {} ({}), repli sur Segoe UI", path, SDL_GetError());
        for (const char* fb : {"C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf"})
            if ((f = TTF_OpenFont(fb, (float)px))) break;
    }
    if (f) {
        TTF_SetFontHinting(f, TTF_HINTING_LIGHT);
        // Le crénage de SDL_ttf (sans HarfBuzz) écarte à tort « T » et la lettre suivante avec Segoe UI.
        TTF_SetFontKerning(f, false);
    }
    fonts_[key] = f;
    return f;
}

TextRenderer::Cached* TextRenderer::Get(TTF_Font* f, const std::string& text, Color c, int wrapW) {
    if (!f || text.empty()) return nullptr;
    std::string key = std::format("{}|{}|{}.{}.{}|{}|{}", (void*)f, wrapW, c.r, c.g, c.b, c.a, text);
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        it->second.lastUse = frame_;
        return &it->second;
    }
    SDL_Color col{c.r, c.g, c.b, 255};
    SDL_Surface* s = wrapW > 0 ? TTF_RenderText_Blended_Wrapped(f, text.c_str(), text.size(), col, wrapW)
                               : TTF_RenderText_Blended(f, text.c_str(), text.size(), col);
    Cached out;
    out.lastUse = frame_;
    if (s) {
        out.tex = SDL_CreateTextureFromSurface(r_, s);
        out.w = (float)s->w;
        out.h = (float)s->h;
        if (out.tex) SDL_SetTextureScaleMode(out.tex, SDL_SCALEMODE_LINEAR);
        SDL_DestroySurface(s);
    }
    return &(cache_[key] = out);
}

SDL_FPoint TextRenderer::Draw(TTF_Font* f, const std::string& text, float x, float y, Color c, Align a, int wrapW,
                              float alpha) {
    Cached* t = Get(f, text, c, wrapW);
    if (!t || !t->tex) return {0, 0};
    if (a == Align::Center) x -= t->w / 2;
    else if (a == Align::Right) x -= t->w;
    SDL_SetTextureAlphaModFloat(t->tex, std::clamp(alpha * c.a / 255.0f, 0.0f, 1.0f));
    SDL_FRect dst{std::floor(x), std::floor(y), t->w, t->h};
    SDL_RenderTexture(r_, t->tex, nullptr, &dst);
    return {t->w, t->h};
}

void TextRenderer::DrawFit(TTF_Font* f, const std::string& text, const SDL_FRect& box, Color c, int wrapW, float alpha,
                           double angle) {
    Cached* t = Get(f, text, c, wrapW);
    if (!t || !t->tex) return;
    float k = std::min({box.w / t->w, box.h / t->h, 1.0f});
    SDL_FRect dst{box.x + (box.w - t->w * k) / 2, box.y + (box.h - t->h * k) / 2, t->w * k, t->h * k};
    SDL_SetTextureAlphaModFloat(t->tex, std::clamp(alpha, 0.0f, 1.0f));
    SDL_RenderTextureRotated(r_, t->tex, nullptr, &dst, angle, nullptr, SDL_FLIP_NONE);
}

SDL_FPoint TextRenderer::Measure(TTF_Font* f, const std::string& text, int wrapW) {
    Cached* t = Get(f, text, {255, 255, 255, 255}, wrapW);
    return t ? SDL_FPoint{t->w, t->h} : SDL_FPoint{0, 0};
}

void TextRenderer::EndFrame() {
    ++frame_;
    if (cache_.size() < 400) return;
    for (auto it = cache_.begin(); it != cache_.end();) {
        if (frame_ - it->second.lastUse > 120) {
            if (it->second.tex) SDL_DestroyTexture(it->second.tex);
            it = cache_.erase(it);
        } else {
            ++it;
        }
    }
}
