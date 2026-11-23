#pragma once
#include <functional>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "core/Util.h"

// Lecture en flux d'une entrée d'archive ZIP (méthodes « stockée » et « deflate », sans ZIP64 : archives < 4 Go).
// Le contenu décompressé est passé par blocs à sink (false = arrêt) : rien n'est chargé entièrement en mémoire.
bool ZipStreamEntry(const fs::path& zip, const std::string& entryName,
                    const std::function<bool(const char* data, size_t size)>& sink, std::string& err);

struct ZipEntryInfo {
    std::string name;
    uint64_t size = 0;
};
// Liste des entrées (fichiers) d'une archive.
bool ZipList(const fs::path& zip, std::vector<ZipEntryInfo>& out, std::string& err);

// Écriture d'une archive ZIP sans compression (« stockée ») : sûre et lisible partout (Explorateur Windows).
// Limite : 4 Go au total (pas de ZIP64).
class ZipWriter {
public:
    bool Open(const fs::path& file, std::string& err);
    bool AddFile(const std::string& name, const fs::path& src, std::string& err);  // name : chemin avec « / »
    bool Close(std::string& err);  // écrit le répertoire central
    ~ZipWriter();

private:
    struct Entry {
        std::string name;
        uint32_t crc = 0, size = 0, offset = 0;
    };
    FILE* f_ = nullptr;
    std::vector<Entry> entries_;
    uint64_t pos_ = 0;
};
