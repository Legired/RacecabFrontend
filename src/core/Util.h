#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace util {

// Conversions UTF-8 <-> UTF-16 (API Win32). Tout le programme manipule de l'UTF-8.
std::wstring Widen(const std::string& s);
std::string Narrow(const std::wstring& s);

// std::filesystem::path(const char*) utilise la page de code ANSI sous Windows :
// toujours passer par ces deux fonctions pour les chemins.
fs::path U8Path(const std::string& s);
std::string PathU8(const fs::path& p);

int64_t NowUnix();      // horloge murale, secondes
uint64_t NowMs();       // horloge monotone, millisecondes

std::string ToLower(std::string s);
std::string Trim(const std::string& s);
bool FileExists(const std::string& utf8Path);
std::string FormatDuration(int64_t seconds);  // "1 h 05 min"

}  // namespace util
