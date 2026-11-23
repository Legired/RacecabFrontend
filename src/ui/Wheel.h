#pragma once
#include <SDL3/SDL.h>

#include <vector>

#include "theme/Theme.h"

// Roue / bande horizontale de logos.
// La sélection logique change immédiatement à chaque commande (aucune latence) ;
// seule la position affichée « rattrape » la cible par interpolation exponentielle.
class Wheel {
public:
    struct Slot {
        int index;       // élément de la liste
        SDL_FRect rect;  // zone où dessiner le logo (déjà mise à l'échelle)
        float alpha;
        double angle;
        float focus;     // 1 = sélectionné, 0 = éloigné
    };

    void Reset(int count, int selected);
    void Move(int delta);
    void JumpTo(int index);
    int Selected() const;
    int Count() const { return count_; }
    bool Animating() const;

    void Update(float dt, const WheelTheme& t);
    // Calcule les emplacements visibles, du plus éloigné au plus proche (ordre de dessin).
    void Layout(float screenW, float screenH, const WheelTheme& t, std::vector<Slot>& out) const;
    // Indices à précharger (visibles + marge), sélection en premier.
    void PrefetchIndices(int margin, const WheelTheme& t, std::vector<int>& out) const;

private:
    int Wrap(long long i) const { return count_ ? (int)(((i % count_) + count_) % count_) : 0; }
    int count_ = 0;
    long long target_ = 0;  // non borné : permet le bouclage sans saut visuel
    double scroll_ = 0;
};
