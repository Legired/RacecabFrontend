#pragma once
#include <SDL3/SDL.h>

#include "theme/Theme.h"

// Formes vectorielles dessinées avec SDL_RenderGeometry (anticrénelage non garanti, mais nettes en plein écran) :
// rectangles arrondis, contours, dégradés, chevrons et icônes de la disposition pistolet.
namespace shapes {

void FillRound(SDL_Renderer* r, const SDL_FRect& rc, float radius, Color c, float alpha = 1.0f);
void StrokeRound(SDL_Renderer* r, const SDL_FRect& rc, float radius, float thick, Color c, float alpha = 1.0f);
// Rectangle arrondi rempli d'un dégradé horizontal (gauche -> droite).
void FillRoundGradientH(SDL_Renderer* r, const SDL_FRect& rc, float radius, Color c0, float a0, Color c1, float a1);
// Dégradé linéaire : horizontal (gauche -> droite) ou vertical (haut -> bas).
void Gradient(SDL_Renderer* r, const SDL_FRect& rc, Color c0, float a0, Color c1, float a1, bool horizontal);
// Chevron centré en (cx, cy) : demi-hauteur s, épaisseur t, pointe à gauche ou à droite.
void Chevron(SDL_Renderer* r, float cx, float cy, float s, float t, bool left, Color c, float alpha = 1.0f);
// Triangle « lecture » pointant à droite, inscrit dans un carré de côté s centré en (cx, cy).
void Play(SDL_Renderer* r, float cx, float cy, float s, Color c, float alpha = 1.0f);

enum class Icon { Back, Grid, Single, Menu };
void DrawIcon(SDL_Renderer* r, Icon icon, float cx, float cy, float s, Color c, float alpha = 1.0f);

}  // namespace shapes
