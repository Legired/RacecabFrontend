#include "core/Util.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <format>

namespace util {

std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

fs::path U8Path(const std::string& s) { return fs::path(Widen(s)); }
std::string PathU8(const fs::path& p) { return Narrow(p.wstring()); }

int64_t NowUnix() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

uint64_t NowMs() {
    using namespace std::chrono;
    return (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

std::string ToLower(std::string s) {
    // Suffisant pour l'ASCII (extensions, mots-clés) ; les accents restent intacts.
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n\"");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n\"");
    return s.substr(a, b - a + 1);
}

bool FileExists(const std::string& p) {
    if (p.empty()) return false;
    std::error_code ec;
    return fs::is_regular_file(U8Path(p), ec);
}

std::string FormatDuration(int64_t s) {
    if (s < 60) return std::format("{} s", s);
    int64_t m = s / 60;
    if (m < 60) return std::format("{} min", m);
    return std::format("{} h {:02} min", m / 60, m % 60);
}

}  // namespace util
