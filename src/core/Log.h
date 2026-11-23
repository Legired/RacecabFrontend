#pragma once
#include <format>
#include <string>

#include "core/Util.h"

// Journal minimal, thread-safe, avec rotation (5 Mo x 3 fichiers).
namespace logx {

enum class Level { Debug, Info, Warn, Error };

void Init(const fs::path& dir);
void Shutdown();
void Write(Level lvl, const std::string& msg);
std::string FilePath();

template <class... A> void Info(std::format_string<A...> f, A&&... a) { Write(Level::Info, std::format(f, std::forward<A>(a)...)); }
template <class... A> void Warn(std::format_string<A...> f, A&&... a) { Write(Level::Warn, std::format(f, std::forward<A>(a)...)); }
template <class... A> void Error(std::format_string<A...> f, A&&... a) { Write(Level::Error, std::format(f, std::forward<A>(a)...)); }
template <class... A> void Debug(std::format_string<A...> f, A&&... a) { Write(Level::Debug, std::format(f, std::forward<A>(a)...)); }

}  // namespace logx
