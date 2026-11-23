#pragma once
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <map>
#include <string>
#include <unordered_map>

#include "theme/Theme.h"

enum class Align { Left, Center, Right };

// Rendu de texte avec cache de textures (une texture par chaîne/police/couleur/largeur).
class TextRenderer {
public:
    explicit TextRenderer(SDL_Renderer* r) : r_(r) {}
    ~TextRenderer();

    // Ouvre (ou réutilise) une police ; repli sur Segoe UI / Arial si le fichier est introuvable.
    TTF_Font* Font(const std::string& path, int px);

    // Dessine et renvoie la taille occupée. wrapW > 0 : retour à la ligne automatique.
    SDL_FPoint Draw(TTF_Font* f, const std::string& text, float x, float y, Color c, Align a = Align::Left,
                    int wrapW = 0, float alpha = 1.0f);
    // Dessine en ajustant la texture dans un rectangle (logos de remplacement).
    void DrawFit(TTF_Font* f, const std::string& text, const SDL_FRect& box, Color c, int wrapW, float alpha,
                 double angle = 0.0);
    SDL_FPoint Measure(TTF_Font* f, const std::string& text, int wrapW = 0);

    void EndFrame();  // libère les textes inutilisés depuis un moment
    size_t CacheSize() const { return cache_.size(); }

private:
    struct Cached {
        SDL_Texture* tex = nullptr;
        float w = 0, h = 0;
        uint64_t lastUse = 0;
    };
    Cached* Get(TTF_Font* f, const std::string& text, Color c, int wrapW);

    SDL_Renderer* r_;
    std::map<std::pair<std::string, int>, TTF_Font*> fonts_;
    std::unordered_map<std::string, Cached> cache_;
    uint64_t frame_ = 1;
};
