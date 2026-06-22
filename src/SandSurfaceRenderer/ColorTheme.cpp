/***********************************************************************
ColorTheme - Predefined color themes (biome modes) for the sand surface.
Copyright (c) 2025

This file is part of DuneBox / Magic Sand.

The Magic Sand is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the
License, or (at your option) any later version.
***********************************************************************/

#include "ColorTheme.h"

using Key = ColorMap::HeightMapKey;

ColorThemeManager::ColorThemeManager()
    : currentIndex(0) {
    buildThemes();
}

int ColorThemeManager::getThemeCount() const {
    return (int)themes.size();
}

int ColorThemeManager::getCurrentIndex() const {
    return currentIndex;
}

const ColorTheme& ColorThemeManager::getTheme(int index) const {
    return themes[ofClamp(index, 0, (int)themes.size() - 1)];
}

const ColorTheme& ColorThemeManager::getCurrentTheme() const {
    return themes[currentIndex];
}

int ColorThemeManager::setTheme(int index) {
    currentIndex = ofClamp(index, 0, (int)themes.size() - 1);
    return currentIndex;
}

int ColorThemeManager::cycleTheme() {
    currentIndex = (currentIndex + 1) % (int)themes.size();
    return currentIndex;
}

void ColorThemeManager::buildThemes() {
    themes.clear();

    // -- 1. Topo (default) -- classic terrain: ocean -> green -> brown -> snow
    {
        ColorTheme t;
        t.name = "Topo";
        t.elevationKeys = {
            Key(-220.0f, ofColor(0,   0,   0)),
            Key(-200.0f, ofColor(0,   0,  80)),
            Key(-170.0f, ofColor(0,  30, 100)),
            Key(-150.0f, ofColor(0,  50, 102)),
            Key(-125.0f, ofColor(19, 108, 160)),
            Key(  -7.5f, ofColor(24, 140, 205)),
            Key(  -2.5f, ofColor(135, 206, 250)),
            Key(  -0.5f, ofColor(176, 226, 255)),
            Key(   0.0f, ofColor(0,   97,  71)),
            Key(   2.5f, ofColor(16, 122,  47)),
            Key(  25.0f, ofColor(232, 215, 125)),
            Key(  60.0f, ofColor(161,  67,   0)),
            Key(  90.0f, ofColor(130,  30,  30)),
            Key( 140.0f, ofColor(161, 161, 161)),
            Key( 200.0f, ofColor(206, 206, 206)),
            Key( 220.0f, ofColor(255, 255, 255)),
        };
        t.waterColor      = ofColor(24, 140, 205, 200);
        t.contourColor    = ofColor(0, 0, 0, 180);
        t.backgroundColor = ofColor(0, 0, 0);
        t.lavaMode        = false;
        themes.push_back(t);
    }

    // -- 2. Ocean -- deep sea blues, coral mid-tones, white foam peaks
    {
        ColorTheme t;
        t.name = "Ocean";
        t.elevationKeys = {
            Key(-220.0f, ofColor(2,   10,  40)),
            Key(-200.0f, ofColor(3,   20,  60)),
            Key(-170.0f, ofColor(5,   35,  90)),
            Key(-140.0f, ofColor(10,  55, 120)),
            Key(-100.0f, ofColor(15,  80, 150)),
            Key( -50.0f, ofColor(30, 120, 180)),
            Key(  -5.0f, ofColor(60, 170, 210)),
            Key(  -0.5f, ofColor(100, 200, 230)),
            Key(   0.0f, ofColor(194, 178, 128)),  // sand
            Key(  10.0f, ofColor(210, 180, 140)),
            Key(  30.0f, ofColor(220, 140, 100)),   // coral
            Key(  60.0f, ofColor(200, 120, 110)),
            Key(  90.0f, ofColor(180, 160, 150)),
            Key( 140.0f, ofColor(220, 225, 230)),
            Key( 200.0f, ofColor(240, 248, 255)),
            Key( 220.0f, ofColor(255, 255, 255)),   // foam
        };
        t.waterColor      = ofColor(40, 190, 220, 200);
        t.contourColor    = ofColor(10, 40, 80, 160);
        t.backgroundColor = ofColor(2, 10, 30);
        t.lavaMode        = false;
        themes.push_back(t);
    }

    // -- 3. Volcanic -- dark basalt, red-orange lava glow at low elevations
    {
        ColorTheme t;
        t.name = "Volcanic";
        t.elevationKeys = {
            Key(-220.0f, ofColor(180,  30,   0)),   // deep lava glow
            Key(-200.0f, ofColor(200,  50,   0)),
            Key(-170.0f, ofColor(220,  80,  10)),
            Key(-140.0f, ofColor(200,  60,   5)),
            Key(-100.0f, ofColor(160,  30,   0)),
            Key( -50.0f, ofColor(120,  15,   0)),
            Key( -10.0f, ofColor(80,   10,   5)),
            Key(  -0.5f, ofColor(50,    5,   0)),
            Key(   0.0f, ofColor(30,   28,  26)),   // dark basalt
            Key(  10.0f, ofColor(45,   42,  38)),
            Key(  40.0f, ofColor(60,   55,  48)),
            Key(  80.0f, ofColor(75,   70,  62)),
            Key( 120.0f, ofColor(90,   85,  78)),
            Key( 160.0f, ofColor(110, 105,  95)),
            Key( 200.0f, ofColor(140, 130, 115)),
            Key( 220.0f, ofColor(170, 155, 135)),   // ash
        };
        t.waterColor      = ofColor(230, 90, 10, 220);
        t.contourColor    = ofColor(200, 60, 0, 140);
        t.backgroundColor = ofColor(10, 5, 0);
        t.lavaMode        = true;
        themes.push_back(t);
    }

    // -- 4. Ice Age -- whites, light blues, frozen tundra
    {
        ColorTheme t;
        t.name = "Ice Age";
        t.elevationKeys = {
            Key(-220.0f, ofColor(10,  20,  50)),
            Key(-200.0f, ofColor(20,  40,  80)),
            Key(-170.0f, ofColor(30,  60, 110)),
            Key(-140.0f, ofColor(50,  90, 140)),
            Key(-100.0f, ofColor(70, 120, 170)),
            Key( -50.0f, ofColor(100, 150, 200)),
            Key( -10.0f, ofColor(140, 180, 220)),
            Key(  -0.5f, ofColor(170, 200, 235)),   // frozen surface
            Key(   0.0f, ofColor(200, 215, 230)),
            Key(  10.0f, ofColor(210, 225, 235)),
            Key(  30.0f, ofColor(215, 230, 240)),
            Key(  60.0f, ofColor(225, 235, 245)),
            Key( 100.0f, ofColor(235, 242, 250)),
            Key( 150.0f, ofColor(245, 250, 255)),
            Key( 200.0f, ofColor(250, 253, 255)),
            Key( 220.0f, ofColor(255, 255, 255)),   // pure snow
        };
        t.waterColor      = ofColor(140, 190, 230, 180);
        t.contourColor    = ofColor(80, 110, 150, 140);
        t.backgroundColor = ofColor(10, 15, 25);
        t.lavaMode        = false;
        themes.push_back(t);
    }

    // -- 5. Alien -- purples, teals, neon green water
    {
        ColorTheme t;
        t.name = "Alien";
        t.elevationKeys = {
            Key(-220.0f, ofColor(10,   0,  30)),
            Key(-200.0f, ofColor(20,   5,  60)),
            Key(-170.0f, ofColor(40,  10,  90)),
            Key(-140.0f, ofColor(60,  15, 120)),
            Key(-100.0f, ofColor(80,  30, 140)),
            Key( -50.0f, ofColor(100,  50, 160)),
            Key( -10.0f, ofColor(120,  70, 170)),
            Key(  -0.5f, ofColor(140,  90, 180)),
            Key(   0.0f, ofColor(0,  120, 110)),     // teal surface
            Key(  10.0f, ofColor(0,  140, 120)),
            Key(  30.0f, ofColor(20, 160, 130)),
            Key(  60.0f, ofColor(50, 140, 150)),
            Key(  90.0f, ofColor(90, 120, 160)),
            Key( 140.0f, ofColor(130, 100, 170)),
            Key( 200.0f, ofColor(170, 130, 200)),
            Key( 220.0f, ofColor(210, 180, 240)),    // pale lavender peaks
        };
        t.waterColor      = ofColor(0, 255, 100, 200);
        t.contourColor    = ofColor(0, 200, 150, 160);
        t.backgroundColor = ofColor(5, 0, 15);
        t.lavaMode        = false;
        themes.push_back(t);
    }
}
