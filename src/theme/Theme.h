#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/Util.h"

struct Color {
    uint8_t r = 255, g = 255, b = 255, a = 255;
};

// Rectangle en fractions de l'écran (0..1)
struct RectF {
    float x = 0, y = 0, w = 1, h = 1;
};

struct WheelTheme {
    std::string mode = "wheel";  // "wheel" (arc, rotation, échelle) ou "strip" (bande simple)
    float y = 0.855f;            // centre vertical de la roue
    int visible = 7;             // nombre de logos visibles
    float itemW = 0.13f;         // taille d'un logo (fraction de la largeur / hauteur)
    float itemH = 0.12f;
    float spacing = 0.15f;       // écart horizontal entre deux logos (fraction de la largeur)
    float selectedScale = 1.4f;
    float sideScale = 0.85f;
    float falloff = 0.92f;       // réduction supplémentaire par cran d'éloignement (mode wheel)
    float curvature = 0.05f;     // remontée des logos aux bords de la roue (fraction de la hauteur, mode wheel)
    float tilt = 4.0f;           // rotation en degrés par cran (mode wheel)
    float sideAlpha = 0.55f;
    float animSpeed = 16.0f;     // plus élevé = plus rapide
    float bandHeight = 0.25f;
    Color bandColor{0, 0, 0, 150};
};

struct Theme {
    std::string name = "default";
    fs::path dir;
    Color backgroundTop{18, 22, 38, 255};
    Color backgroundBottom{4, 5, 10, 255};
    Color panel{255, 255, 255, 18};
    Color accent{255, 170, 40, 255};
    Color text{240, 240, 245, 255};
    Color textDim{150, 155, 170, 255};
    std::string fontTitle = "C:/Windows/Fonts/segoeuib.ttf";
    std::string fontBody = "C:/Windows/Fonts/segoeui.ttf";
    float titleSize = 0.052f;  // fractions de la hauteur d'écran
    float bodySize = 0.024f;
    float smallSize = 0.019f;
    float fanartBackgroundAlpha = 0.22f;  // image de fond du jeu derrière l'interface
    RectF headerRect{0.03f, 0.025f, 0.94f, 0.05f};
    RectF mediaRect{0.03f, 0.09f, 0.725f, 0.62f};
    RectF infoRect{0.775f, 0.09f, 0.195f, 0.62f};
    bool showStats = true;
    bool showHeader = true;       // chemin de catégorie et compteur
    bool showMeta = true;         // année · éditeur · genre · plateforme
    bool showDescription = true;
    std::string backgroundImage;  // image de fond du thème (relative au dossier du thème), sous le fanart
    WheelTheme wheel;

    // Charge <themes>/<nom>/theme.json ; crée le fichier par défaut s'il n'existe pas.
    bool Load(const fs::path& themesDir, const std::string& name);
    bool Save() const;
    std::string ResolveFile(const std::string& p) const;  // chemins relatifs au dossier du thème

    static std::vector<std::string> List(const fs::path& themesDir);
    static void EnsureBuiltins(const fs::path& themesDir);  // « default » et « neon »
};
