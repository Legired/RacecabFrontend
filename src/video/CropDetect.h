#pragma once
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#include "core/Util.h"

// Zone utile d'une vidéo (sans les bandes noires incrustées dans l'image), en fractions de la largeur et de
// la hauteur : {0, 0, 1, 1} = pas de bandes.
struct CropRect {
    float x = 0, y = 0, w = 1, h = 1;
    bool Full() const { return x <= 0 && y <= 0 && w >= 1 && h >= 1; }
};

// Analyse 5 images réparties dans la vidéo (décodage logiciel) et renvoie la plus petite zone qui contient
// le contenu de toutes. Une bande n'est retirée que si elle fait au moins 2 % de l'image.
bool DetectCrop(const fs::path& file, CropRect& out, std::string& err);

// Cache des zones utiles : <données>\cache\videocrop.json, clé = chemin + taille + date du fichier
// (une vidéo remplacée est réanalysée). L'analyse se fait dans un thread à part : Get() ne bloque jamais.
class CropCache {
public:
    ~CropCache();
    void Init(const fs::path& file);
    // true si la zone est connue (out rempli) ; sinon l'analyse est demandée et false est renvoyé.
    bool Get(const std::string& videoPath, CropRect& out);
    void Forget(const std::string& videoPath);  // fichier remplacé : nouvelle analyse au prochain Get()

private:
    static std::string Key(const std::string& videoPath);
    void Loop();
    void Save();

    fs::path file_;
    std::mutex m_;
    std::condition_variable cv_;
    std::map<std::string, CropRect> known_;  // clé -> zone
    std::map<std::string, std::string> keyOf_;  // chemin -> clé (évite de relire la date à chaque image)
    std::deque<std::string> queue_;
    std::set<std::string> pending_;
    bool quit_ = false;
    std::thread worker_;
};
