#include "ui/Wheel.h"

#include <algorithm>
#include <cmath>

void Wheel::Reset(int count, int selected) {
    count_ = std::max(0, count);
    target_ = count_ ? std::clamp(selected, 0, count_ - 1) : 0;
    scroll_ = (double)target_;
}

void Wheel::Move(int delta) {
    if (!count_) return;
    target_ += delta;
    // Sur un très long déplacement, on rapproche l'affichage pour ne pas faire défiler 50 logos.
    double gap = (double)target_ - scroll_;
    if (std::abs(gap) > 6) scroll_ = (double)target_ - (gap > 0 ? 6 : -6);
}

void Wheel::JumpTo(int index) {
    if (!count_) return;
    target_ = std::clamp(index, 0, count_ - 1);
    scroll_ = (double)target_;
}

int Wheel::Selected() const { return Wrap(target_); }

bool Wheel::Animating() const { return std::abs((double)target_ - scroll_) > 0.0005; }

void Wheel::Update(float dt, const WheelTheme& t) {
    // Peu d'éléments (pas de bouclage visuel) : la cible est ramenée dans la liste, sinon le centre
    // resterait vide après un passage du dernier au premier élément (ou un tir du pistolet).
    if (count_ && count_ <= t.visible && (target_ < 0 || target_ >= count_)) {
        long long shift = (long long)std::floor((double)target_ / count_) * count_;
        target_ -= shift;
        scroll_ -= (double)shift;
    }
    double k = 1.0 - std::exp(-(double)t.animSpeed * dt);
    scroll_ += ((double)target_ - scroll_) * k;
    if (!Animating()) scroll_ = (double)target_;
    // Ramène les compteurs près de zéro pour éviter toute dérive après des heures de navigation.
    if (count_ && std::abs(target_) > 1000000) {
        long long shift = (target_ / count_) * count_;
        target_ -= shift;
        scroll_ -= (double)shift;
    }
}

void Wheel::Layout(float W, float H, const WheelTheme& t, std::vector<Slot>& out) const {
    out.clear();
    if (!count_) return;
    const bool wheel = t.mode != "strip";
    const bool wrap = count_ > t.visible;  // peu d'éléments : pas de doublons visibles
    const float half = t.visible / 2.0f;
    const float baseW = t.itemW * W, baseH = t.itemH * H;
    const float cy = t.y * H;

    long long first = (long long)std::floor(scroll_ - half - 1);
    long long last = (long long)std::ceil(scroll_ + half + 1);
    for (long long k = first; k <= last; ++k) {
        if (!wrap && (k < 0 || k >= count_)) continue;
        float d = (float)((double)k - scroll_);
        float ad = std::abs(d);
        if (ad > half + 0.75f) continue;

        float scale;
        if (ad < 1) scale = t.selectedScale + (t.sideScale - t.selectedScale) * ad;
        else scale = wheel ? t.sideScale * std::pow(t.falloff, ad - 1) : t.sideScale;

        float alpha = ad < 1 ? 1.0f + (t.sideAlpha - 1.0f) * ad : t.sideAlpha;
        float edge = half + 0.75f - ad;  // fondu sur le dernier demi-cran
        if (edge < 0.75f) alpha *= std::max(0.0f, edge / 0.75f);

        float x = W / 2 + d * t.spacing * W;
        float nd = d / std::max(1.0f, half);  // -1..1 sur la largeur visible
        float y = cy - (wheel ? t.curvature * H * nd * nd : 0.0f);
        float w = baseW * scale, h = baseH * scale;
        Slot s;
        s.index = Wrap(k);
        s.rect = {x - w / 2, y - h / 2, w, h};
        s.alpha = alpha;
        s.angle = wheel ? d * t.tilt : 0.0;
        s.focus = std::max(0.0f, 1.0f - ad);
        out.push_back(s);
    }
    // Les plus éloignés d'abord : le logo sélectionné est dessiné par-dessus.
    std::sort(out.begin(), out.end(), [](const Slot& a, const Slot& b) { return a.focus < b.focus; });
}

void Wheel::PrefetchIndices(int margin, const WheelTheme& t, std::vector<int>& out) const {
    out.clear();
    if (!count_) return;
    out.push_back(Selected());
    int reach = t.visible / 2 + 1 + margin;
    for (int d = 1; d <= reach && (int)out.size() < count_; ++d) {
        out.push_back(Wrap(target_ + d));
        out.push_back(Wrap(target_ - d));
    }
}
