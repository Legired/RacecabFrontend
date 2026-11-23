#pragma once
#include <SDL3/SDL.h>

#include <string>

struct IMFSinkWriter;

// Enregistrement de l'écran du frontend en MP4 (H.264, encodeur Media Foundation intégré à Windows).
// Sert aux démonstrations (--record fichier.mp4) : cadence fixe de 30 images/s, images répétées si une frame est en retard.
class Recorder {
public:
    ~Recorder();
    bool Start(const std::wstring& file, int w, int h, int bitrate, std::string& err);
    void Capture(SDL_Renderer* r, uint64_t nowMs);  // à appeler juste avant SDL_RenderPresent
    void Stop();
    bool Active() const { return writer_ != nullptr; }
    int Frames() const { return frames_; }

private:
    IMFSinkWriter* writer_ = nullptr;
    unsigned long stream_ = 0;
    int w_ = 0, h_ = 0, frames_ = 0;
    uint64_t startMs_ = 0;
    bool comInit_ = false;
};
