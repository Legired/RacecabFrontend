#include "library/Emulators.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <regex>
#include <set>

#include "core/Log.h"

namespace emu {
namespace {

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

// Dossiers jamais parcourus pendant la recherche des émulateurs / ROMs (gros dossiers système ou sans intérêt).
bool SkipDir(const std::string& name) {
    static const std::set<std::string> kSkip = {
        "$recycle.bin", "system volume information", "windows", "windowsapps", "wpsystem", "program files",
        "program files (x86)", "programdata", "xboxgames", "steamlibrary", "wudownloadcache", ".git", ".github",
        "node_modules", "src", "source", "3rdparty", "third_party", "cmake", "cmakefiles", "shaders", "docs",
        "site", "obj", "saves", "nvram", "snap", "screenshots", "logs", "cache", "vcpkg", "vcpkg_installed"};
    return kSkip.count(Lower(name)) > 0;
}

const char* SystemLabel(const std::string& s) {
    if (s == "model2") return "Sega Model 2";
    if (s == "model3") return "Sega Model 3";
    if (s == "naomi") return "Sega Naomi";
    if (s == "naomi2") return "Sega Naomi 2";
    if (s == "atomiswave") return "Atomiswave";
    if (s == "hikaru") return "Sega Hikaru";
    if (s == "dreamcast") return "Dreamcast";
    if (s == "arcade") return "Arcade (MAME)";
    return "système inconnu";
}

// Système d'après les dossiers parents (« ROM\model2\daytona.zip »).
std::string SystemFromPath(const fs::path& file, const fs::path& root) {
    std::error_code ec;
    fs::path rel = fs::relative(file.parent_path(), root, ec);
    std::string found;
    for (const auto& part : ec ? file.parent_path() : rel) {
        std::string n = Lower(util::PathU8(part));
        n.erase(std::remove_if(n.begin(), n.end(), [](char c) { return c == ' ' || c == '-' || c == '_'; }), n.end());
        if (n == "model2" || n == "m2" || n == "segamodel2") found = "model2";
        else if (n == "model3" || n == "m3" || n == "segamodel3") found = "model3";
        else if (n == "naomi2") found = "naomi2";
        else if (n == "naomi") found = "naomi";
        else if (n == "atomiswave" || n == "awave") found = "atomiswave";
        else if (n == "hikaru") found = "hikaru";
        else if (n == "dreamcast" || n == "dc") found = "dreamcast";
        else if (n == "mame" || n == "arcade") found = "arcade";
    }
    return found;
}

// Jeux Model 2 et Model 3 (noms des ROMs comme MAME / les émulateurs) -> titre.
struct Known {
    const char* rom;
    const char* system;
    const char* title;
};
const Known kKnown[] = {
    // Sega Model 2
    {"daytona", "model2", "Daytona USA"}, {"daytona93", "model2", "Daytona USA (1993)"},
    {"dayton93", "model2", "Daytona USA (1993)"}, {"daytonam", "model2", "Daytona USA To The MAXX"},
    {"daytonase", "model2", "Daytona USA Special Edition"}, {"daytonas", "model2", "Daytona USA (Saturn Ads)"},
    {"srallyc", "model2", "Sega Rally Championship"}, {"srallycb", "model2", "Sega Rally Championship (Rev. B)"},
    {"indy500", "model2", "Indy 500"}, {"indy500d", "model2", "Indy 500 Deluxe"},
    {"indy500to", "model2", "Indy 500 Twin (Rev. A)"}, {"manxtt", "model2", "Manx TT Superbike"},
    {"manxttc", "model2", "Manx TT Superbike (Rev. C)"}, {"motoraid", "model2", "Motor Raid"},
    {"overrev", "model2", "Over Rev"}, {"overrevb", "model2", "Over Rev (Model 2B)"},
    {"stcc", "model2", "Sega Touring Car Championship"}, {"stcca", "model2", "Sega Touring Car Championship (Rev. A)"},
    {"stccb", "model2", "Sega Touring Car Championship (Rev. B)"}, {"sgt24h", "model2", "Super GT 24h"},
    {"waverunr", "model2", "Wave Runner"}, {"desert", "model2", "Desert Tank"},
    {"bel", "model2", "Behind Enemy Lines"}, {"vcop", "model2", "Virtua Cop"}, {"vcop2", "model2", "Virtua Cop 2"},
    {"hotd", "model2", "The House of the Dead"}, {"gunblade", "model2", "Gunblade NY"},
    {"rchase2", "model2", "Rail Chase 2"}, {"dynamcop", "model2", "Dynamite Cop"},
    {"vf2", "model2", "Virtua Fighter 2"}, {"fvipers", "model2", "Fighting Vipers"},
    {"lastbrnx", "model2", "Last Bronx"}, {"doa", "model2", "Dead or Alive"},
    {"von", "model2", "Cyber Troopers Virtual-On"}, {"zerogun", "model2", "Zero Gunner"},
    {"pltkids", "model2", "Pilot Kids"}, {"skisuprg", "model2", "Sega Ski Super G"},
    {"topskatr", "model2", "Top Skater"}, {"segawski", "model2", "Sega Water Ski"},
    {"dynabb", "model2", "Dynamite Baseball"}, {"sfight", "model2", "Sonic the Fighters"},
    {"vstriker", "model2", "Virtua Striker"}, {"skytargt", "model2", "Sky Target"},
    // Sega Model 3 (complété par Games.xml de Supermodel quand il est présent)
    {"scud", "model3", "Scud Race"}, {"scudplus", "model3", "Scud Race Plus"},
    {"srally2", "model3", "Sega Rally 2"}, {"daytona2", "model3", "Daytona USA 2"},
    {"dayto2pe", "model3", "Daytona USA 2 Power Edition"}, {"lemans24", "model3", "Le Mans 24"},
    {"harley", "model3", "Harley-Davidson & L.A. Riders"}, {"dirtdvls", "model3", "Dirt Devils"},
    {"eca", "model3", "Emergency Call Ambulance"}, {"lostwsga", "model3", "The Lost World"},
    {"oceanhun", "model3", "The Ocean Hunter"}, {"spikeout", "model3", "Spikeout"},
    {"spikeofe", "model3", "Spikeout Final Edition"}, {"vf3", "model3", "Virtua Fighter 3"},
    {"vf3tb", "model3", "Virtua Fighter 3 Team Battle"}, {"swtrilgy", "model3", "Star Wars Trilogy Arcade"},
    {"magtruck", "model3", "Magical Truck Adventure"}, {"von2", "model3", "Virtual-On Oratorio Tangram"},
    {"fvipers2", "model3", "Fighting Vipers 2"}, {"getbass", "model3", "Get Bass"},
    {"skichamp", "model3", "Ski Champ"}, {"bassdx", "model3", "Sega Bass Fishing"},
};

// BIOS et fichiers de système : jamais proposés comme jeux.
bool IsBios(const std::string& rom) {
    static const std::set<std::string> k = {"naomi", "naomi2", "awbios", "hikaru", "neogeo", "qsound", "segabill",
                                            "model2", "model3", "dc", "dcbios", "chihiro", "triforce", "f355bios"};
    return k.count(rom) > 0;
}

std::string Replace(std::string s, const std::string& from, const std::string& to) {
    for (size_t p = s.find(from); p != std::string::npos; p = s.find(from, p + to.size())) s.replace(p, from.size(), to);
    return s;
}

std::string DemulSystem(const std::string& sys) {
    return sys == "atomiswave" ? "awave" : sys == "naomi2" ? "naomi2" : sys == "hikaru" ? "hikaru" : "naomi";
}

// Titres Model 3 de Supermodel : Config\Games.xml (<game name="scud"> … <title>Scud Race</title>)
void LoadSupermodelTitles(const fs::path& root, std::map<std::string, std::string>& out, const std::atomic<bool>& cancel) {
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator() && !cancel; it.increment(ec)) {
        if (ec) break;
        if (it->is_directory(ec) && (it.depth() >= 3 || SkipDir(util::PathU8(it->path().filename())))) {
            it.disable_recursion_pending();
            continue;
        }
        if (Lower(util::PathU8(it->path().filename())) != "games.xml" ||
            Lower(util::PathU8(it->path().parent_path().filename())) != "config")
            continue;
        std::ifstream in(it->path(), std::ios::binary);
        std::string s((std::istreambuf_iterator<char>(in)), {});
        if (s.find("<games") == std::string::npos) continue;
        static const std::regex re(R"re(<game\s+name="([^"]+)"[^>]*>[\s\S]*?<title>([^<]+)</title>)re");
        int n = 0;
        for (std::sregex_iterator m(s.begin(), s.end(), re), end; m != end; ++m, ++n) out[Lower((*m)[1])] = util::Trim((*m)[2]);
        logx::Info("Supermodel : {} titres lus dans {}", n, util::PathU8(it->path()));
    }
}

}  // namespace

const std::vector<Def>& Catalog() {
    static const std::vector<Def> k = {
        {"sm2-emu", "sm2-emu", {"sm2-emu.exe"}, "",
         R"(--config "{emudir}" --nvram "{emudir}\saves" --game {rom} "{romfile}")", {".zip", ".7z"}, {"model2"}, ""},
        {"m2emulator", "Model 2 Emulator", {"emulator_multicpu.exe", "emulator.exe"}, "emulator.ini", "{rom}",
         {".zip"}, {"model2"}, "la ROM doit être dans un dossier déclaré dans EMULATOR.INI"},
        {"supermodel", "Supermodel", {"supermodel.exe"}, "", R"("{romfile}" -fullscreen)", {".zip"}, {"model3"}, ""},
        {"flycast", "Flycast", {"flycast.exe"}, "", R"("{romfile}")", {".zip", ".7z", ".chd", ".gdi", ".cdi"},
         {"naomi", "naomi2", "atomiswave", "dreamcast"}, ""},
        {"demul", "Demul", {"demul.exe"}, "", "-run={demulsys} -rom={rom}", {".zip", ".7z"},
         {"naomi", "naomi2", "atomiswave", "hikaru"}, ""},
        {"mame", "MAME", {"mame.exe", "mame64.exe"}, "", R"({rom} -rompath "{romdir}")", {".zip", ".7z", ".chd"}, {"*"}, ""},
        {"teknoparrot", "TeknoParrot", {"teknoparrotui.exe"}, "", "", {}, {}, ""},
    };
    return k;
}

std::string SystemOf(const std::string& romName) {
    const std::string r = Lower(romName);
    for (const Known& k : kKnown)
        if (r == k.rom) return k.system;
    return {};
}

std::string TitleOf(const std::string& romName) {
    const std::string r = Lower(romName);
    for (const Known& k : kKnown)
        if (r == k.rom) return k.title;
    return {};
}

std::vector<Found> Detect(const fs::path& root, const std::atomic<bool>& cancel) {
    std::vector<std::pair<Found, fs::file_time_type>> found;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator() && !cancel; it.increment(ec)) {
        if (ec) break;
        if (it->is_directory(ec)) {
            if (it.depth() >= 4 || SkipDir(util::PathU8(it->path().filename()))) it.disable_recursion_pending();
            continue;
        }
        const std::string fn = Lower(util::PathU8(it->path().filename()));
        if (it->path().extension() != L".exe" && it->path().extension() != L".EXE") continue;
        for (const Def& d : Catalog()) {
            if (std::find(d.exeNames.begin(), d.exeNames.end(), fn) == d.exeNames.end()) continue;
            if (!d.marker.empty() && !fs::exists(it->path().parent_path() / util::U8Path(d.marker), ec)) continue;
            Found f;
            f.def = &d;
            f.exe = it->path();
            fs::path rel = fs::relative(it->path().parent_path(), root, ec);
            f.label = d.name + " (" + (ec || rel.empty() || rel == L"." ? util::PathU8(it->path().parent_path()) : util::PathU8(rel)) + ")";
            found.push_back({f, fs::last_write_time(it->path(), ec)});
            break;
        }
    }
    // Ordre du catalogue, puis le plus récent d'abord pour un même émulateur (ex. plusieurs compilations de sm2-emu)
    auto order = [](const Def* d) { return d - Catalog().data(); };
    std::stable_sort(found.begin(), found.end(), [&](const auto& a, const auto& b) {
        if (a.first.def != b.first.def) return order(a.first.def) < order(b.first.def);
        return a.second > b.second;
    });
    std::vector<Found> out;
    for (auto& f : found) out.push_back(std::move(f.first));
    logx::Info("Émulateurs trouvés dans {} : {}", util::PathU8(root), out.size());
    return out;
}

std::vector<ScanCandidate> ScanRoms(const fs::path& romRoot, const fs::path& emuRoot, const std::vector<Found>& emus,
                                    const std::atomic<bool>& cancel,
                                    const std::function<void(const std::string&)>& progress) {
    std::map<std::string, std::string> smTitles;  // titres Model 3 de Supermodel
    if (!emuRoot.empty()) LoadSupermodelTitles(emuRoot, smTitles, cancel);

    struct RomFile {
        fs::path path, root;
        bool knownOnly;  // dossier d'un émulateur : seulement les ROMs connues (pas les autres .zip)
    };
    std::vector<RomFile> files;
    std::set<std::wstring> seen;
    std::set<std::string> exts;
    for (const Found& f : emus)
        for (const auto& e : f.def->exts) exts.insert(e);
    if (exts.empty()) exts = {".zip", ".7z", ".chd"};
    auto collect = [&](const fs::path& dir, int maxDepth, bool knownOnly) {
        std::error_code ec;
        if (dir.empty() || !fs::is_directory(dir, ec)) return;
        for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator() && !cancel; it.increment(ec)) {
            if (ec) break;
            if (it->is_directory(ec)) {
                if (it.depth() >= maxDepth || SkipDir(util::PathU8(it->path().filename()))) it.disable_recursion_pending();
                else if (progress) progress(util::PathU8(it->path()));
                continue;
            }
            if (!exts.count(Lower(util::PathU8(it->path().extension())))) continue;
            std::wstring key = util::Widen(Lower(util::PathU8(fs::absolute(it->path(), ec))));
            if (seen.insert(key).second) files.push_back({it->path(), dir, knownOnly});
        }
    };
    collect(romRoot, 3, false);
    // Dossier propre de chaque émulateur (dossier de premier niveau sous emuRoot, et son sous-dossier roms)
    for (const Found& f : emus) {
        if (f.def->id == "teknoparrot") continue;
        fs::path top = f.exe.parent_path();
        std::error_code ec;
        fs::path rel = fs::relative(f.exe.parent_path(), emuRoot, ec);
        if (!ec && !rel.empty() && rel.begin() != rel.end() && util::PathU8(*rel.begin()) != "..") top = emuRoot / *rel.begin();
        collect(top, 2, true);
    }

    std::vector<ScanCandidate> out;
    for (const RomFile& rf : files) {
        if (cancel) break;
        const std::string rom = Lower(util::PathU8(rf.path.stem()));
        if (IsBios(rom)) continue;
        std::string system = SystemOf(rom), title = TitleOf(rom);
        if (auto t = smTitles.find(rom); t != smTitles.end()) {
            if (system.empty()) system = "model3";
            if (title.empty()) title = t->second;
        }
        if (system.empty()) system = SystemFromPath(rf.path, rf.root);
        if (rf.knownOnly && system.empty()) continue;

        ScanCandidate c;
        c.kind = "emulator";
        c.rawName = rom;
        c.name = !title.empty() ? title : scanner::CleanName(util::PathU8(rf.path.stem()));
        c.system = SystemLabel(system);
        const std::string ext = Lower(util::PathU8(rf.path.extension()));
        // Émulateurs possibles : ceux qui gèrent ce système (conseillés), MAME en dernier recours
        for (int pass = 0; pass < 2; ++pass)
            for (const Found& f : emus) {
                const Def& d = *f.def;
                if (d.id == "teknoparrot" || std::find(d.exts.begin(), d.exts.end(), ext) == d.exts.end()) continue;
                const bool specific = !system.empty() && std::find(d.systems.begin(), d.systems.end(), system) != d.systems.end();
                const bool any = std::find(d.systems.begin(), d.systems.end(), "*") != d.systems.end();
                if (pass == 0 ? !specific : (specific || !(any || system.empty()))) continue;
                ScanCandidate::EmuChoice ch;
                ch.label = f.label;
                ch.launchPath = util::PathU8(f.exe);
                ch.workDir = util::PathU8(f.exe.parent_path());
                std::string a = d.args;
                a = Replace(a, "{romfile}", util::PathU8(rf.path));
                a = Replace(a, "{romdir}", util::PathU8(rf.path.parent_path()));
                a = Replace(a, "{emudir}", util::PathU8(f.exe.parent_path()));
                a = Replace(a, "{demulsys}", DemulSystem(system));
                a = Replace(a, "{rom}", rom);
                ch.args = a;
                ch.note = d.note;
                ch.recommended = pass == 0 || (system.empty() && any);
                c.emuChoices.push_back(ch);
            }
        if (!c.emuChoices.empty() && c.emuChoices[0].recommended) Choose(c, 0);
        else c.note = c.emuChoices.empty() ? "aucun émulateur trouvé pour ce système" : "émulateur à choisir";
        out.push_back(std::move(c));
    }
    std::sort(out.begin(), out.end(), [](const ScanCandidate& a, const ScanCandidate& b) { return Lower(a.name) < Lower(b.name); });
    return out;
}

std::vector<ScanCandidate> ScanTeknoParrot(const Found& tp) {
    std::vector<ScanCandidate> out;
    const fs::path dir = tp.exe.parent_path() / L"UserProfiles";
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (Lower(util::PathU8(e.path().extension())) != ".xml") continue;
        std::ifstream in(e.path(), std::ios::binary);
        std::string s((std::istreambuf_iterator<char>(in)), {});
        auto tag = [&](const char* t) {
            std::smatch m;
            std::regex re(std::string("<") + t + ">([^<]*)</" + t + ">");
            return std::regex_search(s, m, re) ? util::Trim(m[1].str()) : std::string();
        };
        ScanCandidate c;
        c.kind = "teknoparrot";
        c.system = "TeknoParrot";
        c.rawName = util::PathU8(e.path().stem());
        c.name = tag("GameNameInternal");
        if (c.name.empty()) c.name = c.rawName;
        c.launchPath = util::PathU8(tp.exe);
        c.args = "--profile=" + util::PathU8(e.path().filename());
        c.workDir = util::PathU8(tp.exe.parent_path());
        const std::string gamePath = tag("GamePath");
        if (gamePath.empty()) {
            c.needsSetup = true;
            c.note = "chemin du jeu non réglé dans TeknoParrot (à faire d'abord dans TeknoParrot)";
        } else if (!fs::exists(util::U8Path(gamePath), ec)) {
            c.needsSetup = true;
            c.note = "jeu introuvable : " + gamePath + " (chemin à corriger dans TeknoParrot)";
        } else {
            c.note = "jeu : " + gamePath;
        }
        out.push_back(std::move(c));
    }
    return out;
}

void Choose(ScanCandidate& c, int i) {
    if (i < 0 || i >= (int)c.emuChoices.size()) return;
    const auto& ch = c.emuChoices[(size_t)i];
    c.emuChoice = i;
    c.launchPath = ch.launchPath;
    c.args = ch.args;
    c.workDir = ch.workDir;
    c.runMode = ch.runMode;
    c.watchProcess = ch.watch;
    c.note = ch.note;
    if (ch.custom) c.needsSetup = false;  // l'utilisateur dit lui-même comment le lancer
}

}  // namespace emu
