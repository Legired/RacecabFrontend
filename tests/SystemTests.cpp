// Tests de la sauvegarde / restauration et des archives ZIP (sans fenêtre, dans un dossier temporaire).
#include <sqlite3.h>
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <format>
#include <fstream>
#include <string>

#include "core/Backup.h"
#include "core/ConfigShare.h"
#include "core/Settings.h"
#include "library/Library.h"
#include "library/Scanner.h"
#include "core/Updater.h"
#include "core/Log.h"
#include "media/Zip.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                 \
    do {                                                            \
        if (cond) ++g_pass;                                         \
        else {                                                      \
            ++g_fail;                                               \
            std::printf("  ECHEC ligne %d : %s\n", __LINE__, #cond); \
        }                                                           \
    } while (0)

namespace {
void Write(const fs::path& f, const std::string& s) {
    fs::create_directories(f.parent_path());
    std::ofstream(f, std::ios::binary | std::ios::trunc) << s;
}
std::string Read(const fs::path& f) {
    std::ifstream in(f, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}
int64_t CountGames(const fs::path& db) {
    sqlite3* d = nullptr;
    int64_t n = -1;
    if (sqlite3_open_v2(util::PathU8(db).c_str(), &d, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK) {
        sqlite3_stmt* s = nullptr;
        if (sqlite3_prepare_v2(d, "SELECT COUNT(*) FROM games", -1, &s, nullptr) == SQLITE_OK && sqlite3_step(s) == SQLITE_ROW)
            n = sqlite3_column_int64(s, 0);
        sqlite3_finalize(s);
    }
    sqlite3_close(d);
    return n;
}
void Exec(const fs::path& db, const char* sql) {
    sqlite3* d = nullptr;
    sqlite3_open(util::PathU8(db).c_str(), &d);
    sqlite3_exec(d, sql, nullptr, nullptr, nullptr);
    sqlite3_close(d);
}
}  // namespace

int main() {
    SetConsoleOutputCP(CP_UTF8);
    fs::path root = fs::temp_directory_path() / L"racecab-system-test";
    std::error_code ec;
    fs::remove_all(root, ec);
    logx::Init(root / L"logs");
    Paths p;
    p.dataDir = root / L"données é";  // chemin avec espace et accent

    std::printf("- Archive ZIP : écriture puis relecture (noms UTF-8, fichier vide, fichier de 3 Mo)\n");
    {
        fs::path a = root / L"a.txt", b = root / L"vide.bin", c = root / L"gros.bin";
        Write(a, "contenu é");
        Write(b, "");
        std::string big(3 * 1024 * 1024, 'x');
        for (size_t i = 0; i < big.size(); i += 4096) big[i] = (char)(i / 4096);
        Write(c, big);
        ZipWriter z;
        std::string err;
        CHECK(z.Open(root / L"t.zip", err) && z.AddFile("dossier/à.txt", a, err) && z.AddFile("vide.bin", b, err) &&
              z.AddFile("gros.bin", c, err) && z.Close(err));
        std::vector<ZipEntryInfo> list;
        CHECK(ZipList(root / L"t.zip", list, err) && list.size() == 3);
        std::string got;
        CHECK(ZipStreamEntry(root / L"t.zip", "dossier/à.txt", [&](const char* d, size_t n) { got.append(d, n); return true; }, err));
        CHECK(got == "contenu é");
        got.clear();
        CHECK(ZipStreamEntry(root / L"t.zip", "gros.bin", [&](const char* d, size_t n) { got.append(d, n); return true; }, err));
        CHECK(got == big);
        CHECK(ZipStreamEntry(root / L"t.zip", "vide.bin", [&](const char*, size_t) { return true; }, err));
    }

    std::printf("- Sauvegarde : base (copie cohérente), réglages, profils, thèmes ; pas le cache ni les journaux\n");
    fs::create_directories(p.dataDir / L"db");
    Exec(p.DbFile(), "CREATE TABLE games(id INTEGER PRIMARY KEY, name TEXT); INSERT INTO games(name) VALUES('Rambo'),('Initial D');");
    Write(p.SettingsFile(), "{\"cabMode\":\"guncab\"}");
    Write(p.ProfileFile("GunCab"), "{\"theme\":\"neon\"}");
    Write(p.ThemesDir() / L"neon" / L"theme.json", "{}");
    Write(p.dataDir / L"cache" / L"launchbox" / L"launchbox.db", "cache");
    Write(p.LogsDir() / L"frontend.log", "journal");
    Write(p.MediaDir() / L"1" / L"logo.png", "png");
    std::string name, err;
    CHECK(backup::Create(p, "sauvegarde", false, name, err));
    auto list = backup::List(p);
    CHECK(list.size() == 1 && list[0].name == name && !list[0].date.empty() && !list[0].automatic);
    std::vector<ZipEntryInfo> entries;
    ZipList(list[0].file, entries, err);
    bool hasDb = false, hasSettings = false, hasTheme = false, hasCache = false, hasLog = false, hasMedia = false;
    for (auto& e : entries) {
        hasDb |= e.name == "db/library.sqlite";
        hasSettings |= e.name == "config/settings.json";
        hasTheme |= e.name == "themes/neon/theme.json";
        hasCache |= e.name.find("cache/") == 0;
        hasLog |= e.name.find("logs/") == 0;
        hasMedia |= e.name.find("media/") == 0;
    }
    CHECK(hasDb && hasSettings && hasTheme && !hasCache && !hasLog && !hasMedia);

    std::printf("- Restauration : appliquée au démarrage, état précédent sauvegardé\n");
    Exec(p.DbFile(), "DELETE FROM games;");
    Write(p.SettingsFile(), "{\"cabMode\":\"racecab\"}");
    CHECK(CountGames(p.DbFile()) == 0);
    CHECK(backup::ScheduleRestore(p, list[0].file, err));
    CHECK(fs::exists(p.RestorePending()));
    std::string msg;
    CHECK(backup::ApplyPendingRestore(p, msg));
    std::printf("    %s\n", msg.c_str());
    CHECK(CountGames(p.DbFile()) == 2);
    CHECK(Read(p.SettingsFile()) == "{\"cabMode\":\"guncab\"}");
    CHECK(!fs::exists(p.RestorePending()));
    bool safety = false;
    for (auto& i : backup::List(p)) safety |= i.name.rfind("avant-restauration-", 0) == 0;
    CHECK(safety);
    CHECK(!backup::ApplyPendingRestore(p, msg));  // rien en attente

    std::printf("- Restauration refusée : fichier qui n'est pas une sauvegarde\n");
    CHECK(!backup::ScheduleRestore(p, root / L"t.zip", err));
    std::printf("    %s\n", err.c_str());

    std::printf("- Archive piégée : chemins hors du dossier de données ignorés\n");
    {
        fs::path evil = root / L"piege.zip", f = root / L"x.txt";
        Write(f, "pirate");
        ZipWriter z;
        z.Open(evil, err);
        z.AddFile("db/library.sqlite", p.DbFile(), err);
        z.AddFile("../evade.txt", f, err);
        z.AddFile("config/../../evade2.txt", f, err);
        z.AddFile("C:/Windows/evade3.txt", f, err);
        z.AddFile("autre/evade4.txt", f, err);
        z.Close(err);
        CHECK(backup::ScheduleRestore(p, evil, err));
        CHECK(backup::ApplyPendingRestore(p, msg));
        std::printf("    %s\n", msg.c_str());
        CHECK(!fs::exists(root / L"evade.txt") && !fs::exists(root / L"evade2.txt") &&
              !fs::exists(p.dataDir / L"autre" / L"evade4.txt"));
        CHECK(msg.find("4 fichier(s) ignoré(s)") != std::string::npos);
    }

    std::printf("- Sauvegardes automatiques : une seule par jour, les plus anciennes supprimées\n");
    backup::AutoIfDue(p, 2);
    backup::AutoIfDue(p, 2);  // déjà faite il y a moins de 20 h : rien
    int autos = 0;
    for (auto& i : backup::List(p)) autos += i.automatic;
    CHECK(autos == 1);

    std::printf("- Partage de la configuration : export puis import sur une autre borne\n");
    {
        Paths pa, pb;
        pa.dataDir = root / L"share-a";
        pb.dataDir = root / L"share-b";
        int64_t idTirA = 0, idA1 = 0;
        {
            Library a;
            CHECK(a.Open(pa.dataDir / L"db" / L"library.sqlite", pa.MediaDir()));
            int64_t drift = a.EnsureCategoryPath("Course/Drift");
            idTirA = a.EnsureCategoryPath("Tir");
            Game g1;
            g1.name = "Initial D Arcade Stage 8";
            g1.launchPath = "D:\\Jeux\\ID8\\start.bat";
            g1.categories = {drift};
            g1.videoUrl = "https://youtu.be/abc";
            g1.videoStart = 6;
            g1.videoDuration = 0;
            idA1 = a.AddGame(g1);
            Game g2;
            g2.name = "Rambo";
            g2.launchPath = "D:\\Jeux\\Rambo\\rambo.exe";
            g2.categories = {idTirA};
            g2.videoUrl = "https://youtu.be/xyz";
            a.AddGame(g2);
            Write(a.MediaDirFor(idA1) / L"logo.png", "PNGLOGO");
            Write(pa.ProfilesDir() / L"Racecab.json", std::format("{{\"name\":\"Racecab\",\"hiddenCategories\":[{}]}}", idTirA));
            Write(pa.ThemesDir() / L"perso" / L"theme.json", "{}");
            share::ExportOptions o;
            std::string summary, err;
            CHECK(share::Export(a, pa, o, root / L"share.zip", summary, err));
            std::printf("    export : %s %s\n", summary.c_str(), err.c_str());
        }
        Library b;
        CHECK(b.Open(pb.dataDir / L"db" / L"library.sqlite", pb.MediaDir()));
        b.EnsureCategoryPath("Divers");  // numéros de catégories différents de la borne A
        Game existing;
        existing.name = "rambo";  // même jeu, nom en minuscules, déjà présent : complété, pas dupliqué
        existing.launchPath = "C:\\Autre\\rambo.exe";
        int64_t idB2 = b.AddGame(existing);
        share::Preview pv;
        std::string err;
        CHECK(share::ReadPreview(b, root / L"share.zip", "D:\\Jeux", "E:\\Arcade", pv, err));
        CHECK(pv.games == 2 && pv.newGames == 1 && pv.existing == 1 && pv.videoLinks == 2 && pv.profiles == 1);
        CHECK(pv.commonRoot == "D:\\Jeux");
        share::ImportOptions io;
        io.oldRoot = "D:\\Jeux";
        io.newRoot = "E:\\Arcade";
        share::ImportResult r;
        CHECK(share::Import(b, pb, root / L"share.zip", io, r, err));
        std::printf("    import : %s %s\n", r.summary.c_str(), err.c_str());
        CHECK(r.added == 1 && r.updated == 1 && b.Games().size() == 2);
        const Game* id8 = nullptr;
        for (const Game& g : b.Games())
            if (g.name == "Initial D Arcade Stage 8") id8 = &g;
        CHECK(id8 && id8->launchPath == "E:\\Arcade\\ID8\\start.bat");
        CHECK(id8 && id8->videoUrl == "https://youtu.be/abc" && id8->videoStart == 6 && id8->videoDuration == 0);
        CHECK(id8 && id8->categories.size() == 1 && b.NodePath(id8->categories[0]).find("Drift") != std::string::npos);
        CHECK(id8 && Read(b.MediaDirFor(id8->id) / L"logo.png") == "PNGLOGO");
        const Game* rb = b.FindGame(idB2);
        CHECK(rb && rb->launchPath == "C:\\Autre\\rambo.exe" && rb->videoUrl == "https://youtu.be/xyz");  // chemin gardé
        CHECK(r.needVideo.size() == 2);
        // Profil : catégorie masquée « Tir » retrouvée par son nom (numéro différent sur la borne B).
        std::string prof = Read(pb.ProfileFile("Racecab"));
        int64_t idTirB = b.EnsureCategoryPath("Tir");
        CHECK(idTirB != idTirA && prof.find(std::to_string(idTirB)) != std::string::npos);
        CHECK(fs::exists(pb.ThemesDir() / L"perso" / L"theme.json"));
        // Fichier qui n'est pas une configuration : refus clair.
        Write(root / L"pas-config.zip", "PK");
        CHECK(!share::ReadPreview(b, root / L"pas-config.zip", {}, {}, pv, err) && !err.empty());
    }

    std::printf("- Ajout automatique : scan d'un dossier de jeux\n");
    {
        fs::path g = root / L"scan";
        Write(g / L"Initial D Arcade Stage 8 Infinity (2014) (Japan) [TP]" / L"ID8.exe", std::string(600 * 1024, 'x'));
        Write(g / L"Initial D Arcade Stage 8 Infinity (2014) (Japan) [TP]" / L"unins000.exe", std::string(700 * 1024, 'x'));
        Write(g / L"loaders" / L"id4.bat", "@echo off\nstart.exe\n");
        Write(g / L"loaders" / L"twd.bat", "@echo off\nrem pause en commentaire\nloader.exe\npause >nul\n");
        Write(g / L"loaders" / L"build_all.bat", "x");
        Write(g / L"backup-old" / L"Old" / L"game.exe", std::string(600 * 1024, 'x'));
        Write(g / L"ll" / L"linuxloader.exe", "MZ");
        const std::string elf = std::string("\x7f" "ELF") + std::string(100, '\0');
        Write(g / L"ll" / L"cruisnblast" / L"game", elf);
        Write(g / L"ll" / L"cruisnblast" / L"linuxloader.ini", "");
        Write(g / L"ll" / L"games" / L"hummer" / L"disk0" / L"hummer_Master.elf", elf);
        std::atomic<bool> cancel{false};
        auto list = scanner::Scan(g, cancel, [](const std::string&) {});
        auto find = [&](const std::string& n) -> const ScanCandidate* {
            for (auto& c : list)
                if (c.name == n) return &c;
            return nullptr;
        };
        for (auto& c : list) std::printf("    %s [%s] %s %s\n", c.name.c_str(), c.kind.c_str(), c.launchPath.c_str(), c.args.c_str());
        CHECK(list.size() == 5);  // ID8, id4, twd, cruisnblast, hummer (ni désinstalleur, ni build_all, ni sauvegarde)
        auto id8 = find("Initial D Arcade Stage 8 Infinity");
        CHECK(id8 && id8->kind == "exe" && id8->launchPath.find("ID8.exe") != std::string::npos);
        auto twd = find("twd");
        CHECK(twd && twd->kind == "bat" && twd->runMode == RunMode::Normal);  // « pause » réelle détectée
        auto id4 = find("id4");
        CHECK(id4 && id4->runMode == RunMode::Auto);  // pas de pause (le commentaire ne compte pas)
        auto cb = find("cruisnblast");
        CHECK(cb && cb->kind == "linuxloader" && cb->runMode == RunMode::Hidden && cb->args.find("-g") != std::string::npos &&
              cb->launchPath.find("linuxloader.exe") != std::string::npos);
        auto hm = find("hummer");  // dossier « disk0 » générique : nom du dossier parent
        CHECK(hm && hm->kind == "linuxloader" && hm->args.find("hummer_Master.elf") != std::string::npos);
        CHECK(scanner::Alias("ID4") == "Initial D Arcade Stage 4" && scanner::Alias("twd") == "The Walking Dead" &&
              scanner::Alias("inconnu").empty());
        CHECK(scanner::CleanName("Mario Kart Arcade GP DX (1.18.16) (English Mod 1.0) (2013) [Namco ES3A] [TP]") ==
              "Mario Kart Arcade GP DX");
        CHECK(scanner::CleanName("Sega_World_Drivers v1.03.00") == "Sega World Drivers");
    }

    std::printf("- Mises à jour : comparaison de versions\n");
    CHECK(Updater::CompareVersions("0.6.1", "0.6.0") > 0 && Updater::CompareVersions("v0.6.0", "0.6.0") == 0);
    CHECK(Updater::CompareVersions("0.10.0", "0.9.9") > 0 && Updater::CompareVersions("0.6", "0.6.0") == 0);
    CHECK(Updater::CompareVersions("0.5.0 (phase 5)", "0.6.0") < 0 && Updater::CompareVersions("1.0.0-beta", "0.9.0") > 0);
    CHECK(!Updater::CanInstall(root));  // dossier sans datadir.txt : copie de développement

    logx::Shutdown();
    fs::remove_all(root, ec);
    std::printf("\n%d vérifications réussies, %d échec(s)\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
