#pragma once
#include <string>
#include <vector>

#include "core/Util.h"

class Library;
struct Paths;

// Partage de la configuration entre bornes : un fichier .zip (sans compression) qui contient
//   config.json  : jeux (chemins, options de lancement, métadonnées, catégories, lien YouTube), catégories ;
//   profiles/    : profils (catégories masquées traduites en chemins « Course/Drift ») ;
//   themes/      : thèmes ;
//   media/<n>/   : images (et vidéos si demandé) des jeux.
// Ni historique, ni statistiques, ni réglages propres à la machine (type de borne, commandes, identifiants).
namespace share {

struct ExportOptions {
    bool images = true;    // logos, fonds, captures
    bool videos = false;   // fichiers vidéo (sinon : liens YouTube seulement)
    bool profiles = true;  // profils et thèmes
};
bool Export(const Library& lib, const Paths& paths, const ExportOptions& opt, const fs::path& outZip,
            std::string& summary, std::string& err);

struct Preview {
    int games = 0, newGames = 0, existing = 0, videoLinks = 0, profiles = 0, mediaFiles = 0;
    std::string commonRoot;    // dossier commun des jeux dans le fichier (ex. « D:\Jeux »)
    std::string exported;      // date d'export
    std::vector<std::string> missingPaths;  // jeux dont le fichier n'existe pas ici (après remplacement)
};
// Lit le fichier et compare avec la bibliothèque (rien n'est modifié). oldRoot/newRoot : remplacement de chemins.
bool ReadPreview(const Library& lib, const fs::path& zip, const std::string& oldRoot, const std::string& newRoot,
                 Preview& p, std::string& err);

struct ImportOptions {
    std::string oldRoot, newRoot;  // début des chemins à remplacer (dossier des jeux sur l'autre borne -> ici)
    bool profiles = true;          // profils (remplace ceux du même nom) et thèmes absents
    bool media = true;             // images et vidéos contenues dans le fichier
};
struct ImportResult {
    int added = 0, updated = 0, profiles = 0, mediaFiles = 0;
    std::vector<int64_t> needVideo;  // jeux avec un lien YouTube mais sans vidéo : à télécharger
    std::string summary;
};
// Jeu déjà présent (même nom, ou même fichier de lancement) : complété seulement (lien vidéo, catégories,
// métadonnées et médias manquants), jamais écrasé.
bool Import(Library& lib, const Paths& paths, const fs::path& zip, const ImportOptions& opt, ImportResult& r,
            std::string& err);

// Synchronisation avec la base du PC (Synchroniser.bat sur une borne) : la bibliothèque de baseData (dossier
// de données d'un frontend, ex. copie locale de H:\Bornes\Racecab\frontend\data) est fusionnée dans lib.
// Chemins oldRoot (H:\Bornes\Racecab) -> newRoot (C:), arguments compris. Jeux absents ajoutés avec leurs
// médias ; jeux déjà présents seulement complétés ; profils, réglages et statistiques de la borne intacts.
bool SyncFrom(Library& lib, const Paths& paths, const fs::path& baseData, const std::string& oldRoot,
              const std::string& newRoot, ImportResult& r, std::string& err);

// Toutes les occurrences de oldRoot (suivi de \, / ou ") dans un texte remplacées par newRoot.
std::string RemapIn(const std::string& text, const std::string& oldRoot, const std::string& newRoot);

}  // namespace share
