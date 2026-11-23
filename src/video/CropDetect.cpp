#include "video/CropDetect.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
}

#include <algorithm>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>

#include "core/Log.h"

namespace {

// Contenu d'une image : premières / dernières lignes et colonnes qui ne sont pas noires (luminance).
struct Box {
    int top, bottom, left, right;  // inclus
};

bool ContentBox(const AVFrame* f, Box& b) {
    const AVPixFmtDescriptor* d = av_pix_fmt_desc_get((AVPixelFormat)f->format);
    // Formats YUV 8 bits uniquement (cas de toutes les vidéos YouTube / H.264 courantes) : plan 0 = luminance.
    if (!d || (d->flags & AV_PIX_FMT_FLAG_RGB) || d->comp[0].depth != 8 || d->nb_components < 3) return false;
    const int W = f->width, H = f->height, ls = f->linesize[0];
    const uint8_t* y = f->data[0];
    if (W < 32 || H < 32 || !y) return false;
    const int kBright = 40;  // au-dessus : pixel « non noir » (noir vidéo ≈ 16)
    auto rowHas = [&](int r) {
        int n = 0, tot = 0;
        for (int x = 0; x < W; x += 2, ++tot) n += y[r * ls + x] > kBright;
        return n * 100 > tot;  // plus de 1 % de pixels clairs
    };
    auto colHas = [&](int c) {
        int n = 0, tot = 0;
        for (int r = 0; r < H; r += 2, ++tot) n += y[r * ls + c] > kBright;
        return n * 100 > tot;
    };
    b.top = 0;
    while (b.top < H && !rowHas(b.top)) ++b.top;
    if (b.top >= H) return false;  // image entièrement sombre : ignorée
    b.bottom = H - 1;
    while (b.bottom > b.top && !rowHas(b.bottom)) --b.bottom;
    b.left = 0;
    while (b.left < W && !colHas(b.left)) ++b.left;
    b.right = W - 1;
    while (b.right > b.left && !colHas(b.right)) --b.right;
    return true;
}

}  // namespace

bool DetectCrop(const fs::path& file, CropRect& out, std::string& err) {
    out = {};
    AVFormatContext* fc = nullptr;
    if (avformat_open_input(&fc, util::PathU8(file).c_str(), nullptr, nullptr) < 0) {
        err = "ouverture impossible";
        return false;
    }
    struct Closer {
        AVFormatContext*& fc;
        AVCodecContext* cc = nullptr;
        AVFrame* fr = nullptr;
        AVPacket* pk = nullptr;
        ~Closer() {
            av_packet_free(&pk);
            av_frame_free(&fr);
            avcodec_free_context(&cc);
            avformat_close_input(&fc);
        }
    } c{fc};
    if (avformat_find_stream_info(fc, nullptr) < 0) {
        err = "fichier illisible";
        return false;
    }
    const AVCodec* codec = nullptr;
    int si = av_find_best_stream(fc, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (si < 0 || !codec) {
        err = "aucune piste vidéo";
        return false;
    }
    AVStream* st = fc->streams[si];
    c.cc = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(c.cc, st->codecpar);
    c.cc->thread_count = 2;
    if (avcodec_open2(c.cc, codec, nullptr) < 0) {
        err = "décodeur indisponible";
        return false;
    }
    c.fr = av_frame_alloc();
    c.pk = av_packet_alloc();

    const double dur = fc->duration > 0 ? fc->duration / (double)AV_TIME_BASE : 0;
    Box all{1 << 30, -1, 1 << 30, -1};
    int W = 0, H = 0, used = 0;
    for (double frac : {0.12, 0.32, 0.5, 0.68, 0.88}) {
        const double t = dur * frac;
        const int64_t ts = (int64_t)(t / av_q2d(st->time_base)) + (st->start_time != AV_NOPTS_VALUE ? st->start_time : 0);
        if (dur > 0) av_seek_frame(fc, si, ts, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(c.cc);
        bool got = false;
        for (int guard = 0; guard < 600 && !got; ++guard) {
            if (av_read_frame(fc, c.pk) < 0) break;
            if (c.pk->stream_index == si && avcodec_send_packet(c.cc, c.pk) >= 0) {
                while (avcodec_receive_frame(c.cc, c.fr) >= 0) {
                    const int64_t pts = c.fr->best_effort_timestamp;
                    if (dur > 0 && pts != AV_NOPTS_VALUE && pts < ts) continue;  // jusqu'à l'instant visé
                    Box b;
                    if (ContentBox(c.fr, b)) {
                        all.top = std::min(all.top, b.top);
                        all.bottom = std::max(all.bottom, b.bottom);
                        all.left = std::min(all.left, b.left);
                        all.right = std::max(all.right, b.right);
                        W = c.fr->width;
                        H = c.fr->height;
                        ++used;
                    }
                    got = true;
                    break;
                }
            }
            av_packet_unref(c.pk);
        }
        if (dur <= 0) break;  // durée inconnue : une seule image
    }
    if (!used || W <= 0 || H <= 0) {
        err = "aucune image exploitable";
        return true;  // pas d'erreur bloquante : pas de recadrage
    }
    // Bande retirée seulement si elle dépasse 2 % ; 2 pixels de marge pour les bords adoucis.
    int top = all.top >= H * 0.02 ? all.top + 2 : 0, bottom = (H - 1 - all.bottom) >= H * 0.02 ? all.bottom - 2 : H - 1;
    int left = all.left >= W * 0.02 ? all.left + 2 : 0, right = (W - 1 - all.right) >= W * 0.02 ? all.right - 2 : W - 1;
    if (right - left + 1 < W * 0.4 || bottom - top + 1 < H * 0.4) return true;  // trop petit : on ne touche à rien
    out = {left / (float)W, top / (float)H, (right - left + 1) / (float)W, (bottom - top + 1) / (float)H};
    return true;
}

// ---------------------------------------------------------------- Cache

CropCache::~CropCache() {
    {
        std::lock_guard lk(m_);
        quit_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void CropCache::Init(const fs::path& file) {
    file_ = file;
    try {
        std::ifstream f(file, std::ios::binary);
        if (f) {
            auto j = nlohmann::json::parse(f, nullptr, true, true);
            for (auto& [k, v] : j.items())
                if (v.is_array() && v.size() == 4)
                    known_[k] = {v[0].get<float>(), v[1].get<float>(), v[2].get<float>(), v[3].get<float>()};
        }
    } catch (const std::exception& e) {
        logx::Warn("videocrop.json ignoré : {}", e.what());
    }
    worker_ = std::thread([this] { Loop(); });
}

std::string CropCache::Key(const std::string& videoPath) {
    std::error_code ec;
    fs::path p = util::U8Path(videoPath);
    auto size = fs::file_size(p, ec);
    if (ec) return {};
    auto t = fs::last_write_time(p, ec).time_since_epoch().count();
    return std::format("{}|{}|{}", videoPath, size, t);
}

bool CropCache::Get(const std::string& videoPath, CropRect& out) {
    if (videoPath.empty()) return false;
    std::lock_guard lk(m_);
    auto k = keyOf_.find(videoPath);
    if (k == keyOf_.end()) {
        std::string key = Key(videoPath);
        if (key.empty()) return false;
        k = keyOf_.emplace(videoPath, key).first;
    }
    auto it = known_.find(k->second);
    if (it != known_.end()) {
        out = it->second;
        return true;
    }
    if (pending_.insert(k->second).second) {
        queue_.push_back(videoPath);
        cv_.notify_one();
    }
    return false;
}

void CropCache::Forget(const std::string& videoPath) {
    std::lock_guard lk(m_);
    keyOf_.erase(videoPath);
}

void CropCache::Loop() {
    for (;;) {
        std::string path;
        {
            std::unique_lock lk(m_);
            cv_.wait(lk, [this] { return quit_ || !queue_.empty(); });
            if (quit_) return;
            path = queue_.front();
            queue_.pop_front();
        }
        std::string key = Key(path), err;
        CropRect r;
        DetectCrop(util::U8Path(path), r, err);
        if (!r.Full())
            logx::Info("Bandes noires retirées de {} : zone utile {:.0f} % x {:.0f} %", path, r.w * 100, r.h * 100);
        {
            std::lock_guard lk(m_);
            if (!key.empty()) known_[key] = r;
            pending_.erase(key);
            keyOf_.erase(path);  // la clé sera relue (fichier éventuellement remplacé entre-temps)
        }
        Save();
    }
}

void CropCache::Save() {
    nlohmann::json j = nlohmann::json::object();
    {
        std::lock_guard lk(m_);
        std::error_code ec;
        for (auto& [k, v] : known_) {
            // Entrées dont la vidéo n'existe plus : oubliées.
            std::string path = k.substr(0, k.find('|'));
            if (!fs::exists(util::U8Path(path), ec)) continue;
            j[k] = {v.x, v.y, v.w, v.h};
        }
    }
    std::error_code ec;
    fs::create_directories(file_.parent_path(), ec);
    fs::path tmp = file_;
    tmp += L".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << j.dump(1);
    }
    fs::rename(tmp, file_, ec);
}
