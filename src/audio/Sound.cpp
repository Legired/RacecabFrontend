#include "audio/Sound.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <cmath>

#include "core/Log.h"

namespace {
constexpr int kRate = 48000;
constexpr float kPi = 3.14159265f;
const SDL_AudioSpec kSpec{SDL_AUDIO_F32, 2, kRate};

float Perceived(float v) {
    v = std::clamp(v, 0.0f, 1.0f);
    return v * v * v;
}

// Note : sinusoïde (avec un peu d'harmonique) à enveloppe attaque rapide / décroissance exponentielle,
// fréquence glissant de f0 à f1. Ajoutée (mono) au tampon à partir de startSec.
void Tone(std::vector<float>& mono, float startSec, float durSec, float f0, float f1, float amp, float decay) {
    const size_t a = (size_t)(startSec * kRate), n = (size_t)(durSec * kRate);
    if (mono.size() < a + n) mono.resize(a + n, 0.0f);
    float phase = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        const float t = (float)i / kRate, k = (float)i / (float)n;
        const float f = f0 + (f1 - f0) * k;
        phase += 2.0f * kPi * f / kRate;
        const float env = std::min(1.0f, t / 0.003f) * std::exp(-decay * t) * (1.0f - k * k);
        mono[a + i] += amp * env * (std::sin(phase) + 0.25f * std::sin(2.0f * phase));
    }
}

std::vector<float> Stereo(const std::vector<float>& mono) {
    std::vector<float> out(mono.size() * 2);
    for (size_t i = 0; i < mono.size(); ++i) out[2 * i] = out[2 * i + 1] = std::clamp(mono[i], -1.0f, 1.0f);
    return out;
}

std::vector<float> Builtin(UiSounds::Kind k) {
    std::vector<float> m;
    switch (k) {
        case UiSounds::Move: Tone(m, 0.0f, 0.035f, 1500, 1300, 0.30f, 90.0f); break;  // tic bref
        case UiSounds::Select:  // deux notes montantes
            Tone(m, 0.0f, 0.07f, 660, 660, 0.35f, 25.0f);
            Tone(m, 0.06f, 0.14f, 990, 990, 0.35f, 18.0f);
            break;
        case UiSounds::Back: Tone(m, 0.0f, 0.12f, 700, 420, 0.35f, 20.0f); break;  // glissé descendant
        case UiSounds::Menu:  // accord doux
            Tone(m, 0.0f, 0.18f, 523, 523, 0.22f, 14.0f);
            Tone(m, 0.0f, 0.18f, 784, 784, 0.18f, 14.0f);
            break;
        default: break;
    }
    return Stereo(m);
}

const char* kFiles[UiSounds::Count] = {"move.wav", "select.wav", "back.wav", "menu.wav"};
}  // namespace

// ---------------------------------------------------------------- Sons de l'interface

UiSounds::~UiSounds() {
    if (stream_) SDL_DestroyAudioStream(stream_);
}

void UiSounds::Init(const fs::path& dir) {
    for (int k = 0; k < Count; ++k) {
        custom_[k] = false;
        pcm_[k] = Builtin((Kind)k);
        const fs::path f = dir / kFiles[k];
        std::error_code ec;
        if (!fs::exists(f, ec)) continue;
        SDL_AudioSpec spec{};
        Uint8* buf = nullptr;
        Uint32 len = 0;
        if (!SDL_LoadWAV(util::PathU8(f).c_str(), &spec, &buf, &len)) {
            logx::Warn("Son {} illisible : {}", util::PathU8(f), SDL_GetError());
            continue;
        }
        Uint8* out = nullptr;
        int outLen = 0;
        if (SDL_ConvertAudioSamples(&spec, buf, (int)len, &kSpec, &out, &outLen) && outLen > 0) {
            pcm_[k].assign((float*)out, (float*)out + outLen / sizeof(float));
            custom_[k] = true;
        }
        SDL_free(out);
        SDL_free(buf);
    }
    if (!stream_) {
        stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &kSpec, nullptr, nullptr);
        if (stream_) SDL_ResumeAudioStreamDevice(stream_);
        else logx::Warn("Sons de l'interface indisponibles : {}", SDL_GetError());
    }
}

void UiSounds::SetVolume(float v01) {
    if (stream_) SDL_SetAudioStreamGain(stream_, Perceived(v01));
}

void UiSounds::Play(Kind k) {
    if (!stream_ || k < 0 || k >= Count || pcm_[k].empty()) return;
    const uint64_t now = SDL_GetTicks();
    if (k == Move) {  // défilement rapide : un tic au plus toutes les 40 ms, sans accumulation
        if (now - lastMove_ < 40) return;
        lastMove_ = now;
    }
    SDL_ClearAudioStream(stream_);  // un nouveau son remplace le précédent
    SDL_PutAudioStreamData(stream_, pcm_[k].data(), (int)(pcm_[k].size() * sizeof(float)));
}

// ---------------------------------------------------------------- Musique de fond

MusicPlayer::~MusicPlayer() { Stop(); }

void MusicPlayer::Stop() {
    abort_ = true;
    if (thread_.joinable()) thread_.join();
    abort_ = false;
    if (stream_) {
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
    }
    std::lock_guard lk(m_);
    current_.clear();
    paused_ = true;
    fade_ = 0.0f;
}

void MusicPlayer::SetFolder(const fs::path& dir) {
    Stop();
    std::vector<fs::path> list;
    std::error_code ec;
    if (fs::is_directory(dir, ec))
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            if (!e.is_regular_file(ec)) continue;
            std::string ext = util::ToLower(util::PathU8(e.path().extension()));
            if (ext == ".mp3" || ext == ".ogg" || ext == ".flac" || ext == ".wav" || ext == ".m4a" || ext == ".opus")
                list.push_back(e.path());
        }
    {
        std::lock_guard lk(m_);
        tracks_ = list;
    }
    if (!list.empty()) {
        logx::Info("Musique : {} morceau(x) dans {}", list.size(), util::PathU8(dir));
        Start();
    }
}

size_t MusicPlayer::TrackCount() const {
    std::lock_guard lk(m_);
    return tracks_.size();
}

std::string MusicPlayer::Current() const {
    std::lock_guard lk(m_);
    return current_;
}

void MusicPlayer::Start() {
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &kSpec, nullptr, nullptr);
    if (!stream_) {
        logx::Warn("Musique indisponible : {}", SDL_GetError());
        return;
    }
    SDL_SetAudioStreamGain(stream_, 0.0f);
    paused_ = true;  // flux en pause jusqu'à SetWanted(true)
    thread_ = std::thread([this] { Thread(); });
}

void MusicPlayer::Silence() {
    wanted_ = false;
    fade_ = 0.0f;
    if (stream_ && !paused_) {
        SDL_SetAudioStreamGain(stream_, 0.0f);
        SDL_PauseAudioStreamDevice(stream_);
    }
    paused_ = true;
}

void MusicPlayer::SetVolume(float v01) { volume_ = std::clamp(v01, 0.0f, 1.0f); }
void MusicPlayer::SetWanted(bool play) { wanted_ = play; }

void MusicPlayer::Next() {
    skip_ = true;
    if (stream_) SDL_ClearAudioStream(stream_);
}

void MusicPlayer::Update(float dt) {
    if (!stream_) return;
    const bool on = wanted_ && volume_ > 0.0f;
    if (on && paused_) {
        SDL_ResumeAudioStreamDevice(stream_);
        paused_ = false;
    }
    // Fondu : 1,5 s pour monter, 0,6 s pour descendre
    fade_ = on ? std::min(1.0f, fade_ + dt / 1.5f) : std::max(0.0f, fade_ - dt / 0.6f);
    SDL_SetAudioStreamGain(stream_, Perceived(volume_) * fade_);
    if (!on && fade_ <= 0.0f && !paused_) {
        SDL_PauseAudioStreamDevice(stream_);
        paused_ = true;
    }
}

void MusicPlayer::Thread() {
    size_t last = (size_t)-1;
    int failures = 0;
    while (!abort_) {
        fs::path path;
        {
            std::lock_guard lk(m_);
            if (tracks_.empty()) return;
            size_t i = std::uniform_int_distribution<size_t>(0, tracks_.size() - 1)(rng_);
            if (tracks_.size() > 1 && i == last) i = (i + 1) % tracks_.size();  // pas deux fois le même
            last = i;
            path = tracks_[i];
            current_ = util::PathU8(path.stem());
        }
        skip_ = false;
        bool played = false;
        AVFormatContext* fmt = nullptr;
        AVCodecContext* ctx = nullptr;
        SwrContext* swr = nullptr;
        AVPacket* pkt = av_packet_alloc();
        AVFrame* frame = av_frame_alloc();
        const AVCodec* codec = nullptr;
        int as = -1;
        if (avformat_open_input(&fmt, util::PathU8(path).c_str(), nullptr, nullptr) == 0 &&
            avformat_find_stream_info(fmt, nullptr) >= 0 &&
            (as = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0)) >= 0 && codec) {
            ctx = avcodec_alloc_context3(codec);
            avcodec_parameters_to_context(ctx, fmt->streams[as]->codecpar);
            AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
            if (avcodec_open2(ctx, codec, nullptr) == 0 &&
                swr_alloc_set_opts2(&swr, &stereo, AV_SAMPLE_FMT_FLT, kRate, &ctx->ch_layout, ctx->sample_fmt,
                                    ctx->sample_rate, 0, nullptr) == 0 &&
                swr_init(swr) == 0) {
                std::vector<uint8_t> buf;
                auto push = [&] {
                    while (avcodec_receive_frame(ctx, frame) == 0) {
                        int outSamples = swr_get_out_samples(swr, frame->nb_samples);
                        if (outSamples > 0) {
                            buf.resize((size_t)outSamples * 2 * sizeof(float));
                            uint8_t* outp = buf.data();
                            int n = swr_convert(swr, &outp, outSamples, (const uint8_t**)frame->extended_data, frame->nb_samples);
                            if (n > 0) {
                                SDL_PutAudioStreamData(stream_, buf.data(), n * 2 * (int)sizeof(float));
                                played = true;
                            }
                        }
                        av_frame_unref(frame);
                    }
                };
                while (!abort_ && !skip_) {
                    // Au plus une seconde d'avance : la pause de la musique ne retient pas trop de données.
                    while (!abort_ && !skip_ && SDL_GetAudioStreamQueued(stream_) > kRate * 2 * 4) SDL_Delay(20);
                    if (av_read_frame(fmt, pkt) < 0) break;
                    if (pkt->stream_index == as && avcodec_send_packet(ctx, pkt) == 0) push();
                    av_packet_unref(pkt);
                }
                avcodec_send_packet(ctx, nullptr);
                push();
            }
        } else {
            logx::Warn("Musique illisible : {}", util::PathU8(path));
        }
        swr_free(&swr);
        avcodec_free_context(&ctx);
        avformat_close_input(&fmt);
        av_frame_free(&frame);
        av_packet_free(&pkt);
        // Fin du morceau : attendre que la fin soit jouée avant d'enchaîner (sauf saut demandé).
        while (!abort_ && !skip_ && SDL_GetAudioStreamQueued(stream_) > kRate * 2 * 4 / 10) SDL_Delay(30);
        failures = played ? 0 : failures + 1;
        if (failures >= 3) {  // fichiers tous illisibles : on ne boucle pas à vide
            for (int i = 0; i < 100 && !abort_; ++i) SDL_Delay(50);
            failures = 0;
        }
    }
}
