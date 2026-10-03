/***********************************************************************
ErosionParams.h - Tuning shared by both water simulation backends for the
erosion and sedimentation layer (shaders/water/adapted/Erosion.frag and
shaders/water/compute/erosion.glsl).

Bathymetry and water depth are in normalized elevation units; speeds are in
cells per second.

This file is part of DuneBox, a fork of Magic Sand.
***********************************************************************/

#pragma once

namespace ErosionParams {
    constexpr float capacity = 1.0f;     // carried sediment per (speed x depth)
    constexpr float erodeRate = 0.5f;    // per second
    constexpr float depositRate = 1.0f;  // per second
    constexpr float fade = 0.03f;        // marks halve in about 20 s
    constexpr float resetDelta = 0.02f;  // terrain change that wipes the marks
    constexpr float maxBed = 0.05f;
    constexpr float displayGain = 14.0f; // bed change to overlay opacity
}
