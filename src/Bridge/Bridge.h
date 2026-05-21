/***********************************************************************
Bridge.h — inter-app communication bridge (TCP/JSON)

Connects DuneBox (C++) to DuneBox-sandcam (Python) via a TCP socket
using newline-delimited JSON (NDJSON).  The C++ side acts as a TCP
*client*; the Python side listens as the server.

This file is part of DuneBox, a fork of Magic Sand.
***********************************************************************/

#pragma once

#include "ofMain.h"
#include <string>
#include <vector>
#include <mutex>
#include <deque>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  typedef SOCKET SocketHandle;
  #define INVALID_SOCK INVALID_SOCKET
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <errno.h>
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
