/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <officelabs/BundledAgent.hxx>

#include <sal/log.hxx>

#include <cctype>
#include <cerrno>
#include <cstdlib>

#ifdef MACOSX
#include <mutex>

#include <rtl/bootstrap.hxx>
#include <rtl/string.hxx>
#include <rtl/ustring.hxx>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

namespace officelabs {

namespace {

std::string_view trim(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}

bool isValidKey(std::string_view k)
{
    if (k.empty() || !(std::isalpha(static_cast<unsigned char>(k[0])) || k[0] == '_'))
        return false;
    for (char c : k)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_'))
            return false;
    return true;
}

} // namespace

void parseConfig(std::string_view text, std::map<std::string, std::string>& io)
{
    while (!text.empty())
    {
        const auto nl = text.find('\n');
        std::string_view line = text.substr(0, nl);
        text = (nl == std::string_view::npos) ? std::string_view() : text.substr(nl + 1);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        line = trim(line);
        if (line.empty() || line.front() == '#')
            continue;
        const auto eq = line.find('=');
        if (eq == std::string_view::npos)
            continue;
        const std::string_view key = trim(line.substr(0, eq));
        if (!isValidKey(key))
            continue;
        const std::string_view value = line.substr(eq + 1);
        std::size_t start = 0;
        while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])))
            ++start;
        io[std::string(key)] = std::string(value.substr(start));
    }
}

PortDecision decide(bool portInUse, bool healthIsOfficeLabsAgent)
{
    if (!portInUse)
        return PortDecision::Spawn;
    return healthIsOfficeLabsAgent ? PortDecision::Reuse : PortDecision::Blocked;
}

std::vector<std::string> buildEnv(const std::map<std::string, std::string>& cfg)
{
    std::map<std::string, std::string> env{
        { "LLM_COMPLETION_MODEL", "officelabs-inline" },
        { "OFFICELABS_CHANNEL", "development" },
        { "SKIP_UNO", "1" },
    };
    for (const auto& [k, v] : cfg)
        env[k] = v;
    if (const char* home = std::getenv("HOME"))
        env["HOME"] = home;
    env["PATH"] = "/usr/bin:/bin";

    std::vector<std::string> out;
    for (const auto& [k, v] : env)
        out.push_back(k + "=" + v);
    return out;
}

namespace BundledAgent {

#ifdef MACOSX

namespace {

const int AGENT_PORT = 8766;
pid_t g_nSpawnedPid = 0;
std::mutex g_aMutex;
std::once_flag g_aOnce;

bool fileExists(const std::string& path)
{
    struct stat st;
    return ::stat(path.c_str(), &st) == 0;
}

std::string readFile(const std::string& path)
{
    std::string out;
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0)
        return out;
    char buf[4096];
    ssize_t n;
    while ((n = ::read(fd, buf, sizeof buf)) > 0)
        out.append(buf, static_cast<std::size_t>(n));
    ::close(fd);
    return out;
}

void mkdirp(const std::string& path)
{
    std::size_t pos = 0;
    while ((pos = path.find('/', pos + 1)) != std::string::npos)
        ::mkdir(path.substr(0, pos).c_str(), 0700);
    ::mkdir(path.c_str(), 0700);
}

/// Connects to 127.0.0.1:8766 with a short timeout; returns the fd or -1.
int connectLocal(int nTimeoutMs)
{
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(AGENT_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int rc = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
    if (rc != 0 && errno == EINPROGRESS)
    {
        pollfd p{ fd, POLLOUT, 0 };
        int err = 0;
        socklen_t len = sizeof err;
        if (::poll(&p, 1, nTimeoutMs) == 1 && ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0
            && err == 0)
            rc = 0;
    }
    if (rc != 0)
    {
        ::close(fd);
        return -1;
    }
    ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) & ~O_NONBLOCK);
    timeval tv{ 1, 0 };
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    return fd;
}

bool isHealthyOfficeLabsAgent()
{
    int fd = connectLocal(300);
    if (fd < 0)
        return false;
    const char req[] = "GET /health HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n";
    std::string resp;
    if (::send(fd, req, sizeof req - 1, 0) > 0)
    {
        char buf[1024];
        ssize_t n;
        while ((n = ::recv(fd, buf, sizeof buf, 0)) > 0 && resp.size() < 8192)
            resp.append(buf, static_cast<std::size_t>(n));
    }
    ::close(fd);
    const bool bHttp = resp.compare(0, 5, "HTTP/") == 0 && resp.size() > 12;
    return bHttp && resp.compare(9, 3, "200") == 0 && resp.find("healthy") != std::string::npos;
}

bool isPortInUse()
{
    int fd = connectLocal(300);
    if (fd < 0)
        return false;
    ::close(fd);
    return true;
}

void doStart()
{
    OUString aBase(u"$BRAND_BASE_DIR"_ustr);
    rtl::Bootstrap::expandMacros(aBase);
    OString aBaseUtf8 = OUStringToOString(aBase, RTL_TEXTENCODING_UTF8);
    std::string base(aBaseUtf8.getStr());
    if (base.compare(0, 7, "file://") == 0)
        base.erase(0, 7);
    SAL_INFO("officelabs.agent", "BRAND_BASE_DIR=" << base);

    const std::string services = base + "/Resources/officelabs-services";
    const std::string python = services + "/python/bin/python3";
    const std::string agentDir = services + "/agent";
    if (!fileExists(python))
        return;

    const char* pHome = std::getenv("HOME");
    const std::string home = pHome ? pHome : "";

    std::map<std::string, std::string> cfg;
    parseConfig(readFile(services + "/officelabs.config"), cfg);
    if (!home.empty())
        parseConfig(readFile(home + "/Library/Application Support/OfficeLabs/officelabs.config"),
                    cfg);

    const bool bInUse = isPortInUse();
    const PortDecision eDecision = decide(bInUse, bInUse && isHealthyOfficeLabsAgent());
    if (eDecision == PortDecision::Reuse)
    {
        SAL_INFO("officelabs.agent", "agent already healthy on :" << AGENT_PORT << ", reusing");
        return;
    }
    if (eDecision == PortDecision::Blocked)
    {
        SAL_WARN("officelabs.agent",
                 "port " << AGENT_PORT << " is held by a process that is not the OfficeLabs agent");
        return;
    }

    const std::string logDir = home + "/Library/Logs/OfficeLabs";
    mkdirp(logDir);
    const std::string logFile = logDir + "/agent.log";

    std::vector<std::string> envStrs = buildEnv(cfg);
    std::vector<char*> envp;
    for (auto& s : envStrs)
        envp.push_back(s.data());
    envp.push_back(nullptr);

    std::vector<std::string> argStrs{ python,    "-m",     "uvicorn", "src.main:app",
                                      "--app-dir", agentDir, "--host",  "127.0.0.1",
                                      "--port",  std::to_string(AGENT_PORT),
                                      "--log-level", "info" };
    std::vector<char*> argv;
    for (auto& s : argStrs)
        argv.push_back(s.data());
    argv.push_back(nullptr);

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addchdir_np(&fa, agentDir.c_str());
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 1, logFile.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
    posix_spawn_file_actions_adddup2(&fa, 1, 2);

    pid_t pid = 0;
    int rc = ::posix_spawn(&pid, python.c_str(), &fa, nullptr, argv.data(), envp.data());
    posix_spawn_file_actions_destroy(&fa);
    if (rc != 0)
    {
        SAL_WARN("officelabs.agent", "posix_spawn failed: " << rc);
        return;
    }
    std::lock_guard<std::mutex> lock(g_aMutex);
    g_nSpawnedPid = pid;
    SAL_INFO("officelabs.agent", "spawned bundled agent pid " << pid);
}

} // namespace

void ensureStarted()
{
    std::call_once(g_aOnce, doStart);
}

void stop()
{
    pid_t pid;
    {
        std::lock_guard<std::mutex> lock(g_aMutex);
        pid = g_nSpawnedPid;
        g_nSpawnedPid = 0;
    }
    if (pid <= 0)
        return;
    ::kill(pid, SIGTERM);
    for (int i = 0; i < 30; ++i)
    {
        int status = 0;
        if (::waitpid(pid, &status, WNOHANG) == pid)
            return;
        ::usleep(100 * 1000);
    }
    SAL_WARN("officelabs.agent", "agent ignored SIGTERM, sending SIGKILL");
    ::kill(pid, SIGKILL);
    int status = 0;
    ::waitpid(pid, &status, 0);
}

#else

void ensureStarted() {}
void stop() {}

#endif

} // namespace BundledAgent

} // namespace officelabs

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
