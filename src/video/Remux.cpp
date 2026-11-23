#include "video/Remux.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>
}

#include <algorithm>
#include <format>

namespace {

std::string AvErr(int e) {
    char buf[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(e, buf, sizeof(buf));
    return buf;
}

struct Input {
    AVFormatContext* ctx = nullptr;
    int stream = -1;       // flux gardé
    int outStream = -1;    // indice dans la sortie
    ~Input() { avformat_close_input(&ctx); }
    bool Open(const fs::path& p, AVMediaType type, std::string& err) {
        int r = avformat_open_input(&ctx, util::PathU8(p).c_str(), nullptr, nullptr);
        if (r < 0) {
            err = std::format("ouverture de {} impossible ({})", util::PathU8(p.filename()), AvErr(r));
            return false;
        }
        if ((r = avformat_find_stream_info(ctx, nullptr)) < 0) {
            err = std::format("{} illisible ({})", util::PathU8(p.filename()), AvErr(r));
            return false;
        }
        stream = av_find_best_stream(ctx, type, -1, -1, nullptr, 0);
        return true;
    }
    double Duration() const { return ctx && ctx->duration > 0 ? ctx->duration / (double)AV_TIME_BASE : 0; }
};

}  // namespace

double MediaDuration(const fs::path& file) {
    Input in;
    std::string err;
    return in.Open(file, AVMEDIA_TYPE_VIDEO, err) ? in.Duration() : 0;
}

bool RemuxClip(const fs::path& videoIn, const fs::path& audioIn, const fs::path& out, double start, double duration,
               std::string& err) {
    Input v, a;
    if (!v.Open(videoIn, AVMEDIA_TYPE_VIDEO, err)) return false;
    if (v.stream < 0) {
        err = "aucune piste vidéo";
        return false;
    }
    Input* audio = nullptr;
    if (!audioIn.empty()) {
        std::string aerr;
        if (a.Open(audioIn, AVMEDIA_TYPE_AUDIO, aerr) && a.stream >= 0) audio = &a;
    }
    const int sameFileAudio = audioIn.empty() ? av_find_best_stream(v.ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0) : -1;

    // Le début demandé est toujours respecté : si la durée dépasse la fin, l'extrait s'arrête à la fin de la vidéo.
    // Seul un début situé après la fin est ramené en arrière (sinon il n'y aurait rien à extraire).
    const double total = v.Duration();
    start = std::max(0.0, start);
    if (total > 0) {
        if (start > total - 0.5) start = std::max(0.0, total - duration);
        if (start + duration > total) duration = total - start;
    }

    const std::string outU8 = util::PathU8(out);
    AVFormatContext* oc = nullptr;  // nom de sortie requis : +faststart rouvre le fichier pour déplacer l'index
    int r = avformat_alloc_output_context2(&oc, nullptr, "mp4", outU8.c_str());
    if (r < 0 || !oc) {
        err = "muxeur MP4 indisponible";
        return false;
    }
    auto addStream = [&](AVFormatContext* ic, int idx) -> int {
        AVStream* is = ic->streams[idx];
        AVStream* os = avformat_new_stream(oc, nullptr);
        if (!os || avcodec_parameters_copy(os->codecpar, is->codecpar) < 0) return -1;
        os->codecpar->codec_tag = 0;
        os->time_base = is->time_base;
        return os->index;
    };
    v.outStream = addStream(v.ctx, v.stream);
    int aOut = -1;
    if (audio) aOut = audio->outStream = addStream(audio->ctx, audio->stream);
    else if (sameFileAudio >= 0) aOut = addStream(v.ctx, sameFileAudio);
    if (v.outStream < 0) {
        err = "copie de la piste vidéo impossible";
        avformat_free_context(oc);
        return false;
    }

    if ((r = avio_open(&oc->pb, outU8.c_str(), AVIO_FLAG_WRITE)) < 0) {
        err = std::format("écriture impossible ({})", AvErr(r));
        avformat_free_context(oc);
        return false;
    }
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "movflags", "+faststart", 0);
    // Coupe exacte sans réencodage : les images entre l'image clé précédente et le début ont des horodatages
    // négatifs, et une « edit list » MP4 indique au lecteur de commencer exactement au début demandé.
    av_dict_set(&opts, "use_editlist", "1", 0);
    oc->avoid_negative_ts = AVFMT_AVOID_NEG_TS_DISABLED;
    r = avformat_write_header(oc, &opts);
    av_dict_free(&opts);
    if (r < 0) {
        err = std::format("en-tête MP4 refusé ({})", AvErr(r));
        avio_closep(&oc->pb);
        avformat_free_context(oc);
        return false;
    }

    // Vidéo : recherche de l'image clé avant start ; t0 = horodatage de la première image gardée.
    const int64_t startTs = (int64_t)(start * AV_TIME_BASE);
    av_seek_frame(v.ctx, -1, startTs, AVSEEK_FLAG_BACKWARD);
    if (audio) av_seek_frame(audio->ctx, -1, startTs, AVSEEK_FLAG_BACKWARD);

    AVPacket* pkt = av_packet_alloc();
    double t0 = -1;    // en secondes : PTS de la première image gardée (image clé au plus tard au début demandé)
    const double base = start;  // origine des horodatages de sortie : le début demandé
    std::string writeErr;
    size_t written = 0;
    auto write = [&](AVFormatContext* ic, AVPacket* p, int outIdx) {
        AVStream* is = ic->streams[p->stream_index];
        AVStream* os = oc->streams[outIdx];
        // Décalage : le début demandé devient l'instant 0 (images d'amorce avant lui : horodatages négatifs).
        const int64_t off = av_rescale_q((int64_t)(base * AV_TIME_BASE), AV_TIME_BASE_Q, is->time_base);
        if (p->pts != AV_NOPTS_VALUE) p->pts -= off;
        if (p->dts != AV_NOPTS_VALUE) p->dts -= off;
        av_packet_rescale_ts(p, is->time_base, os->time_base);
        p->stream_index = outIdx;
        p->pos = -1;
        int wr = av_interleaved_write_frame(oc, p);
        if (wr >= 0) ++written;
        else if (writeErr.empty()) writeErr = AvErr(wr);
    };
    auto secs = [](AVFormatContext* ic, const AVPacket* p) {
        int64_t ts = p->pts != AV_NOPTS_VALUE ? p->pts : p->dts;
        return ts == AV_NOPTS_VALUE ? -1.0 : ts * av_q2d(ic->streams[p->stream_index]->time_base);
    };

    // Passe 1 : vidéo (et son du même fichier).
    while (av_read_frame(v.ctx, pkt) >= 0) {
        const bool isV = pkt->stream_index == v.stream, isA = pkt->stream_index == sameFileAudio;
        if (!isV && !isA) {
            av_packet_unref(pkt);
            continue;
        }
        double t = secs(v.ctx, pkt);
        if (isV && t0 < 0) {
            if (!(pkt->flags & AV_PKT_FLAG_KEY) || t < 0) {
                av_packet_unref(pkt);
                continue;
            }
            t0 = t;
        }
        // Vidéo : toutes les images depuis l'image clé (nécessaires au décodage) ; son : à partir du début exact.
        if (t0 < 0 || t < (isV ? t0 : start)) {
            av_packet_unref(pkt);
            continue;
        }
        if (t >= start + duration) {
            av_packet_unref(pkt);
            if (isV) break;
            continue;
        }
        write(v.ctx, pkt, isV ? v.outStream : aOut);
        av_packet_unref(pkt);
    }
    // Passe 2 : son d'un fichier séparé, sur la même fenêtre de temps.
    if (audio && t0 >= 0) {
        while (av_read_frame(audio->ctx, pkt) >= 0) {
            if (pkt->stream_index != audio->stream) {
                av_packet_unref(pkt);
                continue;
            }
            double t = secs(audio->ctx, pkt);
            if (t >= start + duration) {
                av_packet_unref(pkt);
                break;
            }
            if (t >= start) write(audio->ctx, pkt, aOut);
            av_packet_unref(pkt);
        }
    }
    av_packet_free(&pkt);
    r = av_write_trailer(oc);
    avio_closep(&oc->pb);
    avformat_free_context(oc);
    if (t0 < 0 || written == 0) {
        err = "aucune image extraite";
        return false;
    }
    if (r < 0) {
        err = std::format("finalisation MP4 impossible ({}{})", AvErr(r), writeErr.empty() ? "" : ", écriture : " + writeErr);
        return false;
    }
    return true;
}
