#pragma once
#include <SDL3/SDL.h>

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Cache de textures à chargement asynchrone.
// - Get() ne bloque jamais : renvoie nullptr tant que l'image n'est pas prête.
// - Le décodage disque se fait dans un thread de travail ; seul l'envoi au GPU se fait sur le thread principal.
// - La file d'attente est reconstruite à chaque frame avec ce qui est réellement demandé :
//   en défilement rapide, les images qui ne sont plus à l'écran ne sont jamais décodées.
// - Le budget mémoire est respecté par éviction LRU des textures non utilisées dans la frame courante.
class TextureCache {
public:
    TextureCache(SDL_Renderer* r, size_t budgetBytes, int maxSide);
    ~TextureCache();

    SDL_Texture* Get(const std::string& path);   // demande + renvoie si prête
    void Prefetch(const std::string& path);      // demande sans utiliser (voisins de la roue)
    bool Failed(const std::string& path) const;  // fichier absent ou illisible
    void EndFrame();                             // publie les demandes, envoie au GPU, évince
    void Invalidate(const std::string& path);    // fichier remplacé sur disque

    size_t BytesUsed() const { return bytes_; }
    size_t Count() const { return entries_.size(); }
    size_t Pending();

private:
    struct Entry {
        SDL_Texture* tex = nullptr;
        size_t bytes = 0;
        uint64_t lastUse = 0;
        bool failed = false;
    };
    void Want(const std::string& path);
    void WorkerLoop();

    SDL_Renderer* r_;
    size_t budget_;
    int maxSide_;
    size_t bytes_ = 0;
    uint64_t frame_ = 1;
    std::unordered_map<std::string, Entry> entries_;
    std::vector<std::string> wanted_;
    std::unordered_set<std::string> wantedSet_;

    std::mutex m_;
    std::condition_variable cv_;
    std::vector<std::string> queue_;                          // protégé par m_
    std::unordered_set<std::string> inflight_;                // protégé par m_
    std::vector<std::pair<std::string, SDL_Surface*>> done_;  // protégé par m_
    bool quit_ = false;
    std::thread worker_;
};
