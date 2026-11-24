#include "library/Scanner.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <regex>

#include "core/Log.h"

namespace scanner {
namespace {

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

// Dossiers jamais parcourus : compilation, dépendances, sauvegardes, langues, journaux…
bool SkipDir(const std::string& name) {
    static const std::regex re(
        R"(^(\..*|__.*|cmakefiles|build(-.*)?|out|obj|ll-deps|env|tmp|temp|utils|lang(\..*)?|locale|logs?|crash.*|)"
        R"(redist|_?commonredist|directx|dxsetup|vcredist.*|support|prerequisites|installers?|setup|node_modules|)"
        R"(backups?(-.*)?|.*[-_.]bak.*|.*[-_]orig|.*[-_]old|old|.*[-_]backup|update|.*[-_]update|patch(es)?|)"
        R"(screenshots?|saves?|savedata|nvram|docs?|manuals?|scripts|tools|source|src|.*-src|third_party)$)",
        std::regex::icase);
    return std::regex_match(name, re);
}

bool SkipExe(const std::string& stem) {
    static const std::regex re(
        R"(^(unins.*|uninstall.*|setup.*|install.*|vc_?redist.*|vcredist.*|dxsetup.*|dxwebsetup.*|directx.*|)"
        R"(.*crash.*|.*report.*|.*updater?|update.*|patch.*|a|msys2.*|python.*|pythonw|7z.*|winrar.*|)"
        R"(ffmpeg|ffprobe|ffplay|yt-dlp|unitycrashhandler.*|cefsharp.*|.*helper|.*service|.*server|amauthd.*|)"
        R"(sendcmd|regedit|cmd|powershell|notepad.*|dxdiag|dotnet.*|ndp.*|oalinst|physx.*|ue4prereq.*|)"
        R"(teknoparrotui|teknoparrot|parrotloader|openparrot.*|budgieloader|linuxloader|.*config.*|.*test.*|)"
        R"(.*debug.*|.*tool.*|.*editor.*|.*benchmark.*|.*server)$)",
        std::regex::icase);
    return std::regex_match(stem, re);
}

bool SkipBat(const std::string& stem) {
    static const std::regex re(
        R"(^(build.*|.*-build|.*[-_]debug|compile.*|make.*|install.*|setup.*|uninstall.*|update.*|clean.*|config.*|)"
        R"(.*\.bak.*|test.*|debug.*|patch.*|apply.*|register.*|unregister.*|fix.*|reset.*|kill.*|stop.*)$)",
        std::regex::icase);
    return std::regex_match(stem, re);
}

// Préférence pour un .bat « principal » : run_*, launch*, start*, play*, game*
int BatScore(const std::string& stem) {
    std::string s = Lower(stem);
    for (const char* k : {"run", "launch", "start", "play", "game", "jouer", "lancer"})
        if (s.find(k) != std::string::npos) return 2;
    return 1;
}

bool IsElf(const fs::path& f) {
    std::ifstream in(f, std::ios::binary);
    char m[4]{};
    in.read(m, 4);
    return in.gcount() == 4 && m[0] == 0x7f && m[1] == 'E' && m[2] == 'L' && m[3] == 'F';
}

bool BatHasPause(const fs::path& f) {
    std::ifstream in(f, std::ios::binary);
    std::string s((std::istreambuf_iterator<char>(in)), {});
    if (s.size() > 256 * 1024) return false;
    static const std::regex re(R"((^|[\s&|(])pause(\s|>|$))", std::regex::icase);
    // On ignore les lignes de commentaire (REM / ::)
    std::string line, kept;
    for (size_t a = 0; a < s.size();) {
        size_t b = s.find('\n', a);
        if (b == std::string::npos) b = s.size();
        line = s.substr(a, b - a);
        std::string t = Lower(line);
        size_t p = t.find_first_not_of(" \t");
        if (p != std::string::npos && t.compare(p, 3, "rem") != 0 && t.compare(p, 2, "::") != 0) kept += line + "\n";
        a = b + 1;
    }
    return std::regex_search(kept, re);
}

// Dossier au nom générique (pm, disk0, bin, game…) : le nom du jeu est celui d'un dossier parent.
bool GenericDir(const std::string& n) {
    static const std::regex re(R"(^(pm|disk\d*|bin(32|64)?|x64|x86|win(32|64)?|game|games|app|exe|release|)"
                               R"(retail|program|programs|data|content|binaries|windows(noeditor)?|shipping|config)$)",
                               std::regex::icase);
    return std::regex_match(n, re);
}

std::string NameFromDir(const fs::path& dir) {
    fs::path d = dir;
    for (int i = 0; i < 4 && d.has_parent_path() && GenericDir(util::PathU8(d.filename())); ++i) d = d.parent_path();
    return CleanName(util::PathU8(d.filename()));
}

struct Ctx {
    const std::atomic<bool>& cancel;
    const std::function<void(const std::string&)>& progress;
    std::vector<ScanCandidate> out;
    std::vector<fs::path> loaders;  // linuxloader.exe trouvés (pour les jeux qui n'en ont pas dans leur dossier)
};

// linuxloader.exe le plus proche : dans le dossier, puis dans les dossiers parents, puis le plus récent trouvé.
fs::path FindLoader(const fs::path& dir, const Ctx& c, std::string& note) {
    std::error_code ec;
    fs::path d = dir;
    for (int i = 0; i < 6; ++i) {
        if (fs::is_regular_file(d / L"linuxloader.exe", ec)) return d / L"linuxloader.exe";
        if (i == 0 && fs::is_regular_file(d / L"config" / L"linuxloader.exe", ec)) return d / L"config" / L"linuxloader.exe";
        if (!d.has_parent_path() || d.parent_path() == d) break;
        d = d.parent_path();
    }
    fs::path best;
    fs::file_time_type bt{};
    for (auto& l : c.loaders) {
        auto t = fs::last_write_time(l, ec);
        if (best.empty() || t > bt) {
            best = l;
            bt = t;
        }
    }
    if (!best.empty()) note = "linuxloader.exe pris dans " + util::PathU8(best.parent_path());
    return best;
}

void Walk(const fs::path& dir, int depth, Ctx& c) {
    if (c.cancel || depth > 6) return;
    c.progress(util::PathU8(dir));
    std::error_code ec;
    std::vector<fs::path> files, dirs;
    for (auto& e : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
        if (e.is_directory(ec)) dirs.push_back(e.path());
        else if (e.is_regular_file(ec)) files.push_back(e.path());
    }

    // ---- Jeu LinuxLoader : ELF « game » + linuxloader.ini (ou config\linuxloader.ini)
    bool hasIni = fs::exists(dir / L"linuxloader.ini", ec), hasCfgIni = fs::exists(dir / L"config" / L"linuxloader.ini", ec);
    if (fs::is_regular_file(dir / L"game", ec) && (hasIni || hasCfgIni) && IsElf(dir / L"game")) {
        ScanCandidate s;
        s.kind = "linuxloader";
        s.name = NameFromDir(dir);
        fs::path loader = FindLoader(dir, c, s.note);
        if (loader.empty()) {
            s.note = "linuxloader.exe introuvable : à indiquer dans la fiche";
            loader = dir / L"linuxloader.exe";
        }
        s.launchPath = util::PathU8(loader);
        std::string ini = hasCfgIni && !hasIni ? "config\\linuxloader.ini" : "linuxloader.ini";
        s.args = "-g \"" + util::PathU8(dir) + "\" -c \"" + ini + "\"";
        s.workDir = util::PathU8(dir);
        s.runMode = RunMode::Hidden;
        c.out.push_back(std::move(s));
        return;  // le contenu du dossier appartient au jeu
    }
    // ---- Jeu LinuxLoader : fichier .elf (ex. games\hummer\disk0\hummer_Master.elf)
    for (auto& f : files) {
        if (Lower(util::PathU8(f.extension())) != ".elf" || !IsElf(f)) continue;
        ScanCandidate s;
        s.kind = "linuxloader";
        s.name = NameFromDir(dir);
        fs::path loader = FindLoader(dir, c, s.note);
        if (loader.empty()) continue;  // un .elf sans loader n'est pas lançable
        s.launchPath = util::PathU8(loader);
        s.args = "\"" + util::PathU8(f) + "\"";
        s.workDir = util::PathU8(dir);
        s.runMode = RunMode::Hidden;
        c.out.push_back(std::move(s));
        return;
    }

    // ---- Lanceurs Windows
    std::vector<fs::path> bats, exes;
    for (auto& f : files) {
        std::string ext = Lower(util::PathU8(f.extension())), stem = util::PathU8(f.stem());
        if ((ext == ".bat" || ext == ".cmd") && !SkipBat(stem)) bats.push_back(f);
        else if (ext == ".exe" && !SkipExe(stem)) exes.push_back(f);
    }
    auto addBat = [&](const fs::path& b, const std::string& name) {
        ScanCandidate s;
        s.kind = "bat";
        s.name = name;
        s.launchPath = util::PathU8(b);
        if (BatHasPause(b)) {
            s.runMode = RunMode::Normal;  // console visible : la pause doit pouvoir être validée
            s.note = "le .bat contient « pause » : console laissée visible";
        }
        c.out.push_back(std::move(s));
    };
    if (bats.size() >= 2) {
        // Dossier de lanceurs : chaque .bat est un jeu, les sous-dossiers sont leurs données.
        for (auto& b : bats) addBat(b, CleanName(util::PathU8(b.stem())));
        return;
    }
    if (bats.size() == 1 && (exes.empty() || BatScore(util::PathU8(bats[0].stem())) == 2)) {
        addBat(bats[0], NameFromDir(dir));
        return;
    }
    if (!exes.empty()) {
        // Le plus gros .exe du dossier est en général le jeu.
        auto big = *std::max_element(exes.begin(), exes.end(), [](const fs::path& a, const fs::path& b) {
            std::error_code e1, e2;
            return fs::file_size(a, e1) < fs::file_size(b, e2);
        });
        if (fs::file_size(big, ec) >= 512 * 1024) {
            ScanCandidate s;
            s.kind = "exe";
            s.name = NameFromDir(dir);
            s.launchPath = util::PathU8(big);
            if (exes.size() > 1) s.note = std::format("{} .exe dans ce dossier : le plus gros est proposé", exes.size());
            c.out.push_back(std::move(s));
            return;
        }
    }
    std::sort(dirs.begin(), dirs.end());
    for (auto& d : dirs) {
        try {
            if (!SkipDir(util::PathU8(d.filename()))) Walk(d, depth + 1, c);
        } catch (const std::exception& e) {  // dossier illisible ou nom inattendu : on passe au suivant
            logx::Warn("Scan : dossier ignoré {} ({})", util::PathU8(d), e.what());
        }
    }
}

}  // namespace

std::string CleanName(const std::string& raw) {
    std::string s = raw;
    // (…) et […] retirés, ainsi que les numéros de version « v1.2.3 » / « 1.18.16 »
    s = std::regex_replace(s, std::regex(R"(\s*[\(\[][^\)\]]*[\)\]])"), "");
    s = std::regex_replace(s, std::regex(R"(\s+v?\d+(\.\d+){2,}\b)", std::regex::icase), "");
    std::replace(s.begin(), s.end(), '_', ' ');
    s = std::regex_replace(s, std::regex(R"(\s{2,})"), " ");
    s = util::Trim(s);
    while (!s.empty() && (s.back() == '-' || s.back() == '.')) s = util::Trim(s.substr(0, s.size() - 1));
    return s.empty() ? raw : s;
}

std::string Alias(const std::string& name) {
    static const std::pair<const char*, const char*> kAliases[] = {
        {"id4", "Initial D Arcade Stage 4"},          {"id5", "Initial D Arcade Stage 5"},
        {"id6", "Initial D Arcade Stage 6 AA"},       {"id7", "Initial D Arcade Stage 7 AA X"},
        {"id8", "Initial D Arcade Stage 8 Infinity"}, {"idz", "Initial D Arcade Stage Zero"},
        {"idzero", "Initial D Arcade Stage Zero"},    {"swdc", "Sega World Drivers Championship"},
        {"mkdx", "Mario Kart Arcade GP DX"},          {"mkgp2", "Mario Kart Arcade GP 2"},
        {"gti3", "GTI Club 3"},                       {"gticlub", "GTI Club 3"},
        {"twd", "The Walking Dead"},                  {"walkingdead", "The Walking Dead"},
        {"hotd4", "The House of the Dead 4"},         {"hotd5", "The House of the Dead: Scarlet Dawn"},
        {"lgj", "Let's Go Jungle!: Lost on the Island of Spice"}, {"or2sp", "OutRun 2 SP SDX"},
        {"srtv", "Sega Race TV"},                     {"abc", "After Burner Climax"},
        {"tc4", "Time Crisis 4"},                     {"tc5", "Time Crisis 5"},
        {"wmmt5", "Wangan Midnight Maximum Tune 5"},  {"wmmt6", "Wangan Midnight Maximum Tune 6"},
        {"cruisnblast", "Cruis'n Blast"},             {"crusnexo", "Cruis'n Exotica"},
        {"crusnusa", "Cruis'n USA"},                  {"crusnwld", "Cruis'n World"},
        {"bbhworld", "Big Buck Hunter World"},             {"hummer", "Hummer Extreme"},
        {"daytona3", "Daytona Championship USA"},     {"sr3", "Sega Rally 3"},
        {"segarally3", "Sega Rally 3"},               {"r-tuned", "R-Tuned: Ultimate Street Racing"},
    };
    std::string k;
    for (unsigned char ch : Lower(util::Trim(name)))
        if (std::isalnum(ch) || ch == '-') k += (char)ch;
    for (auto& [a, full] : kAliases)
        if (k == a) return full;
    return {};
}

std::vector<ScanCandidate> Scan(const fs::path& root, const std::atomic<bool>& cancel,
                                const std::function<void(const std::string&)>& progress) {
    Ctx c{cancel, progress, {}, {}};
    // Tous les linuxloader.exe sous root, y compris dans les dossiers de compilation (versions récentes) :
    // proposés aux jeux LinuxLoader qui n'en ont pas dans leur propre dossier.
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator() && !cancel; it.increment(ec)) {
        if (ec) break;
        if (it.depth() > 6 || Lower(util::PathU8(it->path().filename())) == "cmakefiles") {
            it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file(ec) && Lower(util::PathU8(it->path().filename())) == "linuxloader.exe")
            c.loaders.push_back(it->path());
    }
    Walk(root, 0, c);
    std::sort(c.out.begin(), c.out.end(), [](const ScanCandidate& a, const ScanCandidate& b) {
        return Lower(a.name) < Lower(b.name);
    });
    return c.out;
}

}  // namespace scanner
