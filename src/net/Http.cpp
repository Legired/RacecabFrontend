#include "net/Http.h"

#include <windows.h>
#include <winhttp.h>

#include <format>
#include <fstream>

#include "core/Util.h"

namespace {
struct HInternet {
    HINTERNET h = nullptr;
    ~HInternet() {
        if (h) WinHttpCloseHandle(h);
    }
};
std::string WinErr(const char* what) { return std::format("{} (erreur Windows {})", what, GetLastError()); }
}  // namespace

std::string UrlEncode(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += (char)c;
        else out += std::format("%{:02X}", c);
    }
    return out;
}

namespace {
// Cœur commun : chaque bloc reçu est passé à onData (false = arrêt). total = Content-Length (0 si inconnu).
HttpResponse Perform(const std::string& url, const std::vector<std::string>& headers, int timeoutMs,
                     const std::atomic<bool>* cancel, const std::function<bool(const char*, size_t, uint64_t)>& onData) {
    HttpResponse r;
    std::wstring wurl = util::Widen(url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256], path[8192], extra[8192];
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 8192;
    uc.lpszExtraInfo = extra;
    uc.dwExtraInfoLength = 8192;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        r.error = "Adresse invalide";
        return r;
    }
    std::wstring target = std::wstring(path, uc.dwUrlPathLength) + std::wstring(extra, uc.dwExtraInfoLength);

    HInternet session, connect, request;
    // Agent générique : aucune information personnelle n'est transmise.
    session.h = WinHttpOpen(L"RacecabFrontend/0.5", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                            WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session.h) {
        r.error = WinErr("Initialisation réseau impossible");
        return r;
    }
    WinHttpSetTimeouts(session.h, timeoutMs, timeoutMs, timeoutMs, timeoutMs);
    DWORD decomp = WINHTTP_DECOMPRESSION_FLAG_ALL;
    WinHttpSetOption(session.h, WINHTTP_OPTION_DECOMPRESSION, &decomp, sizeof(decomp));

    connect.h = WinHttpConnect(session.h, std::wstring(host, uc.dwHostNameLength).c_str(), uc.nPort, 0);
    if (!connect.h) {
        r.error = WinErr("Connexion impossible");
        return r;
    }
    request.h = WinHttpOpenRequest(connect.h, L"GET", target.c_str(), nullptr, WINHTTP_NO_REFERER,
                                   WINHTTP_DEFAULT_ACCEPT_TYPES, uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (!request.h) {
        r.error = WinErr("Requête impossible");
        return r;
    }
    std::wstring hdr;
    for (const std::string& h : headers) hdr += util::Widen(h) + L"\r\n";
    if (!hdr.empty()) WinHttpAddRequestHeaders(request.h, hdr.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);

    if (!WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.h, nullptr)) {
        r.error = WinErr("Pas de réponse du serveur (connexion Internet ?)");
        return r;
    }
    DWORD status = 0, len = sizeof(status);
    WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &len, WINHTTP_NO_HEADER_INDEX);
    wchar_t ctype[256];
    len = sizeof(ctype);
    if (WinHttpQueryHeaders(request.h, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX, ctype, &len,
                            WINHTTP_NO_HEADER_INDEX))
        r.contentType = util::Narrow(ctype);
    wchar_t clen[64];
    len = sizeof(clen);
    uint64_t total = 0;
    if (WinHttpQueryHeaders(request.h, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, clen, &len,
                            WINHTTP_NO_HEADER_INDEX))
        total = _wcstoui64(clen, nullptr, 10);

    std::vector<char> buf;
    for (;;) {
        if (cancel && cancel->load()) {
            r.error = "Annulé";
            return r;
        }
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(request.h, &avail)) {
            r.error = WinErr("Téléchargement interrompu");
            return r;
        }
        if (avail == 0) break;
        buf.resize(avail);
        DWORD read = 0;
        if (!WinHttpReadData(request.h, buf.data(), avail, &read)) {
            r.error = WinErr("Lecture interrompue");
            return r;
        }
        // Les pages d'erreur sont gardées en mémoire (texte lisible) ; seules les réponses 2xx vont au destinataire.
        if (status >= 200 && status < 300) {
            if (!onData(buf.data(), read, total)) {
                if (r.error.empty()) r.error = "Téléchargement interrompu";
                return r;
            }
        } else if (r.body.size() < 4096) {
            r.body.append(buf.data(), read);
        }
    }
    r.status = (int)status;
    return r;
}
}  // namespace

HttpResponse HttpGet(const std::string& url, const std::vector<std::string>& headers, int timeoutMs, size_t maxBytes,
                     const std::atomic<bool>* cancel) {
    std::string body;
    bool tooBig = false;
    HttpResponse r = Perform(url, headers, timeoutMs, cancel, [&](const char* p, size_t n, uint64_t) {
        if (body.size() + n > maxBytes) {
            tooBig = true;
            return false;
        }
        body.append(p, n);
        return true;
    });
    if (tooBig) r.error = "Fichier trop volumineux";
    if (r.ok()) r.body = std::move(body);
    return r;
}

HttpResponse HttpDownload(const std::string& url, const fs::path& file, const std::atomic<bool>* cancel,
                          const std::function<void(uint64_t, uint64_t)>& progress) {
    fs::path tmp = file;
    tmp += L".part";
    HttpResponse r;
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            r.error = "écriture impossible : " + util::PathU8(tmp);
            return r;
        }
        uint64_t got = 0;
        r = Perform(url, {}, 60000, cancel, [&](const char* p, size_t n, uint64_t total) {
            if (!f.write(p, (std::streamsize)n)) return false;
            got += n;
            if (progress) progress(got, total);
            return true;
        });
        if (r.ok() && !f.flush()) {
            r.status = 0;
            r.error = "écriture impossible (disque plein ?)";
        }
    }
    std::error_code ec;
    if (!r.ok()) {
        fs::remove(tmp, ec);
        return r;
    }
    if (!MoveFileExW(tmp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        r.status = 0;
        r.error = "remplacement impossible : " + util::PathU8(file);
        fs::remove(tmp, ec);
    }
    return r;
}
