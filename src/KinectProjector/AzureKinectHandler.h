/***********************************************************************
AzureKinectHandler — Wrapper for Azure Kinect DK and Orbbec Femto Bolt
via the ofxAzureKinect addon.

The Orbbec Femto Bolt is hardware-compatible with the Azure Kinect SDK,
so this handler supports both devices.

Requirements:
  - ofxAzureKinect addon: https://github.com/prisonerjohn/ofxAzureKinect
  - Azure Kinect SDK v1.4+:
      Windows: https://learn.microsoft.com/azure/kinect-dk/sensor-sdk-download
      Linux:   https://github.com/microsoft/Azure-Kinect-Sensor-SDK
  - For Orbbec Femto Bolt: install Orbbec's Azure Kinect-compatible firmware

Build instructions:
  1. Install Azure Kinect SDK (Body Tracking SDK optional)
  2. Clone ofxAzureKinect into openFrameworks/addons/
  3. Add ofxAzureKinect to addons.make (or VS project references)
  4. Define DUNEBOX_USE_AZURE_KINECT in your build config
  5. Rebuild the project

Depth modes:
  - NFOV Unbinned: 640×576, range 0.5–3.86 m (best for sandbox)
  - NFOV 2x2 Binned: 320×288, range 0.5–5.46 m
  - WFOV Unbinned: 1024×1024, range 0.25–2.21 m
  - WFOV 2x2 Binned: 512×512, range 0.25–2.88 m

Recommended for DuneBox: NFOV Unbinned (640×576)

Copyright (c) 2025 Manaiakalani
Licensed under GPL-2.0 (same as parent project).
***********************************************************************/

#pragma once

#include "ofMain.h"

#ifdef DUNEBOX_USE_AZURE_KINECT

#include "ofxAzureKinect.h"

class AzureKinectHandler {
public:
    AzureKinectHandler();
    ~AzureKinectHandler();

    bool setup();
    bool open();
    void close();
    void update();

    bool isConnected() const { return connected; }
    bool isFrameNew() const { return frameNew; }

    /// Default NFOV Unbinned: 640×576
    ofVec2f getDepthResolution() const { return ofVec2f(depthWidth, depthHeight); }
    unsigned int getWidth() const { return depthWidth; }
    unsigned int getHeight() const { return depthHeight; }

    /// Raw depth pixels (uint16, millimetres)
    ofShortPixels& getRawDepthPixels() { return depthPixels; }

    /// Colour pixels (1920×1080 or 3840×2160 depending on config)
    ofPixels& getColorPixels() { return colorPixels; }

    /// Float depth for filter pipeline
    ofFloatPixels& getDepthPixelsFloat();

    /// World coordinate transform
    ofMatrix4x4 getWorldMatrix();

    float getRawDepthAt(int x, int y);

private:
    ofxAzureKinect::Device kinect;
    ofShortPixels depthPixels;
    ofPixels colorPixels;
    ofFloatPixels depthFloat;
    bool connected;
    bool frameNew;
    unsigned int depthWidth;
    unsigned int depthHeight;
    float maxDepthMm;  // depends on selected mode
};

#else

// Stub when not compiled with Azure Kinect support
class AzureKinectHandler {
public:
    AzureKinectHandler() {}
    ~AzureKinectHandler() {}

    bool setup() {
        ofLogError("AzureKinectHandler")
            << "Not compiled with DUNEBOX_USE_AZURE_KINECT. "
               "Rebuild with the ofxAzureKinect addon and define DUNEBOX_USE_AZURE_KINECT.\n"
               "  Requirements:\n"
               "    - Azure Kinect SDK v1.4+\n"
               "    - ofxAzureKinect addon (github.com/prisonerjohn/ofxAzureKinect)\n"
               "  Also compatible with Orbbec Femto Bolt.";
        return false;
    }
    bool open() { return false; }
    void close() {}
    void update() {}

    bool isConnected() const { return false; }
    bool isFrameNew() const { return false; }

    ofVec2f getDepthResolution() const { return ofVec2f(640, 576); }
    unsigned int getWidth() const { return 640; }
    unsigned int getHeight() const { return 576; }

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

#endif // DUNEBOX_USE_AZURE_KINECT
