/***********************************************************************
Diagnostics.cpp - startup hardware diagnostics report

This file is part of DuneBox, a fork of Magic Sand.
***********************************************************************/

#include "Diagnostics.h"
#include "ofxKinect.h"
#include <fstream>

// Direct GLFW access for monitor enumeration. OF bundles GLFW; define
// GLFW_INCLUDE_NONE so it doesn't pull in its own GL headers (OF already does).
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

// -- Probing ------------------------------------------------------------------

static DiagnosticItem probeKinect(int kinectVersion, bool kinectConnected) {
    if (kinectVersion == 2) {
        if (kinectConnected)
            return {"Kinect", "v2 connected", DiagnosticItem::OK};
        return {"Kinect", "v2 not opened - may be in use by another app", DiagnosticItem::ERR};
    }
    if (kinectVersion == 3) {
        return {"Kinect", "Azure (v3) - unsupported in this build", DiagnosticItem::WARN};
    }
    // Kinect v1 (ofxKinect / libfreenect)
    int n = ofxKinect::numConnectedDevices();
    if (n > 0)
        return {"Kinect", "v1: " + std::to_string(n) + " device(s) connected", DiagnosticItem::OK};
    return {"Kinect", "v1: no device detected", DiagnosticItem::ERR};
}

static DiagnosticItem probeGPU() {
    std::string vendor   = (const char*)glGetString(GL_VENDOR);
    std::string renderer = (const char*)glGetString(GL_RENDERER);
    return {"GPU", vendor + " - " + renderer, DiagnosticItem::OK};
}

static DiagnosticItem probeOpenGL() {
    std::string ver = (const char*)glGetString(GL_VERSION);
    return {"OpenGL", ver, DiagnosticItem::OK};
}

static DiagnosticItem probeDisplay() {
    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    std::string detail = std::to_string(count) + " display(s)";
    if (count > 0 && monitors) {
        const GLFWvidmode* mode = glfwGetVideoMode(monitors[0]);
        if (mode) {
            detail += " - primary " + std::to_string(mode->width) + "x" + std::to_string(mode->height);
        }
    }
    auto status = count >= 2 ? DiagnosticItem::OK : DiagnosticItem::WARN;
    return {"Displays", detail, status};
}

static DiagnosticItem probeWaterShaders() {
    // Shader compilation is already validated during WaterSimulation::setup().
    // We just report that the pipeline is expected to work with GL 3.2+.
    std::string ver = (const char*)glGetString(GL_VERSION);
    // Parse major version
    int major = 0;
    if (!ver.empty()) major = ver[0] - '0';
    if (major >= 3)
        return {"Water shaders", "GL " + ver.substr(0, 3) + " - supported", DiagnosticItem::OK};
    return {"Water shaders", "GL " + ver.substr(0, 3) + " - may not support #version 150", DiagnosticItem::WARN};
}

static DiagnosticItem probeSettingsFile(const std::string& path, const std::string& label) {
    std::ifstream f(ofToDataPath(path));
    if (f.good())
        return {label, "found", DiagnosticItem::OK};
    return {label, "not found - will use defaults", DiagnosticItem::WARN};
}

void Diagnostics::probe(int kinectVersion, bool kinectConnected) {
    if (probed) return;
    probed = true;

    items.push_back(probeKinect(kinectVersion, kinectConnected));
    items.push_back(probeGPU());
    items.push_back(probeOpenGL());
    items.push_back(probeDisplay());
    items.push_back(probeWaterShaders());
    items.push_back(probeSettingsFile("settings/kinectProjectorSettings.xml", "Kinect settings"));
    items.push_back(probeSettingsFile("settings/sandSurfaceRendererSettings.xml", "Renderer settings"));
}

void Diagnostics::log() const {
    // Emit the startup diagnostics to the console instead of drawing a blocking
    // on-screen overlay. In the two-window (main GUI + projector) layout the
    // overlay would cover the main window's GUI and could remain on screen,
    // making the app look frozen. The console log is just as informative and
    // can never block the UI.
    ofLogNotice("Diagnostics") << "----- DuneBox startup diagnostics -----";
    for (const auto& item : items) {
        ofLogNotice("Diagnostics") << item.indicator() << " " << item.label
                                    << ": " << item.detail;
    }
    ofLogNotice("Diagnostics") << "---------------------------------------";
}

