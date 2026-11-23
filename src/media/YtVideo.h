#pragma once
#include <atomic>
#include <string>

#include "core/Util.h"

struct ScraperConfig;

// Vidéos des jeux depuis les liens YouTube de la base LaunchBox (champ VideoURL).
// Le téléchargement passe par yt-dlp, installé par l'utilisateur (non fourni avec le frontend) :
//   - réglage « ytdlpPath » de scraper.json, sinon yt-dlp.exe à côté du programme,
//     dans <données>\tools, ou dans le PATH ;
//   - vidéo H.264 (720p au plus) et son AAC téléchargés séparément : ffmpeg n'est pas nécessaire ;
//   - extrait de videoSeconds secondes à partir de videoStartSec, assemblé sans réencodage (RemuxClip).
// Usage personnel : les conditions d'utilisation de YouTube interdisent le téléchargement, les vidéos
// appartiennent à leurs auteurs.
namespace ytvideo {

fs::path FindYtDlp(const ScraperConfig& c);  // vide si introuvable

// Mise à jour de yt-dlp par lui-même (yt-dlp -U). out : résumé lisible.
bool SelfUpdate(const ScraperConfig& c, std::string& out);

// Début en secondes : « 90 », « 1:30 », « 1m30s », ou le paramètre t= d'un lien YouTube. -1 si absent ou illisible.
int ParseStart(const std::string& s);
int StartFromUrl(const std::string& url);

// Télécharge la vidéo d'url et écrit <dir>\video.mp4. Annulable (cancel) : yt-dlp est alors arrêté.
// Si video.mp4 est en cours de lecture (fichier verrouillé), l'extrait reste dans *pendingPart (si fourni) :
// l'appelant le met en place une fois la lecture arrêtée. Sinon, c'est une erreur.
bool Download(const ScraperConfig& c, const std::string& url, const fs::path& dir, const std::atomic<bool>& cancel,
              std::string& err, fs::path* pendingPart = nullptr);

}  // namespace ytvideo
