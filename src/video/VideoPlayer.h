#pragma once
#include <SDL3/SDL.h>

#include <memory>
#include <set>
#include <string>
#include <vector>

struct AVBufferRef;  // FFmpeg

struct VideoOptions {
    bool loop = true;
    float volume = 0.6f;  // 0..1
    bool mute = false;
    bool hwDecode = true;  // D3D11VA si disponible, sinon décodage logiciel
};

// Lecteur vidéo léger basé sur FFmpeg.
// - Une seule vidéo décodée à la fois ; décodage et son dans un thread dédié.
// - Open()/Stop() ne bloquent jamais : l'ancienne session est arrêtée en arrière-plan puis libérée plus tard.
// - Frame() (thread principal) envoie au GPU l'image due et renvoie la texture à afficher.
class VideoPlayer {
public:
    explicit VideoPlayer(SDL_Renderer* r);
    ~VideoPlayer();

    void Open(const std::string& path, const VideoOptions& opt);
    void Stop();
    void SetVolume(float volume, bool mute);

    SDL_Texture* Frame();              // nullptr tant qu'aucune image n'est prête
    const std::string& Path() const { return path_; }
    bool Failed(const std::string& path) const { return failed_.count(path) > 0; }
    std::string LastError() const { return lastError_; }
    std::string DecoderInfo() const { return decoderInfo_; }  // ex. « h264 (D3D11VA) 1280x720 »

    struct Session;

private:
    void Reap(bool wait);

    SDL_Renderer* r_;
    std::unique_ptr<Session> cur_;
    AVBufferRef* hwDevice_ = nullptr;  // périphérique D3D11VA créé une seule fois
    bool hwDeviceFailed_ = false;
    std::vector<std::unique_ptr<Session>> zombies_;
    std::string path_;
    std::set<std::string> failed_;
    std::string lastError_, decoderInfo_;

    SDL_Texture* tex_ = nullptr;
    int texW_ = 0, texH_ = 0, texFmt_ = 0;
    bool hasFrame_ = false;
    double clockBase_ = 0;  // instant (s) correspondant à pts = 0
    int serial_ = -1;
};
