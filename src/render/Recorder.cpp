#include "render/Recorder.h"

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <cstring>

#include "core/Log.h"

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

namespace {
constexpr int kFps = 30;
constexpr LONGLONG kFrameDur = 10'000'000 / kFps;
}  // namespace

Recorder::~Recorder() { Stop(); }

bool Recorder::Start(const std::wstring& file, int w, int h, int bitrate, std::string& err) {
    w_ = w & ~1;  // H.264 : dimensions paires
    h_ = h & ~1;
    comInit_ = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    if (FAILED(MFStartup(MF_VERSION))) {
        err = "Media Foundation indisponible";
        return false;
    }
    if (FAILED(MFCreateSinkWriterFromURL(file.c_str(), nullptr, nullptr, &writer_))) {
        err = "création du fichier vidéo impossible";
        return false;
    }
    IMFMediaType *out = nullptr, *in = nullptr;
    MFCreateMediaType(&out);
    out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    out->SetUINT32(MF_MT_AVG_BITRATE, (UINT32)bitrate);
    out->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(out, MF_MT_FRAME_SIZE, w_, h_);
    MFSetAttributeRatio(out, MF_MT_FRAME_RATE, kFps, 1);
    MFSetAttributeRatio(out, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    HRESULT hr = writer_->AddStream(out, &stream_);
    MFCreateMediaType(&in);
    in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    in->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    in->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(in, MF_MT_FRAME_SIZE, w_, h_);
    MFSetAttributeRatio(in, MF_MT_FRAME_RATE, kFps, 1);
    MFSetAttributeRatio(in, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    in->SetUINT32(MF_MT_DEFAULT_STRIDE, w_ * 4);
    if (SUCCEEDED(hr)) hr = writer_->SetInputMediaType(stream_, in, nullptr);
    if (SUCCEEDED(hr)) hr = writer_->BeginWriting();
    out->Release();
    in->Release();
    if (FAILED(hr)) {
        err = "encodeur H.264 indisponible";
        writer_->Release();
        writer_ = nullptr;
        return false;
    }
    startMs_ = 0;
    frames_ = 0;
    logx::Info("Enregistrement vidéo démarré ({}x{})", w_, h_);
    return true;
}

void Recorder::Capture(SDL_Renderer* r, uint64_t nowMs) {
    if (!writer_) return;
    if (!startMs_) startMs_ = nowMs;
    // Nombre d'images que la vidéo devrait contenir à cet instant (cadence fixe, temps réel).
    int due = (int)((nowMs - startMs_) * kFps / 1000) + 1;
    if (due <= frames_) return;
    SDL_Surface* raw = SDL_RenderReadPixels(r, nullptr);
    if (!raw) return;
    SDL_Surface* s = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_XRGB8888);  // = RGB32 de Media Foundation (B, G, R, X)
    SDL_DestroySurface(raw);
    if (!s) return;
    IMFMediaBuffer* buf = nullptr;
    MFCreateMemoryBuffer(w_ * h_ * 4, &buf);
    BYTE* p = nullptr;
    buf->Lock(&p, nullptr, nullptr);
    for (int y = 0; y < h_; ++y) {
        if (y < s->h) std::memcpy(p + y * w_ * 4, (const BYTE*)s->pixels + y * s->pitch, (size_t)std::min(w_, s->w) * 4);
        else std::memset(p + y * w_ * 4, 0, (size_t)w_ * 4);
    }
    buf->Unlock();
    buf->SetCurrentLength(w_ * h_ * 4);
    SDL_DestroySurface(s);
    // Si des images manquent (frame en retard), la même image couvre plusieurs créneaux.
    IMFSample* smp = nullptr;
    MFCreateSample(&smp);
    smp->AddBuffer(buf);
    smp->SetSampleTime(frames_ * kFrameDur);
    smp->SetSampleDuration((due - frames_) * kFrameDur);
    writer_->WriteSample(stream_, smp);
    smp->Release();
    buf->Release();
    frames_ = due;
}

void Recorder::Stop() {
    if (!writer_) return;
    writer_->Finalize();
    writer_->Release();
    writer_ = nullptr;
    MFShutdown();
    if (comInit_) CoUninitialize();
    comInit_ = false;
    logx::Info("Enregistrement vidéo terminé ({} images, {:.1f} s)", frames_, frames_ / (double)kFps);
}
