#include "media/Zip.h"

#include <windows.h>
#include <zlib.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace {
uint16_t U16(const unsigned char* p) { return (uint16_t)(p[0] | p[1] << 8); }
uint32_t U32(const unsigned char* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
}  // namespace

bool ZipStreamEntry(const fs::path& zip, const std::string& entryName,
                    const std::function<bool(const char*, size_t)>& sink, std::string& err) {
    std::ifstream f(zip, std::ios::binary);
    if (!f) {
        err = "archive introuvable : " + util::PathU8(zip);
        return false;
    }
    f.seekg(0, std::ios::end);
    const int64_t size = (int64_t)f.tellg();

    // Fin du répertoire central : signature 0x06054b50 dans les 64 Ko + 22 derniers octets.
    const int64_t tail = std::min<int64_t>(size, 65557);
    std::vector<unsigned char> buf((size_t)tail);
    f.seekg(size - tail);
    f.read((char*)buf.data(), tail);
    int64_t eocd = -1;
    for (int64_t i = tail - 22; i >= 0; --i)
        if (U32(&buf[(size_t)i]) == 0x06054b50) {
            eocd = i;
            break;
        }
    if (eocd < 0) {
        err = "archive ZIP invalide (fin de répertoire absente)";
        return false;
    }
    const uint16_t count = U16(&buf[(size_t)eocd + 10]);
    const uint32_t cdSize = U32(&buf[(size_t)eocd + 12]), cdOffset = U32(&buf[(size_t)eocd + 16]);
    if (cdOffset == 0xFFFFFFFF || (int64_t)cdOffset + cdSize > size) {
        err = "archive ZIP non prise en charge (ZIP64 ?)";
        return false;
    }
    std::vector<unsigned char> cd(cdSize);
    f.seekg(cdOffset);
    f.read((char*)cd.data(), cdSize);

    // Recherche de l'entrée dans le répertoire central (tailles fiables, même avec descripteur de données).
    size_t pos = 0;
    uint32_t method = 0, csize = 0, local = 0;
    bool found = false;
    for (uint16_t i = 0; i < count && pos + 46 <= cd.size(); ++i) {
        const unsigned char* e = &cd[pos];
        if (U32(e) != 0x02014b50) break;
        uint16_t nlen = U16(e + 28), xlen = U16(e + 30), clen = U16(e + 32);
        if (pos + 46 + nlen > cd.size()) break;
        if (std::string((const char*)e + 46, nlen) == entryName) {
            method = U16(e + 10);
            csize = U32(e + 20);
            local = U32(e + 42);
            found = true;
            break;
        }
        pos += 46 + nlen + xlen + clen;
    }
    if (!found) {
        err = "« " + entryName + " » absent de l'archive";
        return false;
    }
    unsigned char lh[30];
    f.seekg(local);
    f.read((char*)lh, 30);
    if (!f || U32(lh) != 0x04034b50) {
        err = "archive ZIP endommagée";
        return false;
    }
    f.seekg((int64_t)local + 30 + U16(lh + 26) + U16(lh + 28));

    std::vector<char> in(1 << 20), out(1 << 20);
    uint64_t left = csize;
    if (method == 0) {  // stockée
        while (left) {
            size_t n = (size_t)std::min<uint64_t>(left, in.size());
            if (!f.read(in.data(), n)) break;
            left -= n;
            if (!sink(in.data(), n)) return false;
        }
        if (left) err = "archive tronquée";
        return left == 0;
    }
    if (method != 8) {
        err = "méthode de compression non prise en charge (" + std::to_string(method) + ")";
        return false;
    }
    z_stream z{};
    if (inflateInit2(&z, -MAX_WBITS) != Z_OK) {
        err = "décompression impossible";
        return false;
    }
    int ret = Z_OK;
    while (ret != Z_STREAM_END) {
        if (z.avail_in == 0) {
            if (!left) break;
            size_t n = (size_t)std::min<uint64_t>(left, in.size());
            if (!f.read(in.data(), n)) break;
            left -= n;
            z.next_in = (Bytef*)in.data();
            z.avail_in = (uInt)n;
        }
        z.next_out = (Bytef*)out.data();
        z.avail_out = (uInt)out.size();
        ret = inflate(&z, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) break;
        size_t have = out.size() - z.avail_out;
        if (have && !sink(out.data(), have)) {
            inflateEnd(&z);
            return false;
        }
    }
    inflateEnd(&z);
    if (ret != Z_STREAM_END) {
        err = "archive endommagée ou tronquée";
        return false;
    }
    return true;
}

bool ZipList(const fs::path& zip, std::vector<ZipEntryInfo>& out, std::string& err) {
    std::ifstream f(zip, std::ios::binary);
    if (!f) {
        err = "archive introuvable";
        return false;
    }
    f.seekg(0, std::ios::end);
    const int64_t size = (int64_t)f.tellg();
    const int64_t tail = std::min<int64_t>(size, 65557);
    std::vector<unsigned char> buf((size_t)tail);
    f.seekg(size - tail);
    f.read((char*)buf.data(), tail);
    int64_t eocd = -1;
    for (int64_t i = tail - 22; i >= 0; --i)
        if (U32(&buf[(size_t)i]) == 0x06054b50) {
            eocd = i;
            break;
        }
    if (eocd < 0) {
        err = "archive ZIP invalide";
        return false;
    }
    const uint16_t count = U16(&buf[(size_t)eocd + 10]);
    const uint32_t cdSize = U32(&buf[(size_t)eocd + 12]), cdOffset = U32(&buf[(size_t)eocd + 16]);
    if ((int64_t)cdOffset + cdSize > size) {
        err = "archive ZIP endommagée";
        return false;
    }
    std::vector<unsigned char> cd(cdSize);
    f.seekg(cdOffset);
    f.read((char*)cd.data(), cdSize);
    size_t pos = 0;
    for (uint16_t i = 0; i < count && pos + 46 <= cd.size(); ++i) {
        const unsigned char* e = &cd[pos];
        if (U32(e) != 0x02014b50) break;
        uint16_t nlen = U16(e + 28), xlen = U16(e + 30), clen = U16(e + 32);
        if (pos + 46 + nlen > cd.size()) break;
        std::string name((const char*)e + 46, nlen);
        if (!name.empty() && name.back() != '/') out.push_back({name, U32(e + 24)});
        pos += 46 + nlen + xlen + clen;
    }
    return true;
}

namespace {
void Put16(std::string& b, uint32_t v) { b += (char)(v & 0xFF); b += (char)(v >> 8 & 0xFF); }
void Put32(std::string& b, uint32_t v) { Put16(b, v & 0xFFFF); Put16(b, v >> 16); }
void DosTime(uint16_t& t, uint16_t& d) {
    SYSTEMTIME s;
    GetLocalTime(&s);
    t = (uint16_t)(s.wHour << 11 | s.wMinute << 5 | s.wSecond / 2);
    d = (uint16_t)((s.wYear - 1980) << 9 | s.wMonth << 5 | s.wDay);
}
}  // namespace

ZipWriter::~ZipWriter() {
    if (f_) fclose(f_);
}

bool ZipWriter::Open(const fs::path& file, std::string& err) {
    if (_wfopen_s(&f_, file.c_str(), L"wb") != 0 || !f_) {
        err = "création impossible : " + util::PathU8(file);
        return false;
    }
    return true;
}

bool ZipWriter::AddFile(const std::string& name, const fs::path& src, std::string& err) {
    std::ifstream in(src, std::ios::binary);
    if (!in) {
        err = "lecture impossible : " + util::PathU8(src);
        return false;
    }
    // Premier passage : taille et CRC (l'en-tête local les contient).
    std::vector<char> buf(1 << 20);
    uLong crc = crc32(0, nullptr, 0);
    uint64_t size = 0;
    while (in) {
        in.read(buf.data(), (std::streamsize)buf.size());
        std::streamsize n = in.gcount();
        if (n <= 0) break;
        crc = crc32(crc, (const Bytef*)buf.data(), (uInt)n);
        size += (uint64_t)n;
    }
    if (pos_ + size + 1024 > 0xFFFFFFFFull) {
        err = "sauvegarde trop volumineuse (plus de 4 Go)";
        return false;
    }
    uint16_t t, d;
    DosTime(t, d);
    std::string h;
    Put32(h, 0x04034b50);
    Put16(h, 20);
    Put16(h, 0x0800);  // noms en UTF-8
    Put16(h, 0);       // stockée
    Put16(h, t);
    Put16(h, d);
    Put32(h, (uint32_t)crc);
    Put32(h, (uint32_t)size);
    Put32(h, (uint32_t)size);
    Put16(h, (uint32_t)name.size());
    Put16(h, 0);
    h += name;
    Entry e{name, (uint32_t)crc, (uint32_t)size, (uint32_t)pos_};
    if (fwrite(h.data(), 1, h.size(), f_) != h.size()) {
        err = "écriture impossible (disque plein ?)";
        return false;
    }
    pos_ += h.size();
    in.clear();
    in.seekg(0);
    while (in) {
        in.read(buf.data(), (std::streamsize)buf.size());
        std::streamsize n = in.gcount();
        if (n <= 0) break;
        if (fwrite(buf.data(), 1, (size_t)n, f_) != (size_t)n) {
            err = "écriture impossible (disque plein ?)";
            return false;
        }
        pos_ += (uint64_t)n;
    }
    entries_.push_back(e);
    return true;
}

bool ZipWriter::Close(std::string& err) {
    if (!f_) return false;
    uint16_t t, d;
    DosTime(t, d);
    std::string cd;
    for (const Entry& e : entries_) {
        Put32(cd, 0x02014b50);
        Put16(cd, 20);
        Put16(cd, 20);
        Put16(cd, 0x0800);
        Put16(cd, 0);
        Put16(cd, t);
        Put16(cd, d);
        Put32(cd, e.crc);
        Put32(cd, e.size);
        Put32(cd, e.size);
        Put16(cd, (uint32_t)e.name.size());
        Put16(cd, 0);
        Put16(cd, 0);
        Put16(cd, 0);
        Put16(cd, 0);
        Put32(cd, 0);
        Put32(cd, e.offset);
        cd += e.name;
    }
    std::string end;
    Put32(end, 0x06054b50);
    Put16(end, 0);
    Put16(end, 0);
    Put16(end, (uint32_t)entries_.size());
    Put16(end, (uint32_t)entries_.size());
    Put32(end, (uint32_t)cd.size());
    Put32(end, (uint32_t)pos_);
    Put16(end, 0);
    bool ok = fwrite(cd.data(), 1, cd.size(), f_) == cd.size() && fwrite(end.data(), 1, end.size(), f_) == end.size();
    ok = fclose(f_) == 0 && ok;
    f_ = nullptr;
    if (!ok) err = "écriture impossible (disque plein ?)";
    return ok;
}
