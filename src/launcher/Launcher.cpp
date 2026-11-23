#include "launcher/Launcher.h"

#include <windows.h>
#include <tlhelp32.h>

#include <chrono>
#include <format>

#include "core/Log.h"
#include "core/Util.h"

namespace {

struct Handle {
    HANDLE h = nullptr;
    Handle() = default;
    explicit Handle(HANDLE v) : h(v) {}
    ~Handle() {
        if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    operator HANDLE() const { return h; }
};

std::wstring ComSpec() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"ComSpec", buf, MAX_PATH);
    if (n && n < MAX_PATH) return buf;
    GetSystemDirectoryW(buf, MAX_PATH);
    return std::wstring(buf) + L"\\cmd.exe";
}

std::string LastErrorText(DWORD err = GetLastError()) {
    wchar_t* msg = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                   err, 0, (LPWSTR)&msg, 0, nullptr);
    std::string s = msg ? util::Trim(util::Narrow(msg)) : "";
    LocalFree(msg);
    return std::format("{} (code {})", s, err);
}

bool ProcessRunning(const std::wstring& exeName) {
    Handle snap(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (snap.h == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe{sizeof(pe)};
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
        if (_wcsicmp(pe.szExeFile, exeName.c_str()) == 0) return true;
    return false;
}

}  // namespace

const char* PhaseLabel(Launcher::Phase p) {
    switch (p) {
        case Launcher::Phase::Idle: return "";
        case Launcher::Phase::PreCommand: return "Préparation…";
        case Launcher::Phase::Delay: return "Lancement…";
        case Launcher::Phase::Starting: return "Lancement…";
        case Launcher::Phase::Running: return "Chargement du jeu…";
        case Launcher::Phase::Watching: return "Chargement du jeu…";
        case Launcher::Phase::PostCommand: return "Retour au frontend…";
        case Launcher::Phase::Done: return "Retour au frontend…";
    }
    return "";
}

Launcher::~Launcher() {
    // On n'interrompt pas un jeu en cours : on attend simplement la fin du thread de supervision.
    if (thread_.joinable()) thread_.join();
}

void Launcher::Start(const Game& g) {
    if (Busy()) return;
    if (thread_.joinable()) thread_.join();
    {
        std::lock_guard lk(m_);
        result_.reset();
    }
    {
        std::lock_guard lk(pm_);
        pids_.clear();
        watch_ = util::Widen(util::Trim(g.watchProcess));
    }
    phase_ = Phase::Starting;
    thread_ = std::thread([this, g] { Run(g); });
}

std::vector<uint32_t> Launcher::Pids() const {
    std::lock_guard lk(pm_);
    return pids_;
}

std::wstring Launcher::WatchName() const {
    std::lock_guard lk(pm_);
    return watch_;
}

std::optional<LaunchResult> Launcher::Poll() {
    std::lock_guard lk(m_);
    if (!result_) return std::nullopt;
    auto r = std::move(result_);
    result_.reset();
    phase_ = Phase::Idle;
    return r;
}

bool Launcher::RunCommandAndWait(const std::string& cmd, const std::string& workDir, uint32_t timeoutMs) {
    std::wstring line = L"\"" + ComSpec() + L"\" /d /c \"" + util::Widen(cmd) + L"\"";
    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::wstring wd = util::Widen(workDir);
    if (!CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                        wd.empty() ? nullptr : wd.c_str(), &si, &pi)) {
        logx::Error("Commande impossible « {} » : {}", cmd, LastErrorText());
        return false;
    }
    Handle proc(pi.hProcess), thr(pi.hThread);
    if (WaitForSingleObject(proc, timeoutMs) == WAIT_TIMEOUT) {
        logx::Warn("Commande « {} » toujours active après {} s : on continue sans l'attendre", cmd, timeoutMs / 1000);
        return false;
    }
    DWORD code = 0;
    GetExitCodeProcess(proc, &code);
    logx::Info("Commande « {} » terminée (code {})", cmd, code);
    return code == 0;
}

void Launcher::Run(Game g) {
    LaunchResult res;
    res.gameId = g.id;
    auto finish = [&] {
        phase_ = Phase::Done;
        std::lock_guard lk(m_);
        result_ = res;
    };

    fs::path target = util::U8Path(g.launchPath);
    std::error_code ec;
    if (g.launchPath.empty() || !fs::is_regular_file(target, ec)) {
        res.error = "Fichier de lancement introuvable : " + g.launchPath;
        logx::Error("{} [{}]", res.error, g.name);
        return finish();
    }
    std::string workDir = g.workDir.empty() ? util::PathU8(target.parent_path()) : g.workDir;
    if (!fs::is_directory(util::U8Path(workDir), ec)) {
        res.error = "Répertoire de travail introuvable : " + workDir;
        logx::Error("{} [{}]", res.error, g.name);
        return finish();
    }
    logx::Info("Lancement de « {} » : {} {} (dossier : {})", g.name, g.launchPath, g.args, workDir);

    if (!g.preCmd.empty()) {
        phase_ = Phase::PreCommand;
        RunCommandAndWait(g.preCmd, workDir, 60000);
    }
    if (g.launchDelayMs > 0) {
        phase_ = Phase::Delay;
        Sleep((DWORD)g.launchDelayMs);
    }
    phase_ = Phase::Starting;

    // Ligne de commande
    std::string ext = util::ToLower(util::PathU8(target.extension()));
    bool isScript = ext == ".bat" || ext == ".cmd";
    std::wstring app, line;
    if (isScript) {
        app = ComSpec();
        // cmd /c ""chemin du script" args" : les guillemets externes sont retirés par cmd.
        line = L"\"" + app + L"\" /d /c \"\"" + target.wstring() + L"\" " + util::Widen(g.args) + L"\"";
    } else {
        app = target.wstring();
        line = L"\"" + app + L"\" " + util::Widen(g.args);
    }

    RunMode mode = g.runMode;
    // .bat / .cmd : console masquée par défaut (l'écran de chargement du frontend reste affiché
    // jusqu'à l'apparition de la vraie fenêtre du jeu). Les programmes console lancés par le script
    // fonctionnent toujours : ils héritent de cette console invisible.
    if (mode == RunMode::Auto) mode = isScript ? RunMode::Hidden : RunMode::Normal;
    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = mode == RunMode::Hidden ? SW_HIDE : mode == RunMode::Minimized ? SW_SHOWMINNOACTIVE : SW_SHOWNORMAL;
    DWORD flags = CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT;
    if (isScript) flags |= (mode == RunMode::Hidden ? CREATE_NO_WINDOW : CREATE_NEW_CONSOLE);

    // Job Object + port de complétion pour être notifié quand le dernier processus se termine.
    Handle job(CreateJobObjectW(nullptr, nullptr));
    Handle port(CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1));
    bool jobOk = job.h && port.h;
    if (jobOk) {
        JOBOBJECT_ASSOCIATE_COMPLETION_PORT acp{};
        acp.CompletionKey = job.h;
        acp.CompletionPort = port.h;
        jobOk = SetInformationJobObject(job, JobObjectAssociateCompletionPortInformation, &acp, sizeof(acp)) != 0;
    }
    if (!jobOk) logx::Warn("Job Object indisponible ({}) : suivi du seul processus principal", LastErrorText());

    PROCESS_INFORMATION pi{};
    std::wstring wd = util::Widen(workDir);
    if (!CreateProcessW(app.c_str(), line.data(), nullptr, nullptr, FALSE, flags, nullptr, wd.c_str(), &si, &pi)) {
        res.error = "Échec du lancement : " + LastErrorText();
        logx::Error("{} [{}]", res.error, g.name);
        return finish();
    }
    Handle proc(pi.hProcess), thr(pi.hThread);
    {
        std::lock_guard lk(pm_);
        pids_.push_back(pi.dwProcessId);
    }
    if (jobOk && !AssignProcessToJobObject(job, proc)) {
        logx::Warn("Impossible d'attacher le jeu au Job ({}) : suivi du seul processus principal", LastErrorText());
        jobOk = false;
    }
    ResumeThread(thr);
    phase_ = Phase::Running;
    auto t0 = std::chrono::steady_clock::now();
    logx::Info("Jeu démarré (pid {}), supervision {}", pi.dwProcessId, jobOk ? "par Job Object" : "simple");

    if (jobOk) {
        DWORD code;
        ULONG_PTR key;
        LPOVERLAPPED ov;
        for (;;) {
            if (!GetQueuedCompletionStatus(port, &code, &key, &ov, INFINITE)) break;
            if ((HANDLE)key != job.h) continue;
            if (code == JOB_OBJECT_MSG_NEW_PROCESS) {
                logx::Debug("Nouveau processus dans le Job : pid {}", (DWORD)(ULONG_PTR)ov);
                std::lock_guard lk(pm_);
                pids_.push_back((DWORD)(ULONG_PTR)ov);
            }
            if (code == JOB_OBJECT_MSG_EXIT_PROCESS || code == JOB_OBJECT_MSG_ABNORMAL_EXIT_PROCESS)
                logx::Debug("Processus terminé dans le Job : pid {}", (DWORD)(ULONG_PTR)ov);
            if (code == JOB_OBJECT_MSG_ACTIVE_PROCESS_ZERO) break;
        }
    } else {
        WaitForSingleObject(proc, INFINITE);
    }
    DWORD exitCode = 0;
    GetExitCodeProcess(proc, &exitCode);
    res.exitCode = exitCode;

    // Lanceurs qui délèguent hors du Job : on surveille le processus indiqué.
    if (!g.watchProcess.empty()) {
        phase_ = Phase::Watching;
        std::wstring name = util::Widen(util::Trim(g.watchProcess));
        logx::Info("Surveillance de {}", g.watchProcess);
        auto waitStart = std::chrono::steady_clock::now();
        bool seen = false;
        for (;;) {
            bool running = ProcessRunning(name);
            if (running) seen = true;
            if (seen && !running) break;
            if (!seen && std::chrono::steady_clock::now() - waitStart > std::chrono::seconds(30)) {
                logx::Warn("{} n'est jamais apparu en 30 s", g.watchProcess);
                break;
            }
            Sleep(1000);
        }
    }

    res.seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - t0).count();
    logx::Info("Fin de « {} » après {} (code de sortie du processus principal : {})", g.name,
               util::FormatDuration(res.seconds), exitCode);

    if (!g.postCmd.empty()) {
        phase_ = Phase::PostCommand;
        RunCommandAndWait(g.postCmd, workDir, 60000);
    }
    res.ok = true;
    finish();
}
