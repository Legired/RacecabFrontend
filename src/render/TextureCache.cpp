#include "render/TextureCache.h"

#include <SDL3_image/SDL_image.h>

#include <algorithm>

#include "core/Log.h"

TextureCache::TextureCache(SDL_Renderer* r, size_t budgetBytes, int maxSide)
    : r_(r), budget_(budgetBytes), maxSide_(maxSide) {
    worker_ = std::thread([this] { WorkerLoop(); });
}

TextureCache::~TextureCache() {
    {
        std::lock_guard lk(m_);
        quit_ = true;
        queue_.clear();
    }
    cv_.notify_all();
    worker_.join();
    for (auto& [_, s] : done_) SDL_DestroySurface(s);
    for (auto& [_, e] : entries_)
        if (e.tex) SDL_DestroyTexture(e.tex);
}

void TextureCache::Want(const std::string& path) {
    if (wantedSet_.insert(path).second) wanted_.push_back(path);
}

SDL_Texture* TextureCache::Get(const std::string& path) {
    if (path.empty()) return nullptr;
    auto it = entries_.find(path);
    if (it != entries_.end()) {
        it->second.lastUse = frame_;
        return it->second.tex;
    }
    Want(path);
    return nullptr;
}

void TextureCache::Prefetch(const std::string& path) {
    if (path.empty()) return;
    auto it = entries_.find(path);
    if (it != entries_.end()) {
        it->second.lastUse = frame_;
        return;
    }
    Want(path);
}

bool TextureCache::Failed(const std::string& path) const {
    auto it = entries_.find(path);
    return it != entries_.end() && it->second.failed;
}

void TextureCache::Invalidate(const std::string& path) {
    auto it = entries_.find(path);
    if (it == entries_.end()) return;
    if (it->second.tex) SDL_DestroyTexture(it->second.tex);
    bytes_ -= it->second.bytes;
    entries_.erase(it);
}

size_t TextureCache::Pending() {
    std::lock_guard lk(m_);
    return queue_.size() + inflight_.size();
}

void TextureCache::EndFrame() {
    // 1) Récupérer les images décodées (au plus 4 envois GPU par frame pour éviter les à-coups)
    std::vector<std::pair<std::string, SDL_Surface*>> ready;
    {
        std::lock_guard lk(m_);
        size_t n = std::min<size_t>(done_.size(), 4);
        ready.assign(done_.begin(), done_.begin() + n);
        done_.erase(done_.begin(), done_.begin() + n);
        // 2) Publier la nouvelle liste de demandes (dans l'ordre de priorité de la frame)
        // Une image déjà décodée mais pas encore envoyée (au plus 4 par frame) ne doit pas être redemandée :
        // elle serait décodée deux fois et la première texture ne serait jamais libérée (fuite constatée
        // au test d'endurance : environ 160 Mo par minute en démonstration).
        std::unordered_set<std::string> pendingUpload;
        for (const auto& d : done_) pendingUpload.insert(d.first);
        for (const auto& d : ready) pendingUpload.insert(d.first);  // envoyées juste après, hors verrou
        queue_.clear();
        for (const std::string& p : wanted_)
            if (!inflight_.count(p) && !entries_.count(p) && !pendingUpload.count(p)) queue_.push_back(p);
    }
    bool hasWork = !wanted_.empty();
    wanted_.clear();
    wantedSet_.clear();
    if (hasWork) cv_.notify_one();

    for (auto& [path, surf] : ready) {
        Entry e;
        e.lastUse = frame_;
        if (surf) {
            e.tex = SDL_CreateTextureFromSurface(r_, surf);
            if (e.tex) {
                SDL_SetTextureScaleMode(e.tex, SDL_SCALEMODE_LINEAR);
                // Sans cela, une image sans transparence (JPEG) ignore l'opacité demandée (fondus, fond atténué).
                SDL_SetTextureBlendMode(e.tex, SDL_BLENDMODE_BLEND);
                e.bytes = (size_t)surf->w * surf->h * 4;
            } else {
                logx::Warn("Texture impossible pour {} : {}", path, SDL_GetError());
                e.failed = true;
            }
            SDL_DestroySurface(surf);
        } else {
            e.failed = true;
        }
        Invalidate(path);  // par sécurité : jamais deux textures pour le même fichier
        bytes_ += e.bytes;
        entries_[path] = e;
    }

    // 3) Éviction LRU : jamais une texture utilisée dans cette frame
    if (bytes_ > budget_) {
        std::vector<std::pair<uint64_t, std::string>> order;
        for (auto& [p, e] : entries_)
            if (e.lastUse < frame_) order.push_back({e.lastUse, p});
        std::sort(order.begin(), order.end());
        for (auto& [_, p] : order) {
            if (bytes_ <= budget_ * 9 / 10) break;
            Invalidate(p);
        }
    }
    ++frame_;
}

void TextureCache::WorkerLoop() {
    for (;;) {
        std::string path;
        {
            std::unique_lock lk(m_);
            cv_.wait(lk, [this] { return quit_ || !queue_.empty(); });
            if (quit_) return;
            path = queue_.front();
            queue_.erase(queue_.begin());
            inflight_.insert(path);
        }
        SDL_Surface* s = IMG_Load(path.c_str());
        if (!s) {
            logx::Warn("Image illisible : {} ({})", path, SDL_GetError());
        } else if (maxSide_ > 0 && (s->w > maxSide_ || s->h > maxSide_)) {
            float k = (float)maxSide_ / (float)std::max(s->w, s->h);
            SDL_Surface* scaled = SDL_ScaleSurface(s, std::max(1, (int)(s->w * k)), std::max(1, (int)(s->h * k)),
                                                   SDL_SCALEMODE_LINEAR);
            if (scaled) {
                SDL_DestroySurface(s);
                s = scaled;
            }
        }
        std::lock_guard lk(m_);
        inflight_.erase(path);
        done_.push_back({path, s});
    }
}
