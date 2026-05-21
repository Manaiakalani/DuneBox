/***********************************************************************
KinectV2Handler — Implementation for Kinect V2 (Xbox One).

Requires ofxKinectV2 addon and libfreenect2.
Only compiled when DUNEBOX_USE_KINECT_V2 is defined.

Copyright (c) 2025 Manaiakalani
Licensed under GPL-2.0 (same as parent project).
***********************************************************************/

#include "KinectV2Handler.h"

#ifdef DUNEBOX_USE_KINECT_V2

KinectV2Handler::KinectV2Handler()
    : connected(false), frameNew(false) {
}

KinectV2Handler::~KinectV2Handler() {
    close();
}

bool KinectV2Handler::setup() {
    // ofxKinectV2 enumerates USB devices internally
    depthPixels.allocate(512, 424, 1);
    colorPixels.allocate(1920, 1080, OF_PIXELS_RGB);
    depthFloat.allocate(512, 424, 1);

    return open();
}

bool KinectV2Handler::open() {
    bool ok = kinect.open();
    if (ok) {
        connected = true;
        ofLogNotice("KinectV2Handler") << "Kinect V2 opened successfully (512x424 depth, 1920x1080 color)";
    } else {
        ofLogError("KinectV2Handler") << "Failed to open Kinect V2. Check USB 3.0 connection.";
    }
    return ok;
}

void KinectV2Handler::close() {
    if (connected) {
        kinect.close();
        connected = false;
        ofLogNotice("KinectV2Handler") << "Kinect V2 closed";
    }
}

void KinectV2Handler::update() {
    if (!connected) return;

    kinect.update();
    frameNew = kinect.isFrameNew();

    if (frameNew) {
        // Copy depth data (512×424, 16-bit millimetres)
        depthPixels = kinect.getRawDepthPixels();

        // Copy colour data (1920×1080 RGB)
        colorPixels = kinect.getPixels();
    }
}

ofFloatPixels& KinectV2Handler::getDepthPixelsFloat() {
    // Convert uint16 mm values to normalised float [0,1]
    // Kinect V2 range: 0–4500 mm
    const unsigned short* src = depthPixels.getData();
    float* dst = depthFloat.getData();
    int total = 512 * 424;

    for (int i = 0; i < total; i++) {
        unsigned short raw = src[i];
        if (raw == 0 || raw > 4500) {
            dst[i] = 0.0f;
        } else {
            dst[i] = static_cast<float>(raw) / 4500.0f;
        }
    }
    return depthFloat;
}

ofMatrix4x4 KinectV2Handler::getWorldMatrix() {
    // Kinect V2 world matrix — identity for now; calibration overrides this
    return ofMatrix4x4();
}

float KinectV2Handler::getRawDepthAt(int x, int y) {
    if (x < 0 || x >= 512 || y < 0 || y >= 424) return 0;
    return static_cast<float>(depthPixels.getData()[y * 512 + x]);
}

#endif // DUNEBOX_USE_KINECT_V2
