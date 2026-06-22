/***********************************************************************
Diagnostics.h - startup hardware diagnostics report

Probes Kinect, GPU, OpenGL, displays, shader compilation, and settings
files at launch and prints the results to the console log. It deliberately
does NOT draw an on-screen overlay: in the two-window (main GUI + projector)
layout a full-window overlay covered the operator GUI and could remain on
screen, making the app look frozen.

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
    /// Run all hardware probes and populate the items list. The Kinect status
    /// is supplied by the caller (which owns the real sensor handle) so the
    /// overlay reports the configured Kinect version (v1/v2) correctly instead
    /// of probing only the legacy v1 API.
    void probe(int kinectVersion, bool kinectConnected);

    /// Print the probe results to the console log. We deliberately do NOT draw
    /// a blocking on-screen overlay: in the two-window (main GUI + projector)
    /// layout an overlay covers the main window's GUI and could remain on
    /// screen, making the app look frozen. The log is just as informative.
    void log() const;

private:
    std::vector<DiagnosticItem> items;
    bool  probed   = false;
};
