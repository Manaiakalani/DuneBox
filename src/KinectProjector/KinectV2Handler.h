/***********************************************************************
KinectV2Handler - Wrapper for Kinect V2 (Xbox One) via ofxKinectV2 or
ofxLibfreenect2 addon.

This provides the same interface expected by KinectProjector so that
V1 and V2 sensors can be swapped via a config setting.

Requirements:
  - ofxKinectV2 addon: https://github.com/ofTheo/ofxKinectV2
    OR ofxLibfreenect2: https://github.com/pierrep/ofxLibfreenect2
  - libfreenect2 system library installed
  - USB 3.0 connection required

Build instructions:
  1. Clone ofxKinectV2 into openFrameworks/addons/
  2. Add ofxKinectV2 to addons.make (or VS project references)
  3. Set DUNEBOX_KINECT_VERSION=2 in your build config
  4. Rebuild the project

Copyright (c) 2025 Manaiakalani
Licensed under GPL-2.0 (same as parent project).
***********************************************************************/

#pragma once

#include "ofMain.h"

#ifdef DUNEBOX_USE_KINECT_V2

#include "ofxKinectV2.h"

class KinectV2Handler {
public:
    KinectV2Handler();
    ~KinectV2Handler();

    bool setup();
    bool open();
    void close();
    void update();

    bool isConnected() const { return connected; }
    bool isFrameNew() const { return frameNew; }

    ofVec2f getDepthResolution() const { return ofVec2f(512, 424); }
    unsigned int getWidth() const { return 512; }
    unsigned int getHeight() const { return 424; }

    /// Raw depth pixels (uint16, millimetres)
    ofShortPixels& getRawDepthPixels() { return depthPixels; }

    /// Colour pixels (RGB)
    ofPixels& getColorPixels() { return colorPixels; }

    /// Float depth image suitable for filtering pipeline
    ofFloatPixels& getDepthPixelsFloat();

    /// World coordinate matrix (for KinectProjector calibration)
    ofMatrix4x4 getWorldMatrix();

    float getRawDepthAt(int x, int y);

private:
    ofxKinectV2 kinect;
    ofShortPixels depthPixels;
    ofPixels colorPixels;
    ofFloatPixels depthFloat;
    bool connected;
    bool frameNew;
};

#else

// Stub when not compiled with V2 support - produces clear error at runtime
class KinectV2Handler {
public:
    KinectV2Handler() {}
    ~KinectV2Handler() {}

    bool setup() {
        ofLogError("KinectV2Handler") << "Not compiled with DUNEBOX_USE_KINECT_V2. "
            "Rebuild with the ofxKinectV2 addon and define DUNEBOX_USE_KINECT_V2.";
        return false;
    }
    bool open() { return false; }
    void close() {}
    void update() {}

    bool isConnected() const { return false; }
    bool isFrameNew() const { return false; }

    ofVec2f getDepthResolution() const { return ofVec2f(512, 424); }
    unsigned int getWidth() const { return 512; }
    unsigned int getHeight() const { return 424; }

    ofShortPixels& getRawDepthPixels() { return depthPixels; }
    ofPixels& getColorPixels() { return colorPixels; }
    ofFloatPixels& getDepthPixelsFloat() { return depthFloat; }
    ofMatrix4x4 getWorldMatrix() { return ofMatrix4x4(); }
    float getRawDepthAt(int x, int y) { return 0; }

private:
    ofShortPixels depthPixels;
    ofPixels colorPixels;
    ofFloatPixels depthFloat;
};

#endif // DUNEBOX_USE_KINECT_V2
