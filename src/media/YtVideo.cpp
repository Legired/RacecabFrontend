#include "media/YtVideo.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <format>
#include <cstring>
#include <fstream>

#include "core/Log.h"
#include "media/Scraper.h"
#include "video/Remux.h"

namespace ytvideo {
namespace {

fs::path ExeDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return n ? fs::path(std::wstring(buf, n)).parent_path() : fs::path();
}

// Seuls les liens YouTube sont acceptés (la base LaunchBox n'en contient pas d'autres).
bool IsYoutube(const std::string& url) {
    std::string u = util::ToLower(url);
    return u.rfind("https://www.youtube.com/", 0) == 0 || u.rfind("https://youtube.com/", 0) == 0 ||
           u.rfind("https://youtu.be/", 0) == 0 || u.rfind("https://m.youtube.com/", 0) == 0 ||
           u.rfind("http://www.youtube.com/", 0) == 0 || u.rfind("http://youtu.be/", 0) == 0;
}

std::string Tail(const fs::path& log, size_t max = 300) {
    std::ifstream f(log, std::ios::binary);
    std::string s((std::istreambuf_iterator<char>(f)), {});
    // Dernière ligne « ERROR: … » si présente, sinon la fin du journal.
    size_t e = s.rfind("ERROR:");
    if (s.find("larger than max-filesize") != std::string::npos || s.find("File is larger") != std::string::npos)
        return "vidéo trop lourde (vidéo très longue) : choisir une vidéo plus courte";
    std::string t = e != std::string::npos ? s.substr(e) : s;
    t = util::Trim(t.substr(0, t.find('\n')));
    if (t.empty()) t = util::Trim(s.size() > max ? s.substr(s.size() - max) : s);
    return t.size() > max ? t.substr(0, max) : t;
}

// Lance yt-dlp (fenêtre masquée, sortie dans log) et attend sa fin. Arrêté si cancel ou après timeoutSec.
bool Run(const fs::path& exe, const std::wstring& args, const fs::path& log, const std::atomic<bool>& cancel,
         int timeoutSec, std::string& err) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE out = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (out == INVALID_HANDLE_VALUE) {
        err = "journal de yt-dlp impossible à créer";
        return false;
    }
    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = out;
    si.hStdError = out;
    std::wstring line = L"\"" + exe.wstring() + L"\" " + args;
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(exe.c_str(), line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                             log.parent_path().c_str(), &si, &pi);
    CloseHandle(out);
    if (!ok) {
        err = std::format("yt-dlp n'a pas pu être lancé (code {})", GetLastError());
        return false;
    }
    auto t0 = std::chrono::steady_clock::now();
    DWORD code = 1;
    for (;;) {
        if (WaitForSingleObject(pi.hProcess, 250) == WAIT_OBJECT_0) {
            GetExitCodeProcess(pi.hProcess, &code);
            break;
        }
        bool late = std::chrono::steady_clock::now() - t0 > std::chrono::seconds(timeoutSec);
        if (cancel || late) {
            // Outil lancé par le frontend lui-même (jamais un jeu) : on peut l'arrêter.
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 5000);
            err = cancel ? "annulé" : std::format("yt-dlp toujours actif après {} min", timeoutSec / 60);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return false;
        }
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (code != 0) {
        err = "yt-dlp : " + Tail(log);
        return false;
    }
    return true;
}

fs::path FindPrefix(const fs::path& dir, const std::wstring& prefix) {
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dir, ec)) {
        std::wstring n = e.path().filename().wstring();
        if (n.rfind(prefix, 0) == 0 && e.path().extension() != L".part" && e.path().extension() != L".log") return e.path();
    }
    return {};
}

}  // namespace

bool SelfUpdate(const ScraperConfig& c, std::string& out) {
    fs::path exe = FindYtDlp(c);
    if (exe.empty()) {
        out = "yt-dlp introuvable";
        return false;
    }
    fs::path log = fs::temp_directory_path() / L"racecab-ytdlp-update.log";
    std::atomic<bool> never{false};
    std::string err;
    bool ok = Run(exe, L"-U", log, never, 300, err);
    std::ifstream f(log, std::ios::binary);
    std::string s((std::istreambuf_iterator<char>(f)), {});
    f.close();
    std::error_code ec;
    fs::remove(log, ec);
    // Dernière ligne utile : « yt-dlp is up to date (…) » ou « Updated yt-dlp to … ».
    std::string last;
    size_t a = 0;
    while (a < s.size()) {
        size_t b = s.find('\n', a);
        if (b == std::string::npos) b = s.size();
        std::string line = util::Trim(s.substr(a, b - a));
        if (!line.empty() && line.rfind("WARNING", 0) != 0) last = line;
        a = b + 1;
    }
    if (last.find("up to date") != std::string::npos) out = "déjà à jour (" + last + ")";
    else if (last.find("Updated yt-dlp") != std::string::npos) out = "mis à jour (" + last + ")";
    else out = ok ? last : (err.empty() ? last : err);
    logx::Info("Mise à jour de yt-dlp : {}", out);
    return ok;
}

int ParseStart(const std::string& in) {
    std::string s = util::Trim(util::ToLower(in));
    if (s.empty()) return -1;
    int total = 0, cur = 0;
    bool any = false;
    for (char ch : s) {
        if (ch >= '0' && ch <= '9') {
            cur = cur * 10 + (ch - '0');
            any = true;
        } else if (ch == ':') {
            total = (total + cur) * 60;
            cur = 0;
        } else if (ch == 'h') {
            total += cur * 3600;
            cur = 0;
        } else if (ch == 'm') {
            total += cur * 60;
            cur = 0;
        } else if (ch == 's') {
            total += cur;
            cur = 0;
        } else {
            return -1;
        }
        if (total > 36000) return -1;
    }
    return any ? total + cur : -1;
}

int StartFromUrl(const std::string& url) {
    for (const char* key : {"?t=", "&t=", "#t=", "?start=", "&start="}) {
        size_t p = url.find(key);
        if (p == std::string::npos) continue;
        p += strlen(key);
        size_t e = url.find_first_of("&#", p);
        return ParseStart(url.substr(p, e == std::string::npos ? std::string::npos : e - p));
    }
    return -1;
}

fs::path FindYtDlp(const ScraperConfig& c) {
    std::error_code ec;
    if (!c.ytdlpPath.empty()) {
        fs::path p = util::U8Path(c.ytdlpPath);
        return fs::is_regular_file(p, ec) ? p : fs::path();
    }
    std::vector<fs::path> cands = {ExeDir() / L"yt-dlp.exe"};
    if (!c.lbDir.empty()) cands.push_back(c.lbDir.parent_path().parent_path() / L"tools" / L"yt-dlp.exe");
    for (auto& p : cands)
        if (fs::is_regular_file(p, ec)) return p;
    wchar_t buf[MAX_PATH];
    if (SearchPathW(nullptr, L"yt-dlp.exe", nullptr, MAX_PATH, buf, nullptr)) return buf;
    return {};
}

bool Download(const ScraperConfig& c, const std::string& url, const fs::path& dir, const std::atomic<bool>& cancel,
              std::string& err, fs::path* pendingPart) {
    if (!IsYoutube(url)) {
        err = "lien vidéo non pris en charge : " + url;
        return false;
    }
    fs::path exe = FindYtDlp(c);
    if (exe.empty()) {
        err = "yt-dlp introuvable";
        return false;
    }
    std::error_code ec;
    fs::path tmp = dir / L".video-tmp";
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);
    const std::wstring u = util::Widen(url);
    // --ignore-config : aucun réglage personnel de yt-dlp ne doit changer le résultat.
    const std::wstring common = L"--ignore-config --no-playlist --no-mtime --no-progress --newline --max-filesize 1500M ";
    logx::Info("Vidéo YouTube : téléchargement de {}", url);
    bool ok = Run(exe,
                  common +
                      // 720p 30 i/s de préférence ; vidéo longue (fichier lourd) : 480p, puis 360p.
                      L"-f \"bv[vcodec^=avc1][height<=720][fps<=30][ext=mp4][filesize<?400M]"
                      L"/bv[vcodec^=avc1][height<=720][ext=mp4][filesize<?400M]"
                      L"/bv[vcodec^=avc1][height<=480][ext=mp4][filesize<?400M]"
                      L"/bv[vcodec^=avc1][height<=360][ext=mp4]/b[vcodec^=avc1][ext=mp4]\" "
                      L"-o \"v.%(ext)s\" \"" + u + L"\"",
                  tmp / L"video.log", cancel, 900, err);
    fs::path v = ok ? FindPrefix(tmp, L"v.") : fs::path();
    if (ok && v.empty()) {
        ok = false;
        std::string why = Tail(tmp / L"video.log");
        err = why.find("trop lourde") != std::string::npos ? why : "yt-dlp n'a produit aucun fichier vidéo";
    }
    fs::path a;
    if (ok) {
        std::string aerr;  // son facultatif : sans lui, la vidéo reste muette
        if (Run(exe, common + L"-f \"ba[ext=m4a]/ba[acodec^=mp4a]\" -o \"a.%(ext)s\" \"" + u + L"\"", tmp / L"audio.log",
                cancel, 600, aerr))
            a = FindPrefix(tmp, L"a.");
        else if (cancel) {
            ok = false;
            err = aerr;
        } else
            logx::Warn("Vidéo YouTube : pas de son ({})", aerr);
    }
    if (ok) {
        fs::path part = dir / L"video.mp4.part";
        double total = MediaDuration(v);
        // Durée 0 : de start jusqu'à la fin de la vidéo.
        double dur = c.videoSeconds > 0 ? (double)c.videoSeconds : std::max(1.0, total - c.videoStartSec);
        ok = RemuxClip(v, a, part, (double)c.videoStartSec, dur, err);
        bool keepPart = false;
        if (ok && !MoveFileExW(part.c_str(), (dir / L"video.mp4").c_str(), MOVEFILE_REPLACE_EXISTING)) {
            DWORD e = GetLastError();
            if (pendingPart && (e == ERROR_SHARING_VIOLATION || e == ERROR_ACCESS_DENIED)) {
                *pendingPart = part;  // video.mp4 en cours de lecture : remplacement après arrêt de la lecture
                keepPart = true;
            } else {
                ok = false;
                err = std::format("écriture de video.mp4 impossible (code {})", e);
            }
        }
        if (!keepPart) fs::remove(part, ec);
        if (ok)
            logx::Info("Vidéo YouTube : extrait de {:.0f} s enregistré (source {:.0f} s{})", dur, total,
                       a.empty() ? ", sans son" : "");
    }
    fs::remove_all(tmp, ec);
    if (!ok) logx::Warn("Vidéo YouTube {} : {}", url, err);
    return ok;
}

}  // namespace ytvideo
