#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "core/Util.h"

// Client HTTP(S) minimal basé sur WinHTTP (natif Windows : aucune dépendance).
// Appels bloquants : à utiliser uniquement depuis un thread de travail.
struct HttpResponse {
    int status = 0;            // 0 = échec réseau
    std::string body;          // octets bruts
    std::string contentType;
    std::string error;         // message lisible si status == 0
    bool ok() const { return status >= 200 && status < 300; }
};

HttpResponse HttpGet(const std::string& url, const std::vector<std::string>& headers = {}, int timeoutMs = 20000,
                     size_t maxBytes = 200 * 1024 * 1024, const std::atomic<bool>* cancel = nullptr);

// Téléchargement en flux vers un fichier (écrit sous <fichier>.part puis renommé : jamais de fichier partiel).
// progress(reçu, total) ; total = 0 si la taille est inconnue.
HttpResponse HttpDownload(const std::string& url, const fs::path& file, const std::atomic<bool>* cancel = nullptr,
                          const std::function<void(uint64_t, uint64_t)>& progress = {});

std::string UrlEncode(const std::string& s);
