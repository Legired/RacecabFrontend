// Génère une vidéo de test MP4 (H.264 + son AAC) avec Media Foundation (encodeurs intégrés à Windows).
//   make_test_video.exe sortie.mp4 [secondes] [titre-couleur 0..5]
// Image : dégradé animé + barre qui défile ; son : bip de 440 Hz au début de chaque seconde.
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

#define CHECK(x)                                                         \
    do {                                                                 \
        HRESULT hr_ = (x);                                               \
        if (FAILED(hr_)) {                                               \
            std::printf("Echec 0x%08lx : %s\n", (unsigned long)hr_, #x); \
            return 1;                                                    \
        }                                                                \
    } while (0)

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::printf("usage : make_test_video sortie.mp4 [secondes] [couleur 0..5]\n");
        return 2;
    }
    const UINT32 W = 640, H = 360, FPS = 30, RATE = 48000;
    int seconds = argc > 2 ? _wtoi(argv[2]) : 6;
    int hue = argc > 3 ? _wtoi(argv[3]) : 0;
    const LONGLONG frameDur = 10'000'000 / FPS;

    CHECK(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    CHECK(MFStartup(MF_VERSION));
    IMFSinkWriter* writer = nullptr;
    CHECK(MFCreateSinkWriterFromURL(argv[1], nullptr, nullptr, &writer));

    // Vidéo : sortie H.264, entrée RGB32
    IMFMediaType *vout = nullptr, *vin = nullptr;
    MFCreateMediaType(&vout);
    vout->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    vout->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    vout->SetUINT32(MF_MT_AVG_BITRATE, 1500000);
    vout->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(vout, MF_MT_FRAME_SIZE, W, H);
    MFSetAttributeRatio(vout, MF_MT_FRAME_RATE, FPS, 1);
    MFSetAttributeRatio(vout, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    DWORD vs = 0;
    CHECK(writer->AddStream(vout, &vs));
    MFCreateMediaType(&vin);
    vin->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    vin->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    vin->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(vin, MF_MT_FRAME_SIZE, W, H);
    MFSetAttributeRatio(vin, MF_MT_FRAME_RATE, FPS, 1);
    MFSetAttributeRatio(vin, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    vin->SetUINT32(MF_MT_DEFAULT_STRIDE, W * 4);
    CHECK(writer->SetInputMediaType(vs, vin, nullptr));

    // Son : sortie AAC, entrée PCM 16 bits stéréo
    IMFMediaType *aout = nullptr, *ain = nullptr;
    MFCreateMediaType(&aout);
    aout->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    aout->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
    aout->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    aout->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, RATE);
    aout->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
    aout->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 16000);
    DWORD as = 0;
    CHECK(writer->AddStream(aout, &as));
    MFCreateMediaType(&ain);
    ain->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    ain->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    ain->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    ain->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, RATE);
    ain->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
    ain->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
    ain->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, RATE * 4);
    CHECK(writer->SetInputMediaType(as, ain, nullptr));

    CHECK(writer->BeginWriting());
    const BYTE base[6][3] = {{255, 140, 30}, {40, 160, 255}, {255, 60, 90}, {90, 230, 110}, {240, 220, 60}, {180, 110, 255}};
    const BYTE* col = base[((hue % 6) + 6) % 6];
    const UINT32 samplesPerFrame = RATE / FPS;
    for (UINT32 f = 0; f < (UINT32)seconds * FPS; ++f) {
        // Image (RGB32 de haut en bas : MF attend un stride positif)
        IMFMediaBuffer* buf = nullptr;
        MFCreateMemoryBuffer(W * H * 4, &buf);
        BYTE* p = nullptr;
        buf->Lock(&p, nullptr, nullptr);
        int barX = (int)((f * 8) % W);
        for (UINT32 y = 0; y < H; ++y)
            for (UINT32 x = 0; x < W; ++x) {
                float t = (float)y / H;
                BYTE* px = p + (y * W + x) * 4;
                bool bar = (int)x >= barX && (int)x < barX + 40;
                px[2] = bar ? 255 : (BYTE)(col[0] * (0.25f + 0.75f * t));
                px[1] = bar ? 255 : (BYTE)(col[1] * (0.25f + 0.75f * t));
                px[0] = bar ? 255 : (BYTE)(col[2] * (0.25f + 0.75f * t));
                px[3] = 255;
            }
        buf->Unlock();
        buf->SetCurrentLength(W * H * 4);
        IMFSample* s = nullptr;
        MFCreateSample(&s);
        s->AddBuffer(buf);
        s->SetSampleTime(f * frameDur);
        s->SetSampleDuration(frameDur);
        CHECK(writer->WriteSample(vs, s));
        s->Release();
        buf->Release();

        // Son : bip de 440 Hz pendant les 150 premières ms de chaque seconde
        IMFMediaBuffer* ab = nullptr;
        MFCreateMemoryBuffer(samplesPerFrame * 4, &ab);
        BYTE* ap = nullptr;
        ab->Lock(&ap, nullptr, nullptr);
        auto* pcm = reinterpret_cast<short*>(ap);
        for (UINT32 i = 0; i < samplesPerFrame; ++i) {
            UINT32 n = f * samplesPerFrame + i;
            bool beep = (n % RATE) < RATE * 15 / 100;
            short v = beep ? (short)(8000 * std::sin(2 * 3.14159265 * 440 * n / RATE)) : 0;
            pcm[i * 2] = pcm[i * 2 + 1] = v;
        }
        ab->Unlock();
        ab->SetCurrentLength(samplesPerFrame * 4);
        IMFSample* asmp = nullptr;
        MFCreateSample(&asmp);
        asmp->AddBuffer(ab);
        asmp->SetSampleTime(f * frameDur);
        asmp->SetSampleDuration(frameDur);
        CHECK(writer->WriteSample(as, asmp));
        asmp->Release();
        ab->Release();
    }
    CHECK(writer->Finalize());
    writer->Release();
    MFShutdown();
    CoUninitialize();
    std::printf("OK : %d s, %ux%u, H.264 + AAC\n", seconds, W, H);
    return 0;
}
