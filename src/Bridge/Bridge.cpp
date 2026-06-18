/***********************************************************************
Bridge.cpp — inter-app communication bridge (TCP/JSON)

This file is part of DuneBox, a fork of Magic Sand.
***********************************************************************/

// Socket headers must come first.  On Windows, <winsock2.h> must precede any
// inclusion of <windows.h> (which ofMain.h, included via Bridge.h below, pulls
// in) — winsock2.h defines _WINSOCKAPI_ so windows.h then skips the legacy
// winsock.h, avoiding the winsock 1.1 / 2 redefinition conflict.  This is the
// only translation unit that touches winsock2.
#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <errno.h>
#endif

#include "Bridge.h"

// ── helpers ──────────────────────────────────────────────────────────

static void closeSock(SocketHandle& s) {
    if (s == INVALID_SOCK) return;
#ifdef _WIN32
    closesocket(s);
#else
    ::close(s);
#endif
    s = INVALID_SOCK;
}

// ── lifecycle ────────────────────────────────────────────────────────

Bridge::Bridge() {}

Bridge::~Bridge() {
    disconnect();
#ifdef _WIN32
    if (wsaInitialised) {
        WSACleanup();
        wsaInitialised = false;
    }
#endif
}

void Bridge::setup(std::string host_, int port_) {
    host = host_;
    port = port_;
    enabled = true;

#ifdef _WIN32
    if (!wsaInitialised) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            ofLogError("Bridge") << "WSAStartup failed";
            enabled = false;
            return;
        }
        wsaInitialised = true;
    }
#endif

    ofLogNotice("Bridge") << "Configured to connect to " << host << ":" << port;
    tryConnect();
}

// ── connection management ────────────────────────────────────────────

void Bridge::tryConnect() {
    if (connected || !enabled) return;

    float now = ofGetElapsedTimef();
    if (now - lastConnectAttempt < RECONNECT_INTERVAL) return;
    lastConnectAttempt = now;

    sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCK) {
        ofLogWarning("Bridge") << "socket() failed";
        return;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        ofLogWarning("Bridge") << "Invalid bridge host '" << host
                               << "' (expected a numeric IPv4 address)";
        closeSock(sock);
        return;
    }

    if (::connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        closeSock(sock);
        return;
    }

    if (!setNonBlocking(sock)) {
        closeSock(sock);
        return;
    }

    connected = true;
    recvBuf.clear();
    ofLogNotice("Bridge") << "Connected to " << host << ":" << port;
}

void Bridge::disconnect() {
    closeSock(sock);
    connected = false;
    recvBuf.clear();
    // Any partially-sent front line went to the now-dead socket; reset the
    // offset so it is re-sent in full on the next connection.
    sendOffset = 0;
}

bool Bridge::setNonBlocking(SocketHandle s) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(s, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(s, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

// ── per-frame update ─────────────────────────────────────────────────

void Bridge::update() {
    if (!enabled) return;

    if (!connected) {
        tryConnect();
        return;
    }

    readIncoming();
    flushOutgoing();
}

// ── send ─────────────────────────────────────────────────────────────

void Bridge::send(const std::string& type, const ofJson& data) {
    if (!enabled) return;

    ofJson msg = data;
    msg["type"] = type;

    std::string line = msg.dump(-1, ' ', false, ofJson::error_handler_t::replace) + "\n";

    std::lock_guard<std::mutex> lock(sendMtx);
    sendQueue.push_back(std::move(line));
    // Bound the backlog. When over the cap, drop the oldest message — but never
    // the front if it is mid-transmission (sendOffset > 0), as its head bytes
    // are already on the wire; drop the next one instead to avoid corrupting it.
    while (sendQueue.size() > MAX_SEND_QUEUE) {
        if (sendOffset > 0 && sendQueue.size() >= 2) {
            sendQueue.erase(sendQueue.begin() + 1);
        } else {
            sendQueue.pop_front();
        }
    }
}

void Bridge::flushOutgoing() {
    std::lock_guard<std::mutex> lock(sendMtx);

    while (!sendQueue.empty()) {
        std::string& line = sendQueue.front();
        const char* data = line.c_str() + sendOffset;
        int remaining = (int)(line.size() - sendOffset);
        int sent = ::send(sock, data, remaining, 0);
        if (sent <= 0) {
#ifdef _WIN32
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) return;
#else
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
#endif
            ofLogWarning("Bridge") << "Send failed — disconnecting";
            disconnect();
            return;
        }
        if (sent < remaining) {
            // Partial write on the non-blocking socket: keep the unsent tail at
            // the front and resume from here on the next flush.
            sendOffset += (size_t)sent;
            return;
        }
        sendQueue.pop_front();
        sendOffset = 0;
    }
}

// ── receive ──────────────────────────────────────────────────────────

void Bridge::readIncoming() {
    char buf[4096];

    while (true) {
        int n = ::recv(sock, buf, sizeof(buf), 0);
        if (n > 0) {
            recvBuf.append(buf, n);
            // Guard against a peer that never delimits a line: if the buffer has
            // grown past the cap with no newline in sight, the stream is invalid.
            if (recvBuf.size() > MAX_RECV_BUF &&
                recvBuf.find('\n') == std::string::npos) {
                ofLogWarning("Bridge")
                    << "recv buffer exceeded " << MAX_RECV_BUF
                    << " bytes without a newline — disconnecting";
                recvBuf.clear();
                disconnect();
                return;
            }
        } else if (n == 0) {
            ofLogNotice("Bridge") << "Peer disconnected";
            disconnect();
            return;
        } else {
#ifdef _WIN32
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) break;
#else
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
#endif
            ofLogWarning("Bridge") << "recv error — disconnecting";
            disconnect();
            return;
        }
    }

    // Parse complete lines
    size_t pos;
    while ((pos = recvBuf.find('\n')) != std::string::npos) {
        std::string line = recvBuf.substr(0, pos);
        recvBuf.erase(0, pos + 1);

        if (line.empty()) continue;

        try {
            ofJson obj = ofJson::parse(line);

            // Auto-reply to pings
            if (obj.value("type", "") == "ping") {
                send("pong");
                continue;
            }

            std::lock_guard<std::mutex> lock(recvMtx);
            recvQueue.push_back(std::move(obj));
        } catch (const std::exception& e) {
            ofLogWarning("Bridge") << "Invalid JSON: " << line.substr(0, 120);
        }
    }
}

// ── poll ─────────────────────────────────────────────────────────────

std::vector<ofJson> Bridge::poll() {
    std::lock_guard<std::mutex> lock(recvMtx);
    std::vector<ofJson> out;
    out.swap(recvQueue);
    return out;
}
