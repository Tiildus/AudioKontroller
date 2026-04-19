// =============================================================================
// DiscordIPC.cpp — Discord local IPC client (see header for protocol notes)
// =============================================================================

#include "DiscordIPC.h"
#include "Logger.h"
#include "Util.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <random>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#include <QByteArray>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QString>
#include <QUrl>
#include <QUrlQuery>

static constexpr uint32_t OP_HANDSHAKE = 0;
static constexpr uint32_t OP_FRAME     = 1;
static constexpr uint32_t OP_CLOSE     = 2;

// Maximum body size we'll accept on a single frame. The protocol allows up
// to 2^32-1, but voice settings payloads are well under 16 KiB. Capping
// guards against a runaway/malformed length field eating memory.
static constexpr uint32_t MAX_FRAME_BYTES = 64 * 1024;

// Socket timeout for normal commands (handshake, authenticate, get/set voice
// settings). If Discord stops responding mid-command, recv() unblocks with
// EAGAIN/EWOULDBLOCK so the HID thread can't get permanently stuck. The
// AUTHORIZE flow temporarily clears the timeout because the user has to
// click a popup inside Discord, which can legitimately take much longer.
static constexpr int SOCKET_TIMEOUT_SEC = 5;

// Discord's OAuth token endpoint and the redirect URI we register in our
// developer application settings. For pure RPC apps (no browser redirect)
// this URI is conventional — Discord just echoes it back.
static constexpr const char* TOKEN_URL    = "https://discord.com/api/v10/oauth2/token";
static constexpr const char* REDIRECT_URI = "http://127.0.0.1";

// Writes exactly `len` bytes to `fd`, looping over partial writes and retrying
// EINTR. Returns false on any unrecoverable error or if the peer closed.
static bool writeAll(int fd, const void* buf, size_t len) {
    const char* p = static_cast<const char*>(buf);
    while (len > 0) {
        ssize_t n = ::write(fd, p, len);
        if (n > 0) {
            p   += n;
            len -= static_cast<size_t>(n);
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

// Reads exactly `len` bytes from `fd`, looping over short reads and retrying
// EINTR. Returns false on any error, peer-close, or recv timeout (EAGAIN).
static bool readAll(int fd, void* buf, size_t len) {
    char* p = static_cast<char*>(buf);
    while (len > 0) {
        ssize_t n = ::read(fd, p, len);
        if (n > 0) {
            p   += n;
            len -= static_cast<size_t>(n);
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

// Sets (or clears, if seconds == 0) a recv timeout on a Unix socket so a
// silent Discord can't hang the HID thread indefinitely.
static void setRecvTimeout(int fd, int seconds) {
    timeval tv{};
    tv.tv_sec  = seconds;
    tv.tv_usec = 0;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

// Generates a short random nonce used to correlate IPC requests with their
// responses. Discord echoes the nonce back so a multi-command client can
// route replies — we send commands one at a time and don't strictly need
// it, but Discord requires the field to be present.
static std::string makeNonce() {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx",
                  static_cast<unsigned long long>(rng()));
    return std::string(buf);
}

DiscordIPC::DiscordIPC(std::string id, std::string secret)
    : clientId(std::move(id)),
      clientSecret(std::move(secret)),
      tokenFilePath(xdgStateDir() + "/discord_token.json") {
    if (isConfigured()) {
        loadTokens();
    }
}

DiscordIPC::~DiscordIPC() {
    disconnectSocket();
}

bool DiscordIPC::isConfigured() const {
    return !clientId.empty() && !clientSecret.empty();
}

// --- Public entry point --------------------------------------------------

bool DiscordIPC::toggleMute() {
    if (!isConfigured()) {
        Logger::instance().warn("DiscordIPC",
            "discordClientId / discordClientSecret are not set in config");
        return false;
    }

    // First attempt uses whatever connection state we have (likely cached
    // from a previous press). If that fails, the most common cause is a
    // stale socket — Discord was restarted between the last press and now,
    // but our cached `authenticated` flag still says we're good. Drop the
    // dead socket and try once more from a clean state. Without this, the
    // first press after every Discord restart silently no-ops and the user
    // has to hit the button twice.
    if (!ensureReady()) return false;
    if (toggleMuteOnce()) return true;

    Logger::instance().info("DiscordIPC", "Toggle failed — retrying with fresh connection");
    disconnectSocket();
    if (!ensureReady()) return false;
    return toggleMuteOnce();
}

bool DiscordIPC::toggleMuteOnce() {
    QJsonObject getCmd{
        {"cmd",   "GET_VOICE_SETTINGS"},
        {"nonce", QString::fromStdString(makeNonce())},
    };
    if (!sendFrame(OP_FRAME, QJsonDocument(getCmd).toJson(QJsonDocument::Compact).toStdString())) {
        return false;
    }
    uint32_t op = 0;
    std::string body;
    if (!recvFrame(op, body) || op != OP_FRAME) return false;

    QJsonObject getResp = QJsonDocument::fromJson(QByteArray::fromStdString(body)).object();
    if (getResp.value("evt").toString() == "ERROR") {
        Logger::instance().warn("DiscordIPC",
            "GET_VOICE_SETTINGS rejected: " +
            QJsonDocument(getResp.value("data").toObject()).toJson(QJsonDocument::Compact).toStdString());
        return false;
    }
    bool currentMute = getResp.value("data").toObject().value("mute").toBool(false);

    QJsonObject setCmd{
        {"cmd",   "SET_VOICE_SETTINGS"},
        {"nonce", QString::fromStdString(makeNonce())},
        {"args",  QJsonObject{{"mute", !currentMute}}},
    };
    if (!sendFrame(OP_FRAME, QJsonDocument(setCmd).toJson(QJsonDocument::Compact).toStdString())) {
        return false;
    }
    if (!recvFrame(op, body) || op != OP_FRAME) return false;

    QJsonObject setResp = QJsonDocument::fromJson(QByteArray::fromStdString(body)).object();
    if (setResp.value("evt").toString() == "ERROR") {
        Logger::instance().warn("DiscordIPC",
            "SET_VOICE_SETTINGS rejected: " +
            QJsonDocument(setResp.value("data").toObject()).toJson(QJsonDocument::Compact).toStdString());
        return false;
    }

    Logger::instance().info("DiscordIPC",
        std::string("Discord mic is now ") + (!currentMute ? "MUTED" : "UNMUTED"));
    return true;
}

// --- Lifecycle -----------------------------------------------------------

bool DiscordIPC::ensureReady() {
    if (socketFd >= 0 && authenticated) return true;

    if (socketFd < 0 && !connectSocket()) return false;
    if (!handshake()) {
        disconnectSocket();
        return false;
    }
    if (!authenticate()) {
        // authenticate() already attempted a refresh on its own. If we got
        // here either we have no refresh token (first run) or the refresh
        // itself failed. Try the full popup flow as a last resort.
        if (!authorizeAndExchange()) {
            disconnectSocket();
            return false;
        }
        // After the full flow we have a fresh access token; re-issue
        // AUTHENTICATE so the IPC session is bound to it.
        if (!authenticate()) {
            disconnectSocket();
            return false;
        }
    }
    authenticated = true;
    return true;
}

bool DiscordIPC::connectSocket() {
    // Discord may be running under any of discord-ipc-0 ... discord-ipc-9.
    const char* runtimeDir = std::getenv("XDG_RUNTIME_DIR");
    std::string base = runtimeDir ? runtimeDir : "/tmp";

    // Check both the native socket location and the Flatpak sandbox path.
    // The Flatpak version of Discord places its socket under a subdirectory
    // rather than directly in XDG_RUNTIME_DIR.
    std::string bases[] = {
        base,
        base + "/app/com.discordapp.Discord",
    };

    int lastErr = 0;
    for (const auto& b : bases) {
        for (int i = 0; i < 10; ++i) {
            std::string path = b + "/discord-ipc-" + std::to_string(i);

            int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
            if (fd < 0) { lastErr = errno; continue; }

            sockaddr_un addr{};
            addr.sun_family = AF_UNIX;
            if (path.size() >= sizeof(addr.sun_path)) {
                ::close(fd);
                continue;
            }
            std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);

            if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
                // Apply a recv timeout immediately so any subsequent read (in
                // handshake / authenticate / toggleMute) can't block forever.
                // authorizeAndExchange() temporarily clears it for the popup.
                setRecvTimeout(fd, SOCKET_TIMEOUT_SEC);
                socketFd = fd;
                return true;
            }
            lastErr = errno;
            ::close(fd);
        }
    }
    Logger::instance().warn("DiscordIPC",
        std::string("No Discord IPC socket reachable (last errno: ") +
        std::to_string(lastErr) + " / " + std::strerror(lastErr) +
        ") — is Discord running?");
    return false;
}

void DiscordIPC::disconnectSocket() {
    if (socketFd >= 0) {
        ::close(socketFd);
        socketFd = -1;
    }
    authenticated = false;
}

// --- Protocol ------------------------------------------------------------

bool DiscordIPC::handshake() {
    QJsonObject h{
        {"v",         1},
        {"client_id", QString::fromStdString(clientId)},
    };
    if (!sendFrame(OP_HANDSHAKE, QJsonDocument(h).toJson(QJsonDocument::Compact).toStdString())) {
        return false;
    }
    uint32_t op = 0;
    std::string body;
    if (!recvFrame(op, body)) return false;
    if (op == OP_CLOSE) {
        Logger::instance().warn("DiscordIPC", "Handshake rejected: " + body);
        return false;
    }
    return true;
}

bool DiscordIPC::authenticate() {
    if (accessToken.empty()) return false;

    // Send AUTHENTICATE with the current accessToken and return the parsed
    // response. A positive success requires a `data.user` object — checking
    // only for absence of "ERROR" would treat empty/garbled responses as
    // success.
    auto tryAuth = [this]() -> QJsonObject {
        QJsonObject cmd{
            {"cmd",   "AUTHENTICATE"},
            {"nonce", QString::fromStdString(makeNonce())},
            {"args",  QJsonObject{{"access_token", QString::fromStdString(accessToken)}}},
        };
        if (!sendFrame(OP_FRAME, QJsonDocument(cmd).toJson(QJsonDocument::Compact).toStdString())) {
            return {};
        }
        uint32_t op = 0;
        std::string body;
        if (!recvFrame(op, body) || op != OP_FRAME) return {};
        return QJsonDocument::fromJson(QByteArray::fromStdString(body)).object();
    };
    auto isOk = [](const QJsonObject& r) {
        return !r.isEmpty()
            && r.value("evt").toString() != "ERROR"
            && r.value("data").toObject().value("user").isObject();
    };

    QJsonObject resp = tryAuth();
    if (isOk(resp)) return true;

    // Most common failure cause is an expired access token. Try one refresh
    // and one retry; if either step fails, caller falls back to AUTHORIZE.
    QJsonObject errData = resp.value("data").toObject();
    if (!errData.isEmpty()) {
        Logger::instance().info("DiscordIPC",
            "AUTHENTICATE rejected, attempting token refresh: " +
            QJsonDocument(errData).toJson(QJsonDocument::Compact).toStdString());
    }
    if (refreshToken.empty() || !refreshAccessToken()) return false;

    return isOk(tryAuth());
}

bool DiscordIPC::authorizeAndExchange() {
    Logger::instance().info("DiscordIPC",
        "Requesting AUTHORIZE — accept the popup inside the Discord app");

    QJsonObject cmd{
        {"cmd",   "AUTHORIZE"},
        {"nonce", QString::fromStdString(makeNonce())},
        {"args",  QJsonObject{
            {"client_id", QString::fromStdString(clientId)},
            {"scopes",    QJsonArray{QString("rpc"), QString("rpc.voice.write"),
                                     QString("rpc.voice.read")}},
        }},
    };
    if (!sendFrame(OP_FRAME, QJsonDocument(cmd).toJson(QJsonDocument::Compact).toStdString())) {
        return false;
    }
    uint32_t op = 0;
    std::string body;

    // Suspend the recv timeout: the user might take a while to click
    // "Authorize" inside Discord, and the popup itself doesn't emit any
    // intermediate traffic. Restore the normal timeout regardless of how
    // we exit so subsequent reads can't hang forever.
    setRecvTimeout(socketFd, 0);
    bool ok = recvFrame(op, body) && op == OP_FRAME;
    setRecvTimeout(socketFd, SOCKET_TIMEOUT_SEC);
    if (!ok) return false;

    QJsonObject resp = QJsonDocument::fromJson(QByteArray::fromStdString(body)).object();
    if (resp.value("evt").toString() == "ERROR") {
        Logger::instance().warn("DiscordIPC",
            "AUTHORIZE rejected: " + QJsonDocument(resp).toJson(QJsonDocument::Compact).toStdString());
        return false;
    }
    std::string code = resp.value("data").toObject().value("code").toString().toStdString();
    if (code.empty()) return false;

    // Exchange the short-lived code for an access + refresh token pair.
    QUrlQuery q;
    q.addQueryItem("client_id",     QString::fromStdString(clientId));
    q.addQueryItem("client_secret", QString::fromStdString(clientSecret));
    q.addQueryItem("grant_type",    "authorization_code");
    q.addQueryItem("code",          QString::fromStdString(code));
    q.addQueryItem("redirect_uri",  REDIRECT_URI);

    std::string respBody = postToken(q.toString(QUrl::FullyEncoded).toStdString());
    if (respBody.empty()) return false;

    QJsonObject tokens = QJsonDocument::fromJson(QByteArray::fromStdString(respBody)).object();
    accessToken  = tokens.value("access_token").toString().toStdString();
    refreshToken = tokens.value("refresh_token").toString().toStdString();
    if (accessToken.empty() || refreshToken.empty()) {
        Logger::instance().warn("DiscordIPC", "Token exchange returned no tokens");
        return false;
    }
    saveTokens();
    return true;
}

bool DiscordIPC::refreshAccessToken() {
    QUrlQuery q;
    q.addQueryItem("client_id",     QString::fromStdString(clientId));
    q.addQueryItem("client_secret", QString::fromStdString(clientSecret));
    q.addQueryItem("grant_type",    "refresh_token");
    q.addQueryItem("refresh_token", QString::fromStdString(refreshToken));

    std::string respBody = postToken(q.toString(QUrl::FullyEncoded).toStdString());
    if (respBody.empty()) return false;

    QJsonObject tokens = QJsonDocument::fromJson(QByteArray::fromStdString(respBody)).object();
    std::string newAccess  = tokens.value("access_token").toString().toStdString();
    std::string newRefresh = tokens.value("refresh_token").toString().toStdString();
    if (newAccess.empty()) return false;

    accessToken = newAccess;
    // Discord may rotate the refresh token; if it did, persist the new one.
    if (!newRefresh.empty()) refreshToken = newRefresh;
    saveTokens();
    return true;
}

// --- Frame I/O -----------------------------------------------------------

bool DiscordIPC::sendFrame(uint32_t opcode, const std::string& json) {
    if (socketFd < 0) return false;

    uint32_t header[2] = { opcode, static_cast<uint32_t>(json.size()) };
    if (!writeAll(socketFd, header, sizeof(header))) return false;
    if (!json.empty() && !writeAll(socketFd, json.data(), json.size())) return false;
    return true;
}

bool DiscordIPC::recvFrame(uint32_t& opcode, std::string& json) {
    if (socketFd < 0) return false;

    uint32_t header[2] = {0, 0};
    if (!readAll(socketFd, header, sizeof(header))) return false;

    opcode = header[0];
    uint32_t len = header[1];
    if (len > MAX_FRAME_BYTES) {
        Logger::instance().warn("DiscordIPC",
            "Refusing oversized frame (" + std::to_string(len) + " bytes)");
        return false;
    }

    json.assign(len, '\0');
    if (len > 0 && !readAll(socketFd, json.data(), len)) return false;
    return true;
}

// --- Token persistence ---------------------------------------------------

void DiscordIPC::loadTokens() {
    QFile f(QString::fromStdString(tokenFilePath));
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    accessToken  = root.value("access_token").toString().toStdString();
    refreshToken = root.value("refresh_token").toString().toStdString();
}

void DiscordIPC::saveTokens() const {
    QDir().mkpath(QString::fromStdString(xdgStateDir()));

    QJsonObject root{
        {"access_token",  QString::fromStdString(accessToken)},
        {"refresh_token", QString::fromStdString(refreshToken)},
    };
    QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);

    // Atomic write with strict perms from the start: open a temp file with
    // O_CREAT|O_EXCL|0600 (no race window where the file exists at 0644
    // before we tighten it), write the contents, then rename(2) over the
    // destination. rename is atomic on the same filesystem, so a crash
    // can't leave a half-written or world-readable token file behind.
    std::string tmpPath = tokenFilePath + ".tmp";
    ::unlink(tmpPath.c_str()); // discard any leftover from a previous crash

    int fd = ::open(tmpPath.c_str(), O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        Logger::instance().warn("DiscordIPC",
            "Failed to create token temp file: " + tmpPath +
            " (" + std::strerror(errno) + ")");
        return;
    }
    if (!writeAll(fd, bytes.constData(), static_cast<size_t>(bytes.size()))) {
        Logger::instance().warn("DiscordIPC",
            std::string("Failed to write token temp file: ") + std::strerror(errno));
        ::close(fd);
        ::unlink(tmpPath.c_str());
        return;
    }
    ::close(fd);

    if (::rename(tmpPath.c_str(), tokenFilePath.c_str()) != 0) {
        Logger::instance().warn("DiscordIPC",
            std::string("Failed to rename token file into place: ") + std::strerror(errno));
        ::unlink(tmpPath.c_str());
    }
}

// --- HTTPS helper --------------------------------------------------------

std::string DiscordIPC::postToken(const std::string& formBody) const {
    QNetworkAccessManager mgr;
    QNetworkRequest req{QUrl(TOKEN_URL)};
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  "application/x-www-form-urlencoded");

    QEventLoop loop;
    QNetworkReply* reply = mgr.post(req, QByteArray::fromStdString(formBody));
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    QByteArray body = reply->readAll();
    int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
    if (!ok) {
        Logger::instance().warn("DiscordIPC",
            "Token endpoint returned HTTP " + std::to_string(status) + ": " + body.toStdString());
        return {};
    }
    return body.toStdString();
}
