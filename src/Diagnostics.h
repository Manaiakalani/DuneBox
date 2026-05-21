/***********************************************************************
Diagnostics.h — startup hardware diagnostics overlay

Probes Kinect, GPU, OpenGL, displays, shader compilation, and settings
files. Draws a brief overlay during the first few seconds of launch,
then auto-dismisses (or on any key press).

This file is part of DuneBox, a fork of Magic Sand.
***********************************************************************/

#pragma once

#include "ofMain.h"
#include <string>
#include <vector>

struct DiagnosticItem {
    std::string label;
    std::string detail;
    enum Status { OK, WARN, ERR } status;

    ofColor color() const {
        switch (status) {
            case OK:   return ofColor(80, 200, 120);
            case WARN: return ofColor(230, 190, 60);
            case ERR:  return ofColor(220, 70, 70);
        }
        return ofColor(160);
    }

    std::string indicator() const {
        switch (status) {
            case OK:   return "[OK]";
            case WARN: return "[!!]";
            case ERR:  return "[XX]";
        }
        return "[??]";
    }
};

class Diagnostics {
public:
    /// Run all hardware probes and populate the items list.
    void probe();

    /// Draw the diagnostics overlay centred on the current window.
    void draw();

    /// Returns true once the overlay should be dismissed.
    bool shouldDismiss() const;

    /// Call from keyPressed — any key dismisses immediately.
    void onKeyPressed();

    /// Call every frame to tick the auto-dismiss timer.
    void update(float dt);

    /// True while the overlay is still being shown.
    bool isActive() const { return active; }

private:
    std::vector<DiagnosticItem> items;
    float elapsed  = 0.0f;
    float duration = 3.0f;   // auto-dismiss after 3 seconds
    bool  active   = true;
    bool  keyDismissed = false;
    bool  probed   = false;
};
