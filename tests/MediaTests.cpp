// Tests de la récupération des médias.
//   media_tests.exe [port]   (serveur de simulation : python tools/mock_media_server.py <port> <video.mp4>)
// 1) Services réels sans identifiants : les erreurs doivent être comprises et lisibles.
// 2) Serveur de simulation : recherche, choix, téléchargement, fichiers écrits, métadonnées.
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include "core/Log.h"
#include "media/LaunchBox.h"
#include "media/Zip.h"
#include "media/Scraper.h"
#include "net/Http.h"
#include "video/Remux.h"
#include "video/CropDetect.h"
#include "media/YtVideo.h"

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
template <class F> auto WaitFor(F poll, int ms = 30000) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        if (auto r = poll()) return r;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return decltype(poll()){};
}
bool Exists(const fs::path& p) {
    std::error_code ec;
    return fs::is_regular_file(p, ec) && fs::file_size(p, ec) > 0;
}
}  // namespace

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    int port = argc > 1 ? std::atoi(argv[1]) : 8765;
    logx::Init(fs::temp_directory_path() / L"racecab-tests");
    std::string local = "http://127.0.0.1:" + std::to_string(port);

    std::printf("- Services réels sans identifiants (erreurs attendues)\n");
    {
        Scraper s;
        ScraperConfig c;
        c.sgdbKey = "cle-invalide";
        c.ssDevId = "invalide";
        c.ssDevPassword = "invalide";
        s.SetConfig(c);
        s.Search(1, "Initial D", Provider::SteamGridDB);
        auto r = WaitFor([&] { return s.PollSearch(); });
        CHECK(r && !r->ok);
        if (r) std::printf("    SteamGridDB : %s\n", r->error.c_str());
        CHECK(r && r->error.find("refus") != std::string::npos);
        s.Search(1, "Initial D", Provider::ScreenScraper);
        r = WaitFor([&] { return s.PollSearch(); });
        CHECK(r && !r->ok);
        if (r) std::printf("    ScreenScraper : %s\n", r->error.c_str());
        CHECK(r && r->error.find("identifiants") != std::string::npos);
    }

    std::printf("- Extrait vidéo sans réencodage (vidéo de test de 20 s)\n");
    {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        fs::path tool = fs::path(exe).parent_path() / L"make_test_video.exe";
        fs::path dir = fs::temp_directory_path() / L"racecab-remux-test";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        fs::path src = dir / L"source.mp4";
        std::wstring cmd = L"\"" + tool.wstring() + L"\" \"" + src.wstring() + L"\" 20 2";
        STARTUPINFOW si{sizeof(si)};
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(tool.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 60000);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
        CHECK(Exists(src));
        std::string err;
        CHECK(RemuxClip(src, {}, dir / L"clip.mp4", 3, 5, err));
        double d = MediaDuration(dir / L"clip.mp4");
        std::printf("    extrait 3 s + 5 s : %.2f s %s\n", d, err.c_str());
        CHECK(d > 4.8 && d < 5.3);  // coupe exacte (edit list), pas à l'image clé précédente
        // Durée qui dépasse la fin : le début demandé est gardé, l'extrait s'arrête à la fin (17 s -> 20 s).
        CHECK(RemuxClip(src, {}, dir / L"late.mp4", 17, 30, err));
        d = MediaDuration(dir / L"late.mp4");
        std::printf("    extrait 17 s + 30 s (vidéo de 20 s) : %.2f s\n", d);
        CHECK(d > 2.7 && d < 3.4);
        // Vidéo et son dans deux fichiers séparés (cas de yt-dlp).
        CHECK(RemuxClip(src, src, dir / L"two.mp4", 2, 4, err));
        CHECK(MediaDuration(dir / L"two.mp4") > 3.0);
        CHECK(!RemuxClip(dir / L"absent.mp4", {}, dir / L"x.mp4", 0, 5, err) && !err.empty());
        // Vidéo de test plein cadre (couleur unie + barre blanche) : aucune bande noire à retirer.
        CropRect cr;
        CHECK(DetectCrop(src, cr, err) && cr.Full());
        CHECK(!DetectCrop(dir / L"absent.mp4", cr, err));
        fs::remove_all(dir, ec);
    }

    std::printf("- Début d'extrait : saisie et lien YouTube\n");
    CHECK(ytvideo::ParseStart("90") == 90 && ytvideo::ParseStart("1:30") == 90 && ytvideo::ParseStart("1m30s") == 90);
    CHECK(ytvideo::ParseStart("1:02:03") == 3723 && ytvideo::ParseStart("") == -1 && ytvideo::ParseStart("abc") == -1);
    CHECK(ytvideo::StartFromUrl("https://youtu.be/abc?t=95") == 95);
    CHECK(ytvideo::StartFromUrl("https://www.youtube.com/watch?v=abc&t=2m5s") == 125);
    CHECK(ytvideo::StartFromUrl("https://www.youtube.com/watch?v=abc") == -1);

    std::printf("- Aucun identifiant : refus immédiat, sans appel réseau\n");
    {
        Scraper s;
        s.SetConfig(ScraperConfig{});
        s.Search(1, "x", Provider::SteamGridDB);
        auto r = WaitFor([&] { return s.PollSearch(); });
        CHECK(r && !r->ok && r->error.find("non renseign") != std::string::npos);
    }

    std::printf("- LaunchBox sans base téléchargée : message clair\n");
    {
        Scraper s;
        ScraperConfig c;
        c.lbDir = fs::temp_directory_path() / L"racecab-lb-absent";
        s.SetConfig(c);
        s.Search(1, "Rambo", Provider::LaunchBox);
        auto r = WaitFor([&] { return s.PollSearch(); });
        CHECK(r && !r->ok && r->error.find("LaunchBox") != std::string::npos);
    }

    if (!HttpGet(local + "/files/logo_101.png").ok()) {
        std::printf("\nServeur de simulation absent sur %s : tests locaux ignorés.\n", local.c_str());
    } else {
        fs::path dir = fs::temp_directory_path() / L"racecab-media-test";
        std::error_code ec;
        fs::remove_all(dir, ec);
        ScraperConfig c;
        c.sgdbKey = "test-key";
        c.ssDevId = c.ssDevPassword = "test";
        c.sgdbBase = local + "/api/v2";
        c.ssBase = local + "/api2/";
        Scraper s;
        s.SetConfig(c);

        std::printf("- SteamGridDB (simulation) : recherche puis logo + fond\n");
        s.Search(5, "Initial D Arcade Stage 8 Infinity", Provider::SteamGridDB);
        auto r = WaitFor([&] { return s.PollSearch(); });
        CHECK(r && r->ok && r->candidates.size() == 2);
        int exact = 0;
        if (r)
            for (auto& cand : r->candidates)
                exact += Scraper::Normalize(cand.name) == Scraper::Normalize("Initial D Arcade Stage 8 : Infinity");
        CHECK(exact == 1);  // la ponctuation n'empêche pas la correspondance exacte, mais « Stage 8 » seul ne correspond pas
        CHECK(r && r->candidates[0].year == "2014");
        if (r) s.Fetch(5, r->candidates[0], dir / L"sgdb", false);
        auto f = WaitFor([&] { return s.PollFetch(); });
        CHECK(f && f->ok);
        if (f) std::printf("    %s\n", f->message.c_str());
        CHECK(Exists(dir / L"sgdb" / L"logo.png"));
        CHECK(Exists(dir / L"sgdb" / L"fanart.jpg"));

        std::printf("- SteamGridDB : médias existants conservés sans « remplacer »\n");
        if (r) s.Fetch(5, r->candidates[0], dir / L"sgdb", false);
        f = WaitFor([&] { return s.PollFetch(); });
        CHECK(f && f->written.empty());

        std::printf("- ScreenScraper (simulation) : recherche, métadonnées, logo, fond, capture, vidéo\n");
        s.Search(7, "Mario Kart Arcade GP DX", Provider::ScreenScraper);
        r = WaitFor([&] { return s.PollSearch(); });
        CHECK(r && r->ok && r->candidates.size() == 2);
        const ScrapeCandidate* dx = nullptr;
        if (r)
            for (auto& cand : r->candidates)
                if (cand.name == "Mario Kart Arcade GP DX") dx = &cand;  // nom « wor » préféré au nom « ss »
        CHECK(dx && dx->year == "2013" && dx->platform == "Arcade");
        if (dx) s.Fetch(7, *dx, dir / L"ss", false);
        f = WaitFor([&] { return s.PollFetch(); }, 60000);
        CHECK(f && f->ok);
        if (f) std::printf("    %s\n", f->message.c_str());
        CHECK(Exists(dir / L"ss" / L"logo.png"));
        CHECK(Exists(dir / L"ss" / L"fanart.jpg"));
        CHECK(Exists(dir / L"ss" / L"screenshot.jpg"));
        bool video = Exists(dir / L"ss" / L"video.mp4");
        std::printf("    vidéo : %s\n", video ? "téléchargée" : "absente (serveur lancé sans fichier vidéo ?)");
        CHECK(f && f->meta.publisher == "Namco" && f->meta.year == 2013 && f->meta.genre == "Course" &&
              f->meta.description == "Synopsis de Mario Kart Arcade GP DX." && f->meta.platform == "Arcade");

        std::printf("- ScreenScraper : recherche limitée aux systèmes arcade (269, 75), comme RetroBat\n");
        s.Search(10, "Rambo", Provider::ScreenScraper);
        r = WaitFor([&] { return s.PollSearch(); });
        CHECK(r && r->ok && r->candidates.size() == 1 && r->candidates[0].id == "302" && r->candidates[0].autoOk);
        c.ssSystems = "269";  // aucun Rambo TeknoParrot : recherche libre, mais rien d'automatique
        s.SetConfig(c);
        s.Search(10, "Rambo", Provider::ScreenScraper);
        r = WaitFor([&] { return s.PollSearch(); });
        CHECK(r && r->ok && r->candidates.size() == 2);
        bool anyAuto = false;
        if (r)
            for (auto& cand : r->candidates) anyAuto |= cand.autoOk;
        CHECK(!anyAuto);
        c.ssSystems = "269,75";
        s.SetConfig(c);

        std::printf("- LaunchBox (simulation) : téléchargement de Metadata.zip et construction de l'index\n");
        c.lbDir = dir / L"lb";
        c.lbMetadataUrl = local + "/launchbox/Metadata.zip";
        c.lbImageBase = local + "/lbimages/";
        c.ytdlpPath = "C:\\inexistant\\yt-dlp.exe";  // vidéos : yt-dlp volontairement introuvable
        c.lbAutoVideo = true;
        s.SetConfig(c);
        CHECK(!s.LaunchBoxReady());
        s.UpdateLaunchBox(true);
        auto ready = WaitFor([&]() -> std::optional<bool> {
            if (s.LaunchBoxReady() && s.Pending() == 0) return true;
            return std::nullopt;
        });
        CHECK(ready.has_value());
        std::printf("    %s\n", s.LaunchBoxInfo().c_str());
        CHECK(s.LaunchBoxInfo().find("3 jeux (Arcade)") != std::string::npos);  // le Rambo Master System est écarté
        CHECK(s.LaunchBoxInfo().find("1 liens vidéo") != std::string::npos);
        CHECK(LaunchBoxDb::HasVideoLinks(c.lbDir / L"launchbox.db"));
        CHECK(Exists(c.lbDir / L"Metadata.zip") && !Exists(c.lbDir / L"Metadata.zip.part"));

        std::printf("- LaunchBox : nom alternatif = correspondance exacte\n");
        s.Search(1, "Initial D Arcade Stage 4", Provider::LaunchBox);
        r = WaitFor([&] { return s.PollSearch(); });
        CHECK(r && r->ok && r->candidates.size() == 1);
        if (r && !r->candidates.empty()) {
            const auto& c1 = r->candidates[0];
            CHECK(c1.name == "Initial D4" && c1.exact && c1.year == "2007" && c1.platform == "Arcade");
            CHECK(c1.note.find("Initial D Arcade Stage 4") != std::string::npos);
            s.Fetch(1, c1, dir / L"lb1", false);
            f = WaitFor([&] { return s.PollFetch(); });
            CHECK(f && f->ok);
            if (f) std::printf("    %s\n", f->message.c_str());
            CHECK(Exists(dir / L"lb1" / L"logo.png") && Exists(dir / L"lb1" / L"fanart.jpg") &&
                  Exists(dir / L"lb1" / L"screenshot.jpg"));
            CHECK(f && f->meta.publisher == "Sega" && f->meta.year == 2007 && f->meta.genre == "Racing");
            CHECK(f && f->message.find("yt-dlp non installé") != std::string::npos);  // lien trouvé, outil absent
        }

        std::printf("- LaunchBox : une seule correspondance exacte (Rambo III proposé, Rambo Master System absent)\n");
        s.Search(10, "Rambo", Provider::LaunchBox);
        r = WaitFor([&] { return s.PollSearch(); });
        int exactLb = 0;
        const ScrapeCandidate* rambo = nullptr;
        if (r)
            for (auto& cand : r->candidates)
                if (cand.exact) {
                    ++exactLb;
                    rambo = &cand;
                }
        CHECK(r && r->ok && r->candidates.size() == 2 && exactLb == 1);
        if (rambo) {
            s.Fetch(10, *rambo, dir / L"lb2", false);
            f = WaitFor([&] { return s.PollFetch(); });
            CHECK(f && f->meta.genre == "Shooter" && f->meta.year == 2008);
            CHECK(f && f->meta.description == "Overview & test \xC3\xA9t\xC3\xA9");  // entités XML décodées
            CHECK(f && f->message.find("logo") != std::string::npos && Exists(dir / L"lb2" / L"logo.png"));
            CHECK(f && f->message.find("pas de lien vidéo") != std::string::npos);
        }

        std::printf("- Cascade : LaunchBox (logo) puis ScreenScraper (fond, capture, vidéo, texte français)\n");
        {
            auto order = s.CascadeOrder();
            CHECK(order.size() == 3 && order[0] == Provider::LaunchBox && order[1] == Provider::ScreenScraper &&
                  order[2] == Provider::SteamGridDB);
            CompleteRequest req;
            req.gameId = 20;
            req.name = "Rambo";
            req.dir = dir / L"cascade";
            req.batch = true;
            s.Complete(req);
            auto cr = WaitFor([&] { return s.PollComplete(); }, 60000);
            CHECK(cr.has_value());
            if (cr) {
                std::printf("    %s\n", cr->fetch.message.c_str());
                CHECK(cr->Obtained() && cr->errors.empty() && cr->stillMissing.empty());
                CHECK(Exists(req.dir / L"logo.png") && Exists(req.dir / L"fanart.jpg") &&
                      Exists(req.dir / L"screenshot.jpg") && Exists(req.dir / L"video.mp4"));
                CHECK(cr->steps.size() == 2);  // SteamGridDB n'est pas interrogé : plus rien ne manque
                CHECK(cr->fetch.meta.genre == "Shooter");                       // LaunchBox, premier servi
                CHECK(cr->fetch.meta.description == "Synopsis de Rambo.");      // le français remplace l'anglais
            }

            std::printf("- Cascade : jeu déjà complet, aucune requête\n");
            req.current = {"Sega", "Tir", "Description", "Arcade", 2008, true};
            s.Complete(req);
            cr = WaitFor([&] { return s.PollComplete(); });
            CHECK(cr && cr->steps.empty() && cr->fetch.written.empty() && cr->fetch.message == "déjà complet");

            std::printf("- Cascade : aucun nom exact nulle part : rien téléchargé, propositions de toutes les sources\n");
            req.name = "Initial D Arcade";
            req.dir = dir / L"cascade2";
            req.current = {};
            s.Complete(req);
            cr = WaitFor([&] { return s.PollComplete(); }, 60000);
            CHECK(cr && !cr->Obtained() && cr->steps.size() == 3);
            if (cr) {
                std::printf("    %s\n", cr->fetch.message.c_str());
                bool lb = false, ss = false, sg = false;
                for (auto& cand : cr->toConfirm.candidates) {
                    lb |= cand.provider == Provider::LaunchBox;
                    ss |= cand.provider == Provider::ScreenScraper;
                    sg |= cand.provider == Provider::SteamGridDB;
                }
                CHECK(lb && ss && sg);
            }

            std::printf("- Cascade : identifiants ScreenScraper refusés : source écartée, les autres continuent\n");
            ScraperConfig bad = c;
            bad.ssDevPassword = "faux";
            s.SetConfig(bad);
            req.name = "Mario Kart Arcade GP DX";
            req.dir = dir / L"cascade3";
            s.Complete(req);
            cr = WaitFor([&] { return s.PollComplete(); }, 60000);
            CHECK(cr && !cr->errors.empty() && cr->Obtained());  // SteamGridDB a fourni logo et fond
            if (cr) std::printf("    %s\n", cr->fetch.message.c_str());
            auto after = s.CascadeOrder();
            CHECK(after.size() == 2 && std::find(after.begin(), after.end(), Provider::ScreenScraper) == after.end());
            s.SetConfig(c);  // nouveaux réglages : la source revient
            CHECK(s.CascadeOrder().size() == 3);
        }

        std::printf("- LaunchBox : mise à jour ratée (fichier non ZIP) : ancien index conservé\n");
        c.lbMetadataUrl = local + "/files/logo_101.png";
        s.SetConfig(c);
        s.UpdateLaunchBox(true);
        auto failed = WaitFor([&]() -> std::optional<bool> {
            if (s.LaunchBoxInfo().find("échec") != std::string::npos && s.Pending() == 0) return true;
            return std::nullopt;
        });
        CHECK(failed.has_value());
        std::printf("    %s\n", s.LaunchBoxInfo().c_str());
        CHECK(s.LaunchBoxReady());
        s.Search(1, "Initial D4", Provider::LaunchBox);
        r = WaitFor([&] { return s.PollSearch(); });
        CHECK(r && r->ok && r->candidates.size() == 1);

        std::printf("- Clé refusée (simulation) : message lisible\n");
        c.sgdbKey = "mauvaise";
        s.SetConfig(c);
        s.Search(5, "Initial D", Provider::SteamGridDB);
        r = WaitFor([&] { return s.PollSearch(); });
        CHECK(r && !r->ok && r->error.find("HTTP 401") != std::string::npos);
        fs::remove_all(dir, ec);
    }
    std::printf("\n%d vérifications réussies, %d échec(s)\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
