# 🏜️ DuneBox

> *Like a sandbox, but epic.*

A cross-platform augmented reality sandbox that projects real-time topographic maps and GPU-accelerated water simulation onto physical sand using a Kinect depth camera and projector.

![Status](https://img.shields.io/badge/status-proof%20of%20concept-orange)
![Language](https://img.shields.io/badge/lang-C%2B%2B-blue)
![Framework](https://img.shields.io/badge/framework-OpenFrameworks-lightgrey)

## Features

- **Topographic mapping** — real-time contour lines and elevation color coding at 60 FPS
- **GPU water simulation** — shallow-water equations (Saint-Venant) solved via RK2 integration on GPU
- **Rain gesture** — wave your hand above the sand to make it rain; water flows downhill realistically
- **Interactive games** — fish, sharks, island shaping, and more
- **Auto-calibration** — chessboard-based projector-Kinect alignment
- **No-Kinect mode** — procedural terrain for development/testing without hardware

## Hardware Requirements

| Component | Recommendation |
|---|---|
| **Kinect** | Kinect v1 (Xbox 360) — best supported |
| **Projector** | Short-throw, 4:3 aspect, HDMI output |
| **PC** | x86 quad-core + Nvidia GPU for water sim (Quadro P620 minimum) |
| **Sandbox** | 40"×30" plywood box (4:3 ratio), Sandtastik White Play Sand |

## Quick Start

### Windows (easiest — no build tools needed)
```powershell
# One-line setup (run as admin):
Set-ExecutionPolicy Bypass -Scope Process -Force
git clone https://github.com/Manaiakalani/DuneBox-docs.git
cd DuneBox-docs\scripts; .\setup-windows.ps1
```
This downloads the pre-built app, creates desktop shortcuts, and you're done.

Or manually: clone this repo and double-click **`run.bat`** — it auto-downloads the latest release.

### Build from source (if you want to modify the code)
```bash
# 1. Install OpenFrameworks 0.12.0 from openframeworks.cc
# 2. Clone into openFrameworks/apps/myApps/DuneBox
# 3. Install addons: ofxCv, ofxDatGui, ofxParagraph, ofxModal
# 4. Build:
#    Windows: Open Magic-Sand.sln → x64 Release → Build
#    macOS:   Open Magic-Sand.xcodeproj → Build
#    Linux:   make && make run
```

Press **`w`** to toggle water simulation. Works without a Kinect (test terrain fallback).

## Project Structure

```
DuneBox/
├── src/
│   ├── ofApp.cpp/h               # Main application
│   ├── KinectProjector/           # Kinect depth processing + calibration
│   ├── SandSurfaceRenderer/       # Topographic color map + contours
│   ├── WaterSimulation/           # GPU water sim (extracted from SARndbox)
│   └── Games/                     # Interactive creatures & games
├── bin/data/shaders/water/        # GLSL water simulation shaders
│   ├── adapted/                   # GLSL 150 core-profile adapted shaders
│   └── SHADER_ANALYSIS.md         # Shader pipeline documentation
└── docs/
    └── RENDER_PIPELINE_ANALYSIS.md
```

## Water Simulation

The water simulation uses GLSL shaders extracted from [SARndbox](https://github.com/KeckCAVES/SARndbox) and adapted to run cross-platform via OpenFrameworks `ofFbo` multi-pass rendering.

**Pipeline (per frame):**
1. Bathymetry update (sync terrain with Kinect depth)
2. Slope + flux + derivative computation (Kurganov-Petrova scheme)
3. Euler predictor step (RK2)
4. Recompute derivatives at predicted state
5. Runge-Kutta corrector step
6. Boundary enforcement
7. Rain addition (hand gesture) + evaporation
8. Water color rendering + compositing

## Keyboard Controls

| Key | Action |
|---|---|
| `w` | Toggle water simulation |
| `c` | Start calibration |
| `space` | Pause/resume |

## See Also

- **[DuneBox-sandcam](https://github.com/Manaiakalani/DuneBox-sandcam)** — Python companion project with ArUco marker triggers and biome creatures

## Credits & Acknowledgments

DuneBox builds on the work of the open-source AR sandbox community:

- **[Magic-Sand](https://github.com/thomwolf/Magic-Sand)** by Thomas Wolf & Rasmus R. Paulsen (DTU Copenhagen) — the cross-platform OpenFrameworks AR sandbox this project is derived from. Licensed under GPL-2.0.

- **[SARndbox](https://github.com/KeckCAVES/SARndbox)** by Oliver Kreylos (UC Davis / KeckCAVES) — the original AR sandbox. The water simulation GLSL shaders in `bin/data/shaders/water/` are extracted and adapted from this project. Licensed under GPL-2.0. [Official site](https://arsandbox.ucdavis.edu/)

- **[sARndbox erosion mod](https://github.com/danigeos/sARndbox)** by danigeos — erosion and sedimentation simulation
- **[ARSandbox-Adds](https://github.com/RiverWeyTrust/ARSandbox-Adds)** by River Wey Trust — weather effects (lava, snow)
- [r/arsandbox](https://reddit.com/r/arsandbox) — community subreddit

## License

Licensed under [GPL-2.0](COPYING), inherited from Magic-Sand and SARndbox.

## Build Guide

See **[DuneBox-docs](https://github.com/Manaiakalani/DuneBox-docs)** for the complete build guide — hardware, physical construction, software setup, calibration, troubleshooting, and customization.
