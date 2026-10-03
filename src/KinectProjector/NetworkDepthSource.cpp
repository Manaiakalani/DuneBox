/***********************************************************************
NetworkDepthSource.cpp - client for DuneBox-sandcam's depth relay.

This file is part of DuneBox, a fork of Magic Sand.
***********************************************************************/

// Socket headers must come before ofMain.h (see Bridge.cpp).
#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  #define DUNEBOX_SEND_FLAGS 0
#else
  #include <sys/socket.h>
  #include <sys/select.h>
  #include <sys/time.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <cerrno>
  #ifdef MSG_NOSIGNAL
    #define DUNEBOX_SEND_FLAGS MSG_NOSIGNAL
  #else
    #define DUNEBOX_SEND_FLAGS 0
  #endif
#endif

#include "NetworkDepthSource.h"
#include <cstring>

namespace {
    const char MAGIC[4] = {'D', 'B', 'D', '1'};
    const uint32_t FLAG_COLOR = 1;
    // magic, uint16 w, uint16 h, uint32 frame, 4 x float32, uint32 flags
    const size_t HEADER_SIZE = 4 + 2 + 2 + 4 + 16 + 4;

    uint16_t readU16(const unsigned char* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
    uint32_t readU32(const unsigned char* p) {
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }
    float readF32(const unsigned char* p) {
        uint32_t bits = readU32(p);
        float f;
        std::memcpy(&f, &bits, sizeof(f));
        return f;
    }
}

NetworkDepthSource::NetworkDepthSource() {
    depth.allocate(WIDTH, HEIGHT, 1);
    depth.set(0);
    color.allocate(WIDTH, HEIGHT, 3);
    color.set(0);
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
}

NetworkDepthSource::~NetworkDepthSource() {
    close();
#ifdef _WIN32
    WSACleanup();
#endif
}

bool NetworkDepthSource::open(const std::string& host_, int port_) {
    if (isConnected()) return true;
    host = host_;
    port = port_;

    Handle s = (Handle)::socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_HANDLE) return false;

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1 ||
        ::connect(s, (sockaddr*)&addr, sizeof(addr)) != 0) {
#ifdef _WIN32
        closesocket(s);
#else
        ::close(s);
#endif
        return false;
    }

    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));

    sock = s;
    colorRequested = false;
    if (wantColor) setWantColor(true);
    ofLogNotice("NetworkDepthSource") << "Connected to the sandcam depth relay at " << host << ":" << port;
    return true;
}

void NetworkDepthSource::close() {
    if (!isConnected()) return;
#ifdef _WIN32
    closesocket(sock);
#else
    ::close(sock);
#endif
    sock = INVALID_HANDLE;
    colorFresh = false;
    ofLogNotice("NetworkDepthSource") << "Disconnected from the sandcam depth relay";
}

void NetworkDepthSource::sendLine(const char* line) {
    if (!isConnected()) return;
    if (::send(sock, line, (int)std::strlen(line), DUNEBOX_SEND_FLAGS) < 0) close();
}

void NetworkDepthSource::setWantColor(bool want) {
    wantColor = want;
    if (isConnected() && want != colorRequested) {
        sendLine(want ? "C1\n" : "C0\n");
        colorRequested = want;
    }
    if (!want) colorFresh = false;
}

// Reads exactly n bytes, waiting 200 ms at a time so the grabber thread can
// still notice it should stop. Returns false if nothing arrives before the
// first byte; once a message has started it keeps waiting (giving up mid-
// message would desync the stream) unless the connection stalls for ~5 s.
bool NetworkDepthSource::readExact(void* dst, size_t n, bool startOfMessage) {
    char* out = static_cast<char*>(dst);
    size_t got = 0;
    int idleWaits = 0;
    while (got < n) {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(sock, &readable);
        timeval tv{0, 200000};
        int ready = ::select((int)(sock + 1), &readable, nullptr, nullptr, &tv);
        if (ready == 0) {
            if (startOfMessage && got == 0) return false;
            if (++idleWaits < 25) continue;
            ofLogWarning("NetworkDepthSource") << "Depth relay stalled mid-frame - reconnecting";
            close();
            return false;
        }
        if (ready < 0) {
            close();
            return false;
        }
        int r = ::recv(sock, out + got, (int)std::min<size_t>(n - got, 1 << 16), 0);
        if (r <= 0) {
            close();
            return false;
        }
        got += (size_t)r;
        idleWaits = 0;
    }
    return true;
}

bool NetworkDepthSource::update() {
    if (!isConnected()) return false;

    unsigned char header[HEADER_SIZE];
    if (!readExact(header, HEADER_SIZE, true)) return false;

    if (std::memcmp(header, MAGIC, 4) != 0) {
        ofLogError("NetworkDepthSource") << "Unexpected data from the depth relay - reconnecting";
        close();
        return false;
    }
    int w = readU16(header + 4);
    int h = readU16(header + 6);
    uint32_t flags = readU32(header + 28);
    if (w != WIDTH || h != HEIGHT) {
        ofLogError("NetworkDepthSource") << "Depth relay sent " << w << "x" << h
            << " frames; this build expects " << WIDTH << "x" << HEIGHT;
        close();
        return false;
    }

    size_t depthBytes = (size_t)w * h * 2;
    scratch.resize(depthBytes);
    if (!readExact(scratch.data(), depthBytes)) return false;
    unsigned short* dst = depth.getData();
    for (size_t i = 0; i < (size_t)w * h; ++i) {
        dst[i] = readU16(&scratch[i * 2]);
    }

    if (flags & FLAG_COLOR) {
        if (!readExact(color.getData(), (size_t)w * h * 3)) return false;
        colorFresh = true;
    }

    sx = readF32(header + 12);
    ax = readF32(header + 16);
    sy = readF32(header + 20);
    ay = readF32(header + 24);
    return true;
}
