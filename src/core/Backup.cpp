#include "core/Backup.h"

#include <sqlite3.h>
#include <windows.h>

#include <algorithm>
#include <ctime>
#include <format>
#include <fstream>
#include <regex>

#include "core/Log.h"
#include "media/Zip.h"

namespace backup {
namespace {

std::string Stamp() {
    time_t now = time(nullptr);
    tm lt{};
    localtime_s(&lt, &now);
    char b[32];
    strftime(b, sizeof(b), "%Y-%m-%d_%H%M%S", &lt);
    return b;
}

// Copie cohérente de la base, même ouverte par le frontend (API de sauvegarde en ligne de SQLite).
bool CopyDatabase(const fs::path& src, const fs::path& dst, std::string& err) {
    sqlite3 *in = nullptr, *out = nullptr;
    bool ok = false;
    if (sqlite3_open_v2(util::PathU8(src).c_str(), &in, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK &&
        sqlite3_open_v2(util::PathU8(dst).c_str(), &out, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK) {
        if (sqlite3_backup* b = sqlite3_backup_init(out, "main", in, "main")) {
            ok = sqlite3_backup_step(b, -1) == SQLITE_DONE;
            sqlite3_backup_finish(b);
        }
    }
    if (!ok) err = std::string("copie de la base impossible : ") + (out ? sqlite3_errmsg(out) : "?");
    sqlite3_close(in);
    sqlite3_close(out);
    return ok;
}

bool AddTree(ZipWriter& z, const fs::path& root, const fs::path& dir, std::string& err) {
    std::error_code ec;
    if (!fs::exists(dir, ec)) return true;
    for (auto it = fs::recursive_directory_iterator(dir, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        fs::path rel = fs::relative(it->path(), root, ec);
        std::string name = util::PathU8(rel);
        std::replace(name.begin(), name.end(), '\\', '/');
        if (name.ends_with(".tmp") || name.ends_with(".part")) continue;
        if (!z.AddFile(name, it->path(), err)) return false;
    }
    return true;
}

// Un chemin d'archive est accepté seulement s'il reste dans les dossiers prévus (pas de « .. », pas de chemin absolu).
bool SafeEntry(const std::string& n) {
    if (n.empty() || n[0] == '/' || n[0] == '\\' || n.find(':') != std::string::npos || n.find("..") != std::string::npos)
        return false;
    for (const char* top : {"config/", "db/", "themes/", "media/"})
        if (n.rfind(top, 0) == 0) return true;
    return false;
}

}  // namespace

bool Create(const Paths& p, const std::string& prefix, bool includeMedia, std::string& outName, std::string& err) {
    std::error_code ec;
    fs::create_directories(p.BackupsDir(), ec);
    outName = prefix + "-" + Stamp() + ".zip";
    for (int n = 2; fs::exists(p.BackupsDir() / util::U8Path(outName), ec); ++n)  // deux sauvegardes dans la même seconde
        outName = prefix + "-" + Stamp() + "-" + std::to_string(n) + ".zip";
    fs::path file = p.BackupsDir() / util::U8Path(outName), part = file;
    part += L".part";
    fs::path dbCopy = p.BackupsDir() / L"library-copie.sqlite";
    fs::remove(dbCopy, ec);
    bool ok = CopyDatabase(p.DbFile(), dbCopy, err);
    if (ok) {
        ZipWriter z;
        ok = z.Open(part, err) && z.AddFile("db/library.sqlite", dbCopy, err) &&
             AddTree(z, p.dataDir, p.ConfigDir(), err) && AddTree(z, p.dataDir, p.ThemesDir(), err) &&
             (!includeMedia || AddTree(z, p.dataDir, p.MediaDir(), err)) && z.Close(err);
    }
    fs::remove(dbCopy, ec);
    if (ok && !MoveFileExW(part.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        ok = false;
        err = "enregistrement de la sauvegarde impossible";
    }
    if (!ok) {
        fs::remove(part, ec);
        logx::Error("Sauvegarde impossible : {}", err);
        return false;
    }
    logx::Info("Sauvegarde créée : {} ({} Ko)", outName, fs::file_size(file, ec) / 1024);
    return true;
}

std::vector<Info> List(const Paths& p) {
    std::vector<Info> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(p.BackupsDir(), ec)) {
        if (!e.is_regular_file(ec) || e.path().extension() != L".zip") continue;
        Info i;
        i.file = e.path();
        i.name = util::PathU8(e.path().filename());
        i.size = e.file_size(ec);
        i.automatic = i.name.rfind("auto-", 0) == 0;
        // Date lue dans le nom (…-AAAA-MM-JJ_HHMMSS[-n].zip) : indépendante des copies de fichiers.
        std::smatch m;
        if (std::regex_search(i.name, m, std::regex(R"((\d{4}-\d{2}-\d{2})_(\d{2})(\d{2})(\d{2}))"))) {
            i.date = m[1].str() + " " + m[2].str() + ":" + m[3].str();
            i.sortKey = m[0].str() + i.name;
        }
        out.push_back(i);
    }
    std::sort(out.begin(), out.end(), [](const Info& a, const Info& b) { return a.sortKey > b.sortKey; });
    return out;
}

void AutoIfDue(const Paths& p, int keep) {
    std::error_code ec;
    auto list = List(p);
    for (const Info& i : list) {
        if (!i.automatic) continue;
        auto age = fs::file_time_type::clock::now() - fs::last_write_time(i.file, ec);
        if (!ec && age < std::chrono::hours(20)) return;  // déjà faite récemment
        break;
    }
    std::string name, err;
    if (!Create(p, "auto", false, name, err)) return;
    int kept = 0;
    for (const Info& i : List(p))
        if (i.automatic && ++kept > std::max(1, keep)) {
            fs::remove(i.file, ec);
            logx::Info("Ancienne sauvegarde automatique supprimée : {}", i.name);
        }
}

bool ScheduleRestore(const Paths& p, const fs::path& zip, std::string& err) {
    std::vector<ZipEntryInfo> entries;
    if (!ZipList(zip, entries, err)) return false;
    bool hasDb = false;
    for (auto& e : entries) hasDb |= e.name == "db/library.sqlite";
    if (!hasDb) {
        err = "ce fichier n'est pas une sauvegarde du frontend (base absente)";
        return false;
    }
    std::error_code ec;
    fs::copy_file(zip, p.RestorePending(), fs::copy_options::overwrite_existing, ec);
    if (ec) {
        err = "préparation de la restauration impossible";
        return false;
    }
    logx::Info("Restauration programmée au prochain démarrage : {}", util::PathU8(zip.filename()));
    return true;
}

bool ApplyPendingRestore(const Paths& p, std::string& message) {
    std::error_code ec;
    fs::path pending = p.RestorePending();
    if (!fs::exists(pending, ec)) return false;
    std::string err, safety;
    // Filet de sécurité : l'état actuel est sauvegardé avant d'être remplacé.
    if (fs::exists(p.DbFile(), ec) && !Create(p, "avant-restauration", false, safety, err)) {
        message = "Restauration annulée : sauvegarde de l'état actuel impossible (" + err + ")";
        fs::remove(pending, ec);
        return true;
    }
    std::vector<ZipEntryInfo> entries;
    if (!ZipList(pending, entries, err)) {
        message = "Restauration impossible : " + err;
        fs::remove(pending, ec);
        return true;
    }
    int written = 0, skipped = 0;
    for (const ZipEntryInfo& e : entries) {
        if (!SafeEntry(e.name)) {
            ++skipped;
            continue;
        }
        fs::path dst = p.dataDir / util::U8Path(e.name), tmp = dst;
        tmp += L".restore";
        fs::create_directories(dst.parent_path(), ec);
        bool ok;
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            ok = out && ZipStreamEntry(pending, e.name, [&](const char* d, size_t n) {
                     return (bool)out.write(d, (std::streamsize)n);
                 }, err);
        }
        if (ok && MoveFileExW(tmp.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING)) ++written;
        else {
            fs::remove(tmp, ec);
            ++skipped;
        }
    }
    fs::remove(pending, ec);
    message = std::format("Configuration restaurée ({} fichier(s)). État précédent sauvegardé dans {}", written, safety);
    if (skipped) message += std::format(" ; {} fichier(s) ignoré(s)", skipped);
    return true;
}

}  // namespace backup
