#include "core/Log.h"

#include <windows.h>
#include <share.h>

#include <chrono>
#include <cstdio>
#include <mutex>

namespace logx {
namespace {
std::mutex g_mutex;
FILE* g_file = nullptr;
fs::path g_dir;
fs::path g_path;
size_t g_written = 0;
constexpr size_t kMaxBytes = 5 * 1024 * 1024;
constexpr int kKeep = 3;

void OpenFile() {
    g_file = _wfsopen(g_path.c_str(), L"ab", _SH_DENYWR);  // lisible par d'autres programmes pendant l'exécution
    std::error_code ec;
    g_written = fs::exists(g_path, ec) ? (size_t)fs::file_size(g_path, ec) : 0;
}

void Rotate() {
    if (g_file) fclose(g_file);
    std::error_code ec;
    for (int i = kKeep - 1; i >= 1; --i) {
        fs::path from = g_dir / std::format(L"frontend.{}.log", i);
        fs::path to = g_dir / std::format(L"frontend.{}.log", i + 1);
        fs::rename(from, to, ec);
    }
    fs::rename(g_path, g_dir / L"frontend.1.log", ec);
    OpenFile();
}
}  // namespace

void Init(const fs::path& dir) {
    std::lock_guard lk(g_mutex);
    std::error_code ec;
    fs::create_directories(dir, ec);
    g_dir = dir;
    g_path = dir / L"frontend.log";
    OpenFile();
}

void Shutdown() {
    std::lock_guard lk(g_mutex);
    if (g_file) fclose(g_file);
    g_file = nullptr;
}

std::string FilePath() { return util::PathU8(g_path); }

void Write(Level lvl, const std::string& msg) {
    static const char* names[] = {"DEBUG", "INFO ", "WARN ", "ERROR"};
    SYSTEMTIME t;
    GetLocalTime(&t);
    std::string line = std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03} [{}] {}\n", t.wYear, t.wMonth, t.wDay,
                                   t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, names[(int)lvl], msg);
    std::lock_guard lk(g_mutex);
    OutputDebugStringW(util::Widen(line).c_str());
    if (!g_file) return;
    fwrite(line.data(), 1, line.size(), g_file);
    fflush(g_file);
    g_written += line.size();
    if (g_written > kMaxBytes) Rotate();
}

}  // namespace logx
