/***********************************************************************
AzureKinectHandler — Implementation for Azure Kinect DK / Orbbec Femto Bolt.

Requires ofxAzureKinect addon and Azure Kinect SDK.
Only compiled when DUNEBOX_USE_AZURE_KINECT is defined.

Copyright (c) 2025 Manaiakalani
Licensed under GPL-2.0 (same as parent project).
***********************************************************************/

#include "AzureKinectHandler.h"

#ifdef DUNEBOX_USE_AZURE_KINECT

AzureKinectHandler::AzureKinectHandler()
    : connected(false), frameNew(false),
      depthWidth(640), depthHeight(576), maxDepthMm(3860.0f) {
}

AzureKinectHandler::~AzureKinectHandler() {
    close();
}

bool AzureKinectHandler::setup() {
    // Configure for NFOV Unbinned mode (640×576) — best for sandbox use
    ofxAzureKinect::DeviceSettings settings;
    settings.depthMode = K4A_DEPTH_MODE_NFOV_UNBINNED;
    settings.colorResolution = K4A_COLOR_RESOLUTION_1080P;
    settings.updateColor = true;
    settings.updateDepth = true;
    settings.updateWorld = false;  // We compute our own projection

    depthWidth = 640;
    depthHeight = 576;
    maxDepthMm = 3860.0f;  // NFOV unbinned max range

    depthPixels.allocate(depthWidth, depthHeight, 1);
    colorPixels.allocate(1920, 1080, OF_PIXELS_RGB);
    depthFloat.allocate(depthWidth, depthHeight, 1);

    if (!kinect.open(settings)) {
        ofLogError("AzureKinectHandler") << "Failed to open Azure Kinect / Femto Bolt. "
            "Check USB 3.0 connection and SDK installation.";
        return false;
    }

    connected = true;
    ofLogNotice("AzureKinectHandler")
        << "Azure Kinect opened (NFOV Unbinned: "
        << depthWidth << "x" << depthHeight << " depth, 1920x1080 color)";
    return true;
}

bool AzureKinectHandler::open() {
    return setup();
}

void AzureKinectHandler::close() {
    if (connected) {
        kinect.close();
        connected = false;
        ofLogNotice("AzureKinectHandler") << "Azure Kinect closed";
    }
}

void AzureKinectHandler::update() {
    if (!connected) return;

    kinect.update();
    frameNew = kinect.isFrameNew();

    if (frameNew) {
        depthPixels = kinect.getDepthPixels();
        colorPixels = kinect.getColorPixels();
    }
}

ofFloatPixels& AzureKinectHandler::getDepthPixelsFloat() {
    const unsigned short* src = depthPixels.getData();
    float* dst = depthFloat.getData();
    int total = depthWidth * depthHeight;

    for (int i = 0; i < total; i++) {
        unsigned short raw = src[i];
        if (raw == 0 || raw > static_cast<unsigned short>(maxDepthMm)) {
            dst[i] = 0.0f;
        } else {
            dst[i] = static_cast<float>(raw) / maxDepthMm;
        }
    }
    return depthFloat;
}

ofMatrix4x4 AzureKinectHandler::getWorldMatrix() {
    // Identity — calibration system handles world transform
    return ofMatrix4x4();
}

float AzureKinectHandler::getRawDepthAt(int x, int y) {
    if (x < 0 || x >= (int)depthWidth || y < 0 || y >= (int)depthHeight) return 0;
    return static_cast<float>(depthPixels.getData()[y * depthWidth + x]);
}

#endif // DUNEBOX_USE_AZURE_KINECT
