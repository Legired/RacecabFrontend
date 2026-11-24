#pragma once
#include <SDL3/SDL.h>

#include <atomic>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "core/Util.h"

// Sons de l'interface (défilement, validation, retour, menu).
// Sons intégrés synthétisés au démarrage (aucun fichier ni licence) ; remplaçables par des fichiers
// <données>/sounds/move.wav, select.wav, back.wav, menu.wav (n'importe quel format WAV).
class UiSounds {
public:
    enum Kind { Move, Select, Back, Menu, Count };
    ~UiSounds();
    void Init(const fs::path& dir);
    void Play(Kind k);
    void SetVolume(float v01);  // 0..1 (courbe perceptive appliquée)
    bool Custom(Kind k) const { return custom_[k]; }

private:
    SDL_AudioStream* stream_ = nullptr;
    std::vector<float> pcm_[Count];  // 48 kHz, stéréo, float
    bool custom_[Count] = {};
    uint64_t lastMove_ = 0;
};

// Musique de fond : fichiers audio d'un dossier (mp3, ogg, flac, wav, m4a, opus), ordre aléatoire,
// décodés par FFmpeg dans un fil dédié. Fondu à l'arrêt / à la reprise.
class MusicPlayer {
public:
    ~MusicPlayer();
    void SetFolder(const fs::path& dir);  // relit la liste des fichiers
    size_t TrackCount() const;
    void SetVolume(float v01);            // 0..1 (courbe perceptive appliquée)
    void SetWanted(bool play);            // jouer (fondu d'entrée) ou se taire (fondu de sortie, puis pause)
    void Update(float dt);                // fondus ; à appeler à chaque image
    void Next();                          // morceau suivant
    std::string Current() const;          // nom du morceau en cours (vide si rien)
    void Stop();
    void Silence();                       // coupure immédiate, sans fondu (lancement d'un jeu)

private:
    void Start();
    void Thread();

    mutable std::mutex m_;
    std::vector<fs::path> tracks_;
    std::string current_;
    std::thread thread_;
    std::atomic<bool> abort_{false}, skip_{false};
    SDL_AudioStream* stream_ = nullptr;
    float volume_ = 0.5f, fade_ = 0.0f;
    bool wanted_ = false, paused_ = true;
    std::mt19937 rng_{std::random_device{}()};
};
