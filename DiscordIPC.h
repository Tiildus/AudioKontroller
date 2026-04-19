// =============================================================================
// DiscordIPC.h — Local IPC client for the Discord desktop app
//
// Discord exposes a local Unix-socket RPC server at:
//   $XDG_RUNTIME_DIR/discord-ipc-0    (with -1, -2, ... fallbacks)
//
// Wire format for every message in either direction:
//   [opcode : uint32 little-endian]
//   [length : uint32 little-endian]   (length of the JSON body that follows)
//   [json   : <length> bytes]
//
// Opcodes used here:
//   0  HANDSHAKE  — first message; identifies our app to Discord
//   1  FRAME      — every other command (AUTHORIZE, AUTHENTICATE, ...)
//   2  CLOSE      — server is closing the connection
//   3  PING / 4 PONG — keepalive (we don't initiate either)
//
// Authentication flow (one-time per machine):
//   1. HANDSHAKE   — { v: 1, client_id }
//   2. AUTHORIZE   — pops up "Allow AudioKontroller to ..." inside Discord;
//                    on accept, returns a short-lived OAuth code
//   3. POST https://discord.com/api/oauth2/token  (client_id + client_secret
//      + code) → access_token + refresh_token
//   4. AUTHENTICATE — { access_token } over the IPC socket
//
// The refresh token is persisted to $XDG_STATE_HOME/audiokontroller/
// discord_token.json so subsequent runs skip the AUTHORIZE popup.
//
// Threading: every public method is synchronous and is intended to be
// called from the HID thread (the same thread ButtonHandler runs on).
// QNetworkAccessManager calls used during OAuth are driven by a local
// QEventLoop, so this class never touches the main Qt event loop.
// =============================================================================

#pragma once
#include <cstdint>
#include <string>

class DiscordIPC {
public:
    DiscordIPC(std::string clientId, std::string clientSecret);
    ~DiscordIPC();

    DiscordIPC(const DiscordIPC&) = delete;
    DiscordIPC& operator=(const DiscordIPC&) = delete;

    // Returns true if discordClientId/Secret are non-empty. When false,
    // every other method is a no-op.
    bool isConfigured() const;

    // Reads the current voice settings, then writes them back with mute
    // inverted. Lazily reconnects if the socket has been closed (e.g.
    // Discord was restarted). Returns false if anything along the way
    // fails — caller is expected to log.
    bool toggleMute();

private:
    std::string clientId;
    std::string clientSecret;
    std::string tokenFilePath;

    int  socketFd      = -1;
    bool authenticated = false;

    // Tokens loaded from disk (or obtained via authorize()) and reused
    // across reconnects. accessToken expires; refreshToken does not under
    // normal use.
    std::string accessToken;
    std::string refreshToken;

    // Lifecycle ---------------------------------------------------------
    bool ensureReady();   // connect + handshake + authenticate, idempotent
    bool connectSocket(); // open the Unix socket (no protocol traffic)
    void disconnectSocket();

    // Issues GET_VOICE_SETTINGS + SET_VOICE_SETTINGS once. Caller is
    // responsible for ensureReady() and for retry-on-stale-socket logic;
    // this helper just runs the protocol exchange and returns success.
    bool toggleMuteOnce();

    // Protocol ----------------------------------------------------------
    bool handshake();
    bool authenticate();          // uses saved accessToken; refreshes once on 4xx
    bool authorizeAndExchange();  // full OAuth: AUTHORIZE → token endpoint
    bool refreshAccessToken();    // exchanges refreshToken for a new accessToken

    // Frame I/O. json may be empty on read if the body wasn't valid UTF-8.
    bool sendFrame(uint32_t opcode, const std::string& json);
    bool recvFrame(uint32_t& opcode, std::string& json);

    // Token persistence -------------------------------------------------
    void loadTokens();
    void saveTokens() const;

    // Helper: synchronous HTTPS POST to Discord's OAuth token endpoint.
    // Returns the response body on 2xx, empty string otherwise.
    std::string postToken(const std::string& formBody) const;
};
