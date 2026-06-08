/***********************************************************************
Diagnostics.cpp — startup hardware diagnostics overlay

This file is part of DuneBox, a fork of Magic Sand.
***********************************************************************/

#include "Diagnostics.h"
#include "ofxKinect.h"
#include <fstream>

// Direct GLFW access for monitor enumeration. OF bundles GLFW; define
// GLFW_INCLUDE_NONE so it doesn't pull in its own GL headers (OF already does).
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

// ── Probing ──────────────────────────────────────────────────────────────────

static DiagnosticItem probeKinect() {
    int n = ofxKinect::numConnectedDevices();
    if (n > 0)
        return {"Kinect", std::to_string(n) + " device(s) connected", DiagnosticItem::OK};
    return {"Kinect", "no device detected", DiagnosticItem::ERR};
}

static DiagnosticItem probeGPU() {
    std::string vendor   = (const char*)glGetString(GL_VENDOR);
    std::string renderer = (const char*)glGetString(GL_RENDERER);
    return {"GPU", vendor + " — " + renderer, DiagnosticItem::OK};
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
            detail += " — primary " + std::to_string(mode->width) + "x" + std::to_string(mode->height);
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
        return {"Water shaders", "GL " + ver.substr(0, 3) + " — supported", DiagnosticItem::OK};
    return {"Water shaders", "GL " + ver.substr(0, 3) + " — may not support #version 150", DiagnosticItem::WARN};
}

static DiagnosticItem probeSettingsFile(const std::string& path, const std::string& label) {
    std::ifstream f(ofToDataPath(path));
    if (f.good())
        return {label, "found", DiagnosticItem::OK};
    return {label, "not found — will use defaults", DiagnosticItem::WARN};
}

void Diagnostics::probe() {
    if (probed) return;
    probed = true;

    items.push_back(probeKinect());
    items.push_back(probeGPU());
    items.push_back(probeOpenGL());
    items.push_back(probeDisplay());
    items.push_back(probeWaterShaders());
    items.push_back(probeSettingsFile("settings/kinectProjectorSettings.xml", "Kinect settings"));
    items.push_back(probeSettingsFile("settings/sandSurfaceRendererSettings.xml", "Renderer settings"));
}

// ── Timing ───────────────────────────────────────────────────────────────────

void Diagnostics::update(float dt) {
    if (!active) return;
    elapsed += dt;
    if (keyDismissed || elapsed >= duration) {
        active = false;
    }
}

bool Diagnostics::shouldDismiss() const {
    return !active;
}

void Diagnostics::onKeyPressed() {
    keyDismissed = true;
    active = false;
}

// ── Drawing ──────────────────────────────────────────────────────────────────

void Diagnostics::draw() {
    if (!active && elapsed > 0) return;

    float w = ofGetWidth();
    float h = ofGetHeight();

    // Semi-transparent dark overlay
    ofSetColor(14, 18, 26, 230);
    ofDrawRectangle(0, 0, w, h);

    float rowH   = 26.0f;
    float pad     = 24.0f;
    float panelW  = std::min(560.0f, w - 60.0f);
    float panelH  = 60.0f + rowH * items.size() + 30.0f;
    float px      = (w - panelW) / 2.0f;
    float py      = (h - panelH) / 2.0f;

    // Card background
    ofSetColor(22, 28, 40, 238);
    ofDrawRectRounded(px, py, panelW, panelH, 12);
    ofNoFill();
    ofSetColor(72, 124, 188);
    ofDrawRectRounded(px, py, panelW, panelH, 12);
    ofFill();

    // Title
    ofSetColor(220, 228, 238);
    ofDrawBitmapString("DuneBox Diagnostics", px + pad, py + pad + 10);

    // Rows
    float y = py + pad + 36;
    for (auto& item : items) {
        ofSetColor(item.color());
        ofDrawBitmapString(item.indicator(), px + pad, y);

        ofSetColor(220, 228, 238);
        ofDrawBitmapString(item.label, px + pad + 42, y);

        ofSetColor(150, 162, 178);
        std::string detail = item.detail;
        if (detail.size() > 50) detail = detail.substr(0, 47) + "...";
        ofDrawBitmapString(detail, px + pad + 180, y);

        y += rowH;
    }

    // Auto-dismiss hint
    float remaining = std::max(0.0f, duration - elapsed);
    ofSetColor(100, 110, 130);
    ofDrawBitmapString("Auto-dismiss in " + ofToString((int)ceilf(remaining)) + "s — press any key",
                       px + pad, y + 10);
}
