#include "core/System.h"

#include <windows.h>
#include <shellapi.h>

#include <chrono>
#include <deque>
#include <fstream>
#include <vector>

namespace sys {
namespace {
constexpr const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t* kRunValue = L"RacecabFrontend";

std::wstring Quote(const std::wstring& a) {
    if (!a.empty() && a.find_first_of(L" \t\"") == std::wstring::npos) return a;
    std::wstring q = L"\"";
    size_t bs = 0;
    for (wchar_t c : a) {
        if (c == L'\\') {
            ++bs;
            continue;
        }
        if (c == L'"') q.append(bs * 2 + 1, L'\\');
        else q.append(bs, L'\\');
        bs = 0;
        q += c;
    }
    q.append(bs * 2, L'\\');
    return q + L"\"";
}

// Arguments actuels (sans le nom du programme), en retirant ceux de « skip ».
std::wstring Args(std::initializer_list<const wchar_t*> skip) {
    int n = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &n);
    std::wstring out;
    for (int i = 1; i < n; ++i) {
        bool drop = false;
        for (auto s : skip) drop |= _wcsicmp(argv[i], s) == 0;
        // Options de test (et leur valeur) jamais transmises à une relance : elles se rejoueraient en boucle.
        if (_wcsicmp(argv[i], L"--actions") == 0 || _wcsicmp(argv[i], L"--screenshot") == 0) {
            ++i;
            continue;
        }
        if (!drop) out += L" " + Quote(argv[i]);
    }
    LocalFree(argv);
    return out;
}

void WatchLog(const std::wstring& line) {
    // Journal minimal de la surveillance, à côté du programme (le dossier de données peut être inaccessible).
    std::wofstream f(ExePath().parent_path() / L"watchdog.log", std::ios::app);
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t ts[64];
    swprintf_s(ts, L"%04d-%02d-%02d %02d:%02d:%02d ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    f << ts << line << L"\n";
}
}  // namespace

fs::path ExePath() {
    wchar_t buf[MAX_PATH * 4];
    GetModuleFileNameW(nullptr, buf, (DWORD)std::size(buf));
    return fs::path(buf);
}

bool AutoStartEnabled() {
    wchar_t buf[4096];
    DWORD size = sizeof(buf);
    return RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS;
}

bool SetAutoStart(bool on, const fs::path& dataDir, std::string& err) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) {
        err = "accès au registre impossible";
        return false;
    }
    LSTATUS st;
    if (on) {
        std::wstring cmd = Quote(ExePath().wstring()) + L" --watchdog --data " + Quote(dataDir.wstring());
        st = RegSetValueExW(k, kRunValue, 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        st = RegDeleteValueW(k, kRunValue);
        if (st == ERROR_FILE_NOT_FOUND) st = ERROR_SUCCESS;
    }
    RegCloseKey(k);
    if (st != ERROR_SUCCESS) err = "écriture dans le registre impossible (" + std::to_string(st) + ")";
    return st == ERROR_SUCCESS;
}

bool UnderWatchdog() {
    wchar_t v[8];
    return GetEnvironmentVariableW(L"RACECAB_WATCHDOG", v, 8) > 0;
}

bool SpawnSelf(const std::wstring& extraArg, std::string& err) {
    std::wstring cmd = Quote(ExePath().wstring()) + Args({L"--restart", L"--watchdog"});
    if (!extraArg.empty()) cmd += L" " + extraArg;
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        err = "relance impossible (erreur " + std::to_string(GetLastError()) + ")";
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

int RunWatchdog() {
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\RacecabFrontend.Watchdog");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) return 0;  // déjà surveillé
    SetEnvironmentVariableW(L"RACECAB_WATCHDOG", L"1");
    std::wstring base = Quote(ExePath().wstring()) + Args({L"--watchdog", L"--restart"});
    std::deque<std::chrono::steady_clock::time_point> crashes;
    bool restartArg = false;
    WatchLog(L"Surveillance démarrée");
    for (;;) {
        std::wstring cmd = base + (restartArg ? L" --restart" : L"");
        STARTUPINFOW si{sizeof(si)};
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
            WatchLog(L"Lancement impossible, erreur " + std::to_wstring(GetLastError()));
            break;
        }
        CloseHandle(pi.hThread);
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess);
        if (code == 0) {
            WatchLog(L"Fermeture normale du frontend : fin de la surveillance");
            break;
        }
        if (code == (DWORD)kRestartExitCode) {
            WatchLog(L"Redémarrage demandé");
            restartArg = true;
            continue;
        }
        auto now = std::chrono::steady_clock::now();
        crashes.push_back(now);
        while (!crashes.empty() && now - crashes.front() > std::chrono::minutes(10)) crashes.pop_front();
        wchar_t hex[32];
        swprintf_s(hex, L"0x%08X", code);
        WatchLog(std::wstring(L"Fermeture anormale (code ") + hex + L") : relance dans 2 s");
        if (crashes.size() > 5) {
            WatchLog(L"Plus de 5 plantages en 10 minutes : abandon (voir le journal du frontend)");
            break;
        }
        restartArg = false;
        Sleep(2000);
    }
    if (mutex) CloseHandle(mutex);
    return 0;
}

}  // namespace sys
