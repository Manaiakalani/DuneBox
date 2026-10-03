/***********************************************************************
NetworkDepthSource - depth frames from DuneBox-sandcam's depth relay.

Lets DuneBox use any sensor sandcam supports (Orbbec Femto, RealSense,
Kinect via other drivers) without linking their SDKs: sandcam owns the
sensor and streams depth in millimetres over TCP (port 9877). Select it
with kinectVersion=4 in settings/kinectProjectorSettings.xml and turn on
"Share this sensor with DuneBox" in the sandcam dashboard.

Wire format: see depth_relay.py in DuneBox-sandcam.

This file is part of DuneBox, a fork of Magic Sand.
***********************************************************************/

#pragma once

// No socket headers here: ofMain.h pulls in <windows.h>, and winsock2 must
// come first. All socket code lives in NetworkDepthSource.cpp.
#include "ofMain.h"
#include <cstdint>
#include <string>

class NetworkDepthSource {
public:
    static constexpr int WIDTH = 640;
    static constexpr int HEIGHT = 480;
    static constexpr int DEFAULT_PORT = 9877;

    NetworkDepthSource();
    ~NetworkDepthSource();

    /// Connect to the relay. Safe to call repeatedly to reconnect.
    bool open(const std::string& host = "127.0.0.1", int port = DEFAULT_PORT);
    void close();
    bool isConnected() const { return sock != INVALID_HANDLE; }

    /// Wait up to ~200 ms for the next frame. Returns true when one arrived.
    bool update();

    /// Ask the relay for colour frames (calibration needs them).
    void setWantColor(bool want);
    bool hasColor() const { return colorFresh; }

    const ofShortPixels& getDepthPixels() const { return depth; }
    const ofPixels& getColorPixels() const { return color; }

    /// Pixel to camera-space mapping: x_world = (sx * px + ax) * depth.
    float sx = 1.0f / WIDTH, ax = -0.5f, sy = 1.0f / HEIGHT, ay = -0.5f;

private:
    bool readExact(void* dst, size_t n, bool startOfMessage = false);
    void sendLine(const char* line);

#ifdef _WIN32
    typedef std::uintptr_t Handle;
#else
    typedef int Handle;
#endif
    static constexpr Handle INVALID_HANDLE = static_cast<Handle>(~static_cast<Handle>(0));

    Handle sock = INVALID_HANDLE;
    std::string host = "127.0.0.1";
    int port = DEFAULT_PORT;
    bool wantColor = false;
    bool colorRequested = false;
    bool colorFresh = false;
    ofShortPixels depth;
    ofPixels color;
    std::vector<unsigned char> scratch;
};
