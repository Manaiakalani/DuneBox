/***********************************************************************
ColorTheme - Predefined color themes (biome modes) for the sand surface.
Each theme defines an elevation color gradient, water/contour colors,
and optional rendering flags (e.g., lava mode).

Copyright (c) 2025

This file is part of DuneBox / Magic Sand.

The Magic Sand is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the
License, or (at your option) any later version.
***********************************************************************/

#pragma once

#include "ofMain.h"
#include "ColorMap.h"
#include <string>
#include <vector>

struct ColorTheme {
    std::string name;
    std::vector<ColorMap::HeightMapKey> elevationKeys;
    ofColor waterColor;
    ofColor contourColor;
    ofColor backgroundColor;
    bool lavaMode;

    ColorTheme()
        : name("Topo"),
          waterColor(24, 140, 205, 200),
          contourColor(0, 0, 0, 180),
          backgroundColor(0, 0, 0),
          lavaMode(false) {}
};

class ColorThemeManager {
public:
    ColorThemeManager();

    int          getThemeCount() const;
    int          getCurrentIndex() const;
    const ColorTheme& getTheme(int index) const;
    const ColorTheme& getCurrentTheme() const;

    /// Set current theme by index (clamped to valid range). Returns the new index.
    int          setTheme(int index);

    /// Advance to the next theme, wrapping around. Returns the new index.
    int          cycleTheme();

private:
    void buildThemes();

    std::vector<ColorTheme> themes;
    int currentIndex;
};
