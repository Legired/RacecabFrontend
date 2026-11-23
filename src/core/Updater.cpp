#include "core/Updater.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include <vector>

#include "core/Log.h"
#include "media/Zip.h"
#include "net/Http.h"

namespace {

std::vector<int> Parts(const std::string& v) {
    std::vector<int> out;
    size_t i = 0;
    while (i < v.size() && !std::isdigit((unsigned char)v[i])) ++i;  // « v0.6.1 » -> « 0.6.1 »
    int cur = -1;
    for (; i < v.size(); ++i) {
        char c = v[i];
        if (std::isdigit((unsigned char)c)) cur = (cur < 0 ? 0 : cur) * 10 + (c - '0');
        else if (c == '.') {
            out.push_back(std::max(cur, 0));
            cur = -1;
        } else break;  // « 0.6.0 (phase 5) », « 0.7.0-beta » : suffixe ignoré
    }
    if (cur >= 0) out.push_back(cur);
    return out;
}

std::string Sha256Hex(const fs::path& file) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    std::string hex;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return hex;
    if (BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) == 0) {
        std::ifstream in(file, std::ios::binary);
        std::vector<char> buf(1 << 20);
        while (in) {
            in.read(buf.data(), (std::streamsize)buf.size());
            if (in.gcount() > 0) BCryptHashData(h, (PUCHAR)buf.data(), (ULONG)in.gcount(), 0);
        }
        unsigned char d[32];
        if (BCryptFinishHash(h, d, 32, 0) == 0)
            for (unsigned char b : d) hex += std::format("{:02x}", b);
        BCryptDestroyHash(h);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    return hex;
}

bool SafeEntry(const std::string& n) {
    return !n.empty() && n.find("..") == std::string::npos && n[0] != '/' && n[0] != '\\' && n.find(':') == std::string::npos;
}

// Chemin en chaîne PowerShell entre apostrophes (apostrophe doublée : « Cruis'n » -> 'Cruis''n')
std::wstring PsEscape(const fs::path& p) {
    std::wstring s = p.wstring(), out;
    for (wchar_t c : s) {
        out += c;
        if (c == L'\'') out += L'\'';
    }
    return L"'" + out + L"'";
}

}  // namespace

Updater::~Updater() {
    cancel_ = true;
    Join();
}

void Updater::Join() {
    if (worker_.joinable()) worker_.join();
}

void Updater::Set(State s, const std::string& msg) {
    std::lock_guard lk(m_);
    state_ = s;
    message_ = msg;
}

Updater::State Updater::GetState() {
    std::lock_guard lk(m_);
    return state_;
}

std::string Updater::Message() {
    std::lock_guard lk(m_);
    return message_;
}

ReleaseInfo Updater::Release() {
    std::lock_guard lk(m_);
    return rel_;
}

float Updater::Progress() { return progress_; }

int Updater::CompareVersions(const std::string& a, const std::string& b) {
    auto pa = Parts(a), pb = Parts(b);
    size_t n = std::max(pa.size(), pb.size());
    pa.resize(n, 0);
    pb.resize(n, 0);
    for (size_t i = 0; i < n; ++i)
        if (pa[i] != pb[i]) return pa[i] < pb[i] ? -1 : 1;
    return 0;
}

bool Updater::CanInstall(const fs::path& exeDir) {
    std::error_code ec;
    return fs::exists(exeDir / L"datadir.txt", ec) && fs::exists(exeDir / L"RacecabFrontend.exe", ec);
}

void Updater::Check(const std::string& repo, const std::string& current) {
    if (Busy()) return;
    Join();
    cancel_ = false;
    Set(State::Checking, "Recherche d'une mise à jour…");
    worker_ = std::thread([this, repo, current] {
        // « propriétaire/dépôt » ; une adresse complète (http…) est utilisée telle quelle (serveur de test)
        const std::string url = repo.rfind("http", 0) == 0 ? repo : "https://api.github.com/repos/" + repo + "/releases/latest";
        HttpResponse r = HttpGet(url, {"Accept: application/vnd.github+json", "X-GitHub-Api-Version: 2022-11-28"}, 15000,
                                 4 * 1024 * 1024, &cancel_);
        if (r.status == 404) {
            Set(State::UpToDate, "Aucune version publiée sur " + repo);
            return;
        }
        if (!r.ok()) {
            Set(State::Error, r.status ? std::format("GitHub a répondu HTTP {}", r.status) : "Pas de connexion : " + r.error);
            return;
        }
        ReleaseInfo info;
        try {
            auto j = nlohmann::json::parse(r.body);
            info.tag = j.value("tag_name", "");
            info.name = j.value("name", "");
            info.notes = j.value("body", "");
            info.published = j.value("published_at", "").substr(0, 10);
            info.version = info.tag;
            if (!info.version.empty() && (info.version[0] == 'v' || info.version[0] == 'V')) info.version.erase(0, 1);
            for (auto& a : j.value("assets", nlohmann::json::array())) {
                std::string n = a.value("name", ""), lower = util::ToLower(n);
                if (lower.rfind("racecabfrontend-", 0) == 0 && lower.ends_with(".zip") && lower.find("source") == std::string::npos) {
                    info.assetName = n;
                    info.assetUrl = a.value("browser_download_url", "");
                    info.assetSize = a.value("size", 0ull);
                }
            }
            for (auto& a : j.value("assets", nlohmann::json::array()))
                if (!info.assetName.empty() && a.value("name", "") == info.assetName + ".sha256")
                    info.shaUrl = a.value("browser_download_url", "");
        } catch (const std::exception& e) {
            Set(State::Error, std::string("Réponse de GitHub illisible : ") + e.what());
            return;
        }
        {
            std::lock_guard lk(m_);
            rel_ = info;
        }
        if (info.version.empty()) {
            Set(State::Error, "Release sans numéro de version");
        } else if (CompareVersions(info.version, current) <= 0) {
            Set(State::UpToDate, std::format("À jour : version {} (dernière publiée : {})", current, info.version));
        } else if (info.assetUrl.empty()) {
            Set(State::Error, std::format("Version {} publiée, mais sans paquet RacecabFrontend-*.zip", info.version));
        } else {
            Set(State::Available, std::format("Version {} disponible ({:.1f} Mo)", info.version, info.assetSize / 1048576.0));
        }
        logx::Info("Mise à jour : {}", Message());
    });
}

void Updater::Download(const fs::path& dataDir) {
    if (GetState() != State::Available) return;
    Join();
    cancel_ = false;
    progress_ = 0;
    ReleaseInfo info = Release();
    Set(State::Downloading, "Téléchargement de la version " + info.version + "…");
    worker_ = std::thread([this, info, dataDir] {
        std::error_code ec;
        fs::path dir = dataDir / L"updates";
        fs::create_directories(dir, ec);
        fs::path zip = dir / util::U8Path(info.assetName);
        HttpResponse r = HttpDownload(info.assetUrl, zip, &cancel_, [this](uint64_t got, uint64_t total) {
            if (total) progress_ = (float)got / (float)total;
        });
        if (!r.ok()) {
            Set(State::Available, r.status ? std::format("Téléchargement refusé (HTTP {})", r.status) : "Téléchargement impossible : " + r.error);
            return;
        }
        if (info.assetSize && fs::file_size(zip, ec) != info.assetSize) {
            Set(State::Available, "Paquet incomplet : réessayez");
            fs::remove(zip, ec);
            return;
        }
        if (!info.shaUrl.empty()) {  // empreinte publiée avec la release : le paquet doit correspondre exactement
            HttpResponse s = HttpGet(info.shaUrl, {}, 15000, 4096, &cancel_);
            std::string want = util::ToLower(util::Trim(s.body.substr(0, s.body.find_first_of(" \t\r\n"))));
            std::string got = Sha256Hex(zip);
            if (!s.ok() || want.size() != 64 || want != got) {
                Set(State::Available, "Empreinte SHA-256 du paquet incorrecte : mise à jour refusée");
                logx::Error("Mise à jour : SHA-256 attendu {} obtenu {}", want, got);
                fs::remove(zip, ec);
                return;
            }
        }
        // Extraction dans updates\<version>
        fs::path out = dir / util::U8Path(info.version);
        fs::remove_all(out, ec);
        fs::create_directories(out, ec);
        std::vector<ZipEntryInfo> entries;
        std::string err;
        if (!ZipList(zip, entries, err)) {
            Set(State::Available, "Paquet illisible : " + err);
            return;
        }
        for (auto& e : entries) {
            if (!SafeEntry(e.name) || e.name.back() == '/') continue;
            fs::path dst = out / util::U8Path(e.name);
            fs::create_directories(dst.parent_path(), ec);
            std::ofstream f(dst, std::ios::binary | std::ios::trunc);
            if (!ZipStreamEntry(zip, e.name, [&](const char* d, size_t n) {
                    f.write(d, (std::streamsize)n);
                    return (bool)f;
                }, err)) {
                Set(State::Available, "Extraction impossible : " + err);
                return;
            }
        }
        // Le paquet peut contenir un dossier racine (RacecabFrontend-x.y.z\…) ou les fichiers directement.
        fs::path root = out;
        if (!fs::exists(root / L"install.ps1", ec))
            for (auto& d : fs::directory_iterator(out, ec))
                if (d.is_directory(ec) && fs::exists(d.path() / L"install.ps1", ec)) root = d.path();
        if (!fs::exists(root / L"install.ps1", ec) || !fs::exists(root / L"RacecabFrontend.exe", ec)) {
            Set(State::Available, "Paquet invalide (install.ps1 ou RacecabFrontend.exe absent)");
            return;
        }
        {
            std::lock_guard lk(m_);
            extracted_ = root;
        }
        fs::remove(zip, ec);
        Set(State::Ready, "Version " + info.version + " prête à être installée");
        logx::Info("Mise à jour {} téléchargée et extraite dans {}", info.version, util::PathU8(root));
    });
}

bool Updater::LaunchInstall(const fs::path& installDir, const fs::path& dataDir, bool autoStart, bool kiosk, std::string& err) {
    fs::path src;
    {
        std::lock_guard lk(m_);
        if (state_ != State::Ready) {
            err = "aucune mise à jour prête";
            return false;
        }
        src = extracted_;
    }
    if (!CanInstall(installDir)) {
        err = "copie de développement : installation automatique désactivée";
        return false;
    }
    // Script : attendre la fermeture du frontend (et de sa surveillance), installer, relancer.
    fs::path dir = dataDir / L"updates";
    fs::path script = dir / L"appliquer-mise-a-jour.ps1", log = dir / L"mise-a-jour.log";
    std::wstring relaunch = autoStart ? L"'--watchdog','--data'," + PsEscape(dataDir) : L"'--data'," + PsEscape(dataDir);
    std::wstring s =
        L"$ErrorActionPreference = 'Continue'\r\n"
        L"$inst = " + PsEscape(installDir) + L"\r\n"
        L"Start-Transcript -Path " + PsEscape(log) + L" -Force | Out-Null\r\n"
        L"for ($i = 0; $i -lt 60; $i++) {\r\n"
        L"  $p = Get-Process RacecabFrontend -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith($inst, 'OrdinalIgnoreCase') }\r\n"
        L"  if (-not $p) { break }\r\n"
        L"  Start-Sleep -Milliseconds 500\r\n"
        L"}\r\n"
        L"$args2 = @('-ExecutionPolicy','Bypass','-File'," + PsEscape(src / L"install.ps1") + L",'-Quiet','-NoShortcut','-InstallDir',$inst" +
        (autoStart ? L",'-AutoStart'" : L"") + (kiosk ? L",'-Kiosk'" : L"") + L")\r\n"
        L"& powershell.exe @args2\r\n"
        L"Write-Host \"install.ps1 : code $LASTEXITCODE\"\r\n"
        L"Start-Process -FilePath (Join-Path $inst 'RacecabFrontend.exe') -ArgumentList @(" + relaunch + L")\r\n"
        L"Stop-Transcript | Out-Null\r\n";
    {
        std::ofstream f(script, std::ios::binary | std::ios::trunc);
        const char bom[] = "\xEF\xBB\xBF";  // PowerShell 5 : UTF-8 avec BOM pour les chemins accentués
        f.write(bom, 3);
        std::string u8 = util::Narrow(s);
        f.write(u8.data(), (std::streamsize)u8.size());
        if (!f) {
            err = "script de mise à jour impossible à écrire";
            return false;
        }
    }
    std::wstring cmd = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" + script.wstring() + L"\"";
    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    // Console masquée (PowerShell 5 en a besoin) ; hors de tout Job parent si possible, pour survivre à la fermeture.
    const DWORD base = CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP;
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, base | CREATE_BREAKAWAY_FROM_JOB, nullptr, dir.c_str(),
                        &si, &pi) &&
        !CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, base, nullptr, dir.c_str(), &si, &pi)) {
        err = std::format("lancement du script impossible (code {})", GetLastError());
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    logx::Info("Mise à jour {} : script lancé ({}), fermeture du frontend", Release().version, util::PathU8(script));
    return true;
}
