#include "video/VideoPlayer.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <atomic>
#include <condition_variable>
#include <deque>
#include <format>
#include <mutex>
#include <thread>

#include "core/Log.h"

namespace {
double NowSec() { return SDL_GetTicksNS() / 1e9; }

struct QueuedFrame {
    AVFrame* f;
    double pts;
    int serial;
};

enum PixKind { kNV12 = 1, kYUV420 = 2 };

AVPixelFormat GetHwFormat(AVCodecContext*, const AVPixelFormat* fmts) {
    for (const AVPixelFormat* p = fmts; *p != AV_PIX_FMT_NONE; ++p)
        if (*p == AV_PIX_FMT_D3D11) return *p;
    return fmts[0];  // le décodeur repasse en logiciel
}
}  // namespace

struct VideoPlayer::Session {
    std::string path;
    VideoOptions opt;
    std::atomic<bool> abort{false};
    std::atomic<bool> done{false};
    std::atomic<bool> failed{false};
    std::atomic<float> gain{1.0f};
    AVBufferRef* sharedHw = nullptr;  // périphérique D3D11VA commun à toutes les vidéos (référence propre)
    std::string error, info;  // écrits par le thread avant done=true
    std::mutex m;
    std::condition_variable cv;
    std::deque<QueuedFrame> queue;
    std::thread thread;

    ~Session() {
        abort = true;
        cv.notify_all();
        if (thread.joinable()) thread.join();
        for (auto& q : queue) av_frame_free(&q.f);
        av_buffer_unref(&sharedHw);
    }

    static int Interrupt(void* p) { return static_cast<Session*>(p)->abort.load() ? 1 : 0; }

    void Fail(const std::string& e) {
        error = e;
        failed = true;
    }

    void Run();
};

void VideoPlayer::Session::Run() {
    AVFormatContext* fmt = avformat_alloc_context();
    AVCodecContext *vctx = nullptr, *actx = nullptr;
    AVBufferRef* hwdev = nullptr;
    SwrContext* swr = nullptr;
    SwsContext* sws = nullptr;
    SDL_AudioStream* audio = nullptr;
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    int vs = -1, as = -1, serial = 0;
    double vtb = 0, lastPts = 0;
    std::vector<uint8_t> abuf;

    auto cleanup = [&] {
        if (audio) SDL_DestroyAudioStream(audio);
        sws_freeContext(sws);
        swr_free(&swr);
        avcodec_free_context(&vctx);
        avcodec_free_context(&actx);
        av_buffer_unref(&hwdev);
        avformat_close_input(&fmt);
        av_packet_free(&pkt);
        av_frame_free(&frame);
        done = true;
    };

    fmt->interrupt_callback = {&Session::Interrupt, this};
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) {  // fmt est libéré en cas d'échec
        Fail("fichier vidéo illisible");
        return cleanup();
    }
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        Fail("format vidéo non reconnu");
        return cleanup();
    }
    const AVCodec* vcodec = nullptr;
    vs = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &vcodec, 0);
    if (vs < 0 || !vcodec) {
        Fail("aucune piste vidéo décodable");
        return cleanup();
    }
    vtb = av_q2d(fmt->streams[vs]->time_base);
    vctx = avcodec_alloc_context3(vcodec);
    avcodec_parameters_to_context(vctx, fmt->streams[vs]->codecpar);

    bool hw = false;
    if (opt.hwDecode) {
        for (int i = 0;; ++i) {
            const AVCodecHWConfig* c = avcodec_get_hw_config(vcodec, i);
            if (!c) break;
            if (c->device_type == AV_HWDEVICE_TYPE_D3D11VA && (c->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)) {
                // Périphérique partagé : en créer un nouveau (D3D11) à chaque vidéo faisait grossir la mémoire
                // du pilote d'environ 200 Mo par minute en démonstration (test d'endurance).
                if (sharedHw) hwdev = av_buffer_ref(sharedHw);
                if (hwdev || av_hwdevice_ctx_create(&hwdev, AV_HWDEVICE_TYPE_D3D11VA, nullptr, nullptr, 0) == 0) {
                    vctx->hw_device_ctx = av_buffer_ref(hwdev);
                    vctx->get_format = GetHwFormat;
                    hw = true;
                }
                break;
            }
        }
    }
    if (!hw) vctx->thread_count = 2;
    if (avcodec_open2(vctx, vcodec, nullptr) < 0) {
        Fail(std::string("décodeur ") + vcodec->name + " indisponible");
        return cleanup();
    }
    {
        std::lock_guard lk(m);
        info = std::format("{} ({}) {}x{}", vcodec->name, hw ? "D3D11VA" : "logiciel", vctx->width, vctx->height);
    }
    logx::Info("Vidéo : {} - {}", info, path);

    // Son (facultatif)
    const AVCodec* acodec = nullptr;
    as = opt.mute ? -1 : av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, vs, &acodec, 0);
    if (as >= 0 && acodec) {
        actx = avcodec_alloc_context3(acodec);
        avcodec_parameters_to_context(actx, fmt->streams[as]->codecpar);
        AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        if (avcodec_open2(actx, acodec, nullptr) == 0 &&
            swr_alloc_set_opts2(&swr, &stereo, AV_SAMPLE_FMT_FLT, 48000, &actx->ch_layout, actx->sample_fmt,
                                actx->sample_rate, 0, nullptr) == 0 &&
            swr_init(swr) == 0) {
            SDL_AudioSpec spec{SDL_AUDIO_F32, 2, 48000};
            audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
            if (audio) {
                SDL_SetAudioStreamGain(audio, gain.load());
                SDL_ResumeAudioStreamDevice(audio);
            } else {
                logx::Warn("Son vidéo indisponible : {}", SDL_GetError());
            }
        }
        if (!audio) as = -1;
    }
    float appliedGain = gain.load();

    auto pushVideo = [&](AVFrame* src) {
        AVFrame* out = nullptr;
        AVFrame* sw = src;
        AVFrame* tmp = nullptr;
        if (src->format == AV_PIX_FMT_D3D11) {  // image en mémoire GPU : copie vers la RAM (NV12)
            tmp = av_frame_alloc();
            if (av_hwframe_transfer_data(tmp, src, 0) < 0) {
                av_frame_free(&tmp);
                return;
            }
            sw = tmp;
        }
        if (sw->format == AV_PIX_FMT_NV12 || sw->format == AV_PIX_FMT_YUV420P) {
            out = tmp ? tmp : av_frame_clone(sw);
            tmp = nullptr;
        } else {  // autres formats : conversion en YUV420P
            sws = sws_getCachedContext(sws, sw->width, sw->height, (AVPixelFormat)sw->format, sw->width, sw->height,
                                       AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr, nullptr);
            out = av_frame_alloc();
            out->format = AV_PIX_FMT_YUV420P;
            out->width = sw->width;
            out->height = sw->height;
            if (!sws || av_frame_get_buffer(out, 0) < 0) {
                av_frame_free(&out);
                av_frame_free(&tmp);
                return;
            }
            sws_scale(sws, sw->data, sw->linesize, 0, sw->height, out->data, out->linesize);
            av_frame_free(&tmp);
        }
        int64_t ts = src->best_effort_timestamp != AV_NOPTS_VALUE ? src->best_effort_timestamp : src->pts;
        double pts = ts != AV_NOPTS_VALUE ? ts * vtb : lastPts + 1.0 / 30;
        lastPts = pts;
        std::unique_lock lk(m);
        cv.wait(lk, [&] { return abort.load() || queue.size() < 4; });  // au plus 4 images d'avance
        if (abort) {
            av_frame_free(&out);
            return;
        }
        queue.push_back({out, pts, serial});
    };

    auto pushAudio = [&](AVFrame* f) {
        int outSamples = swr_get_out_samples(swr, f->nb_samples);
        if (outSamples <= 0) return;
        abuf.resize((size_t)outSamples * 2 * sizeof(float));
        uint8_t* outp = abuf.data();
        int n = swr_convert(swr, &outp, outSamples, (const uint8_t**)f->extended_data, f->nb_samples);
        if (n > 0) SDL_PutAudioStreamData(audio, abuf.data(), n * 2 * (int)sizeof(float));
    };

    auto decode = [&](AVCodecContext* ctx, AVPacket* p, bool isVideo) {
        if (avcodec_send_packet(ctx, p) < 0) return;
        while (!abort && avcodec_receive_frame(ctx, frame) == 0) {
            if (isVideo) pushVideo(frame);
            else pushAudio(frame);
            av_frame_unref(frame);
        }
    };

    while (!abort) {
        float g = gain.load();
        if (audio && g != appliedGain) {
            SDL_SetAudioStreamGain(audio, g);
            appliedGain = g;
        }
        // Le son ne doit pas prendre plus d'une seconde d'avance (la vidéo est cadencée par la file).
        while (audio && !abort && SDL_GetAudioStreamQueued(audio) > 48000 * 2 * 4) SDL_Delay(10);

        int rc = av_read_frame(fmt, pkt);
        if (rc < 0) {
            decode(vctx, nullptr, true);  // vide le décodeur
            if (!opt.loop || abort) break;
            av_seek_frame(fmt, -1, 0, AVSEEK_FLAG_BACKWARD);
            avcodec_flush_buffers(vctx);
            if (actx) avcodec_flush_buffers(actx);
            ++serial;  // nouvelle boucle : l'horloge d'affichage repart
            continue;
        }
        if (pkt->stream_index == vs) decode(vctx, pkt, true);
        else if (pkt->stream_index == as && actx) decode(actx, pkt, false);
        av_packet_unref(pkt);
    }
    // Fin sans boucle : on laisse la dernière image affichée ; le son restant est abandonné à l'arrêt.
    cleanup();
}

VideoPlayer::VideoPlayer(SDL_Renderer* r) : r_(r) {}

VideoPlayer::~VideoPlayer() {
    Stop();
    Reap(true);
    if (tex_) SDL_DestroyTexture(tex_);
    av_buffer_unref(&hwDevice_);
}

void VideoPlayer::Reap(bool wait) {
    for (auto it = zombies_.begin(); it != zombies_.end();) {
        if (wait || (*it)->done) it = zombies_.erase(it);  // le destructeur rejoint le thread (immédiat si done)
        else ++it;
    }
}

void VideoPlayer::Stop() {
    if (cur_) {
        cur_->abort = true;
        cur_->cv.notify_all();
        zombies_.push_back(std::move(cur_));
    }
    path_.clear();
    hasFrame_ = false;
    serial_ = -1;
}

void VideoPlayer::Open(const std::string& path, const VideoOptions& opt) {
    Stop();
    if (path.empty() || failed_.count(path)) return;
    path_ = path;
    cur_ = std::make_unique<Session>();
    cur_->path = path;
    cur_->opt = opt;
    cur_->gain = opt.mute ? 0.0f : opt.volume;
    if (opt.hwDecode && !hwDevice_ && !hwDeviceFailed_) {
        hwDeviceFailed_ = av_hwdevice_ctx_create(&hwDevice_, AV_HWDEVICE_TYPE_D3D11VA, nullptr, nullptr, 0) != 0;
        if (hwDeviceFailed_) logx::Warn("Vidéo : décodage matériel D3D11VA indisponible, décodage logiciel");
    }
    if (opt.hwDecode && hwDevice_) cur_->sharedHw = av_buffer_ref(hwDevice_);
    Session* s = cur_.get();
    s->thread = std::thread([s] { s->Run(); });
}

void VideoPlayer::SetVolume(float volume, bool mute) {
    if (cur_) cur_->gain = mute ? 0.0f : volume;
}

SDL_Texture* VideoPlayer::Frame() {
    Reap(false);
    if (!cur_) return hasFrame_ ? tex_ : nullptr;
    if (cur_->done && cur_->failed) {
        lastError_ = cur_->error + " : " + path_;
        logx::Warn("Vidéo : {}", lastError_);
        failed_.insert(path_);
        Stop();
        return nullptr;
    }
    // Sélection de l'image due selon l'horloge ; les images en retard sont abandonnées.
    AVFrame* show = nullptr;
    {
        std::lock_guard lk(cur_->m);
        if (!cur_->info.empty()) decoderInfo_ = cur_->info;
        auto& q = cur_->queue;
        const double now = NowSec();
        while (!q.empty()) {
            QueuedFrame& f = q.front();
            if (f.serial != serial_) {  // première image ou nouvelle boucle
                serial_ = f.serial;
                clockBase_ = now - f.pts;
            }
            if (f.pts > now - clockBase_) break;
            if (show) av_frame_free(&show);
            show = f.f;
            q.pop_front();
        }
    }
    cur_->cv.notify_all();
    if (!show) return hasFrame_ ? tex_ : nullptr;

    int kind = show->format == AV_PIX_FMT_NV12 ? kNV12 : kYUV420;
    if (!tex_ || texW_ != show->width || texH_ != show->height || texFmt_ != kind) {
        if (tex_) SDL_DestroyTexture(tex_);
        tex_ = SDL_CreateTexture(r_, kind == kNV12 ? SDL_PIXELFORMAT_NV12 : SDL_PIXELFORMAT_IYUV,
                                 SDL_TEXTUREACCESS_STREAMING, show->width, show->height);
        texW_ = show->width;
        texH_ = show->height;
        texFmt_ = kind;
        if (tex_) {
            SDL_SetTextureScaleMode(tex_, SDL_SCALEMODE_LINEAR);
            SDL_SetTextureBlendMode(tex_, SDL_BLENDMODE_BLEND);
        }
    }
    if (tex_) {
        if (kind == kNV12)
            SDL_UpdateNVTexture(tex_, nullptr, show->data[0], show->linesize[0], show->data[1], show->linesize[1]);
        else
            SDL_UpdateYUVTexture(tex_, nullptr, show->data[0], show->linesize[0], show->data[1], show->linesize[1],
                                 show->data[2], show->linesize[2]);
        hasFrame_ = true;
    }
    av_frame_free(&show);
    return hasFrame_ ? tex_ : nullptr;
}
