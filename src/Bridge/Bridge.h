/***********************************************************************
Bridge.h — inter-app communication bridge (TCP/JSON)

Connects DuneBox (C++) to DuneBox-sandcam (Python) via a TCP socket
using newline-delimited JSON (NDJSON).  The C++ side acts as a TCP
*client*; the Python side listens as the server.

This file is part of DuneBox, a fork of Magic Sand.
***********************************************************************/

#pragma once

// NOTE: this header deliberately does NOT include any socket headers.
// On Windows, ofMain.h pulls in <windows.h> (and thus the legacy winsock.h),
// so pulling <winsock2.h> in here would clash for every translation unit that
// includes us (winsock 1.1 / 2 redefinition).  Instead, all winsock2 usage is
// confined to Bridge.cpp, which includes <winsock2.h> before anything else.
// Here we expose only a platform-portable socket handle type.

#include "ofMain.h"
#include <string>
#include <vector>
#include <mutex>
#include <deque>
#include <cstdint>

#ifdef _WIN32
  // Matches the Win32 SOCKET type (UINT_PTR) and INVALID_SOCKET ((SOCKET)~0)
  // without needing the winsock headers in this header.
  typedef std::uintptr_t SocketHandle;
  #define INVALID_SOCK (~static_cast<SocketHandle>(0))
#else
  typedef int SocketHandle;
  #define INVALID_SOCK (-1)
#endif

class Bridge {
public:
    Bridge();
    ~Bridge();

    /// Connect to the Python bridge server.
    void setup(std::string host = "127.0.0.1", int port = 9876);

    /// Call every frame — reads incoming data, handles reconnect.
    void update();

    /// Send a typed JSON message.  Extra fields are merged into the
    /// top-level object alongside "type".
    void send(const std::string& type, const ofJson& data = ofJson::object());

    /// Return (and clear) all messages received since the last poll.
    std::vector<ofJson> poll();

    bool isConnected() const { return connected; }

private:
    void tryConnect();
    void disconnect();
    void readIncoming();
    void flushOutgoing();
    bool setNonBlocking(SocketHandle sock);

    std::string host;
    int         port = 9876;
    bool        enabled = false;
    bool        connected = false;
    SocketHandle sock = INVALID_SOCK;

    float lastConnectAttempt = 0.0f;
    static constexpr float RECONNECT_INTERVAL = 2.0f;

    std::string recvBuf;

    std::mutex  sendMtx;
    std::deque<std::string> sendQueue;

    std::mutex  recvMtx;
    std::vector<ofJson> recvQueue;

#ifdef _WIN32
    bool wsaInitialised = false;
#endif
};
