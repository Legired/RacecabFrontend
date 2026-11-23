#pragma once
#include <string>

#include "core/Util.h"

// Extrait de [start, start + duration] secondes d'une vidéo, sans réencodage (copie des flux) :
// vidéo prise dans videoIn, son pris dans audioIn (facultatif, vide = son de videoIn s'il en a un).
// Le découpage commence à l'image clé la plus proche avant start. Sortie MP4 (moov en tête, lecture immédiate).
// Si la vidéo est plus courte que start + duration, l'extrait est décalé vers le début.
bool RemuxClip(const fs::path& videoIn, const fs::path& audioIn, const fs::path& out, double start, double duration,
               std::string& err);

// Durée en secondes d'un fichier média (0 si illisible).
double MediaDuration(const fs::path& file);
