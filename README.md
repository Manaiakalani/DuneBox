# 🏜️ DuneBox

> *Like a sandbox, but epic.*

A cross-platform augmented reality sandbox that projects real-time topographic maps and GPU-accelerated water simulation onto physical sand using a Kinect depth camera and projector.

![Status](https://img.shields.io/badge/status-proof%20of%20concept-orange)
![Language](https://img.shields.io/badge/lang-C%2B%2B-blue)
![Framework](https://img.shields.io/badge/framework-OpenFrameworks-lightgrey)
[![Build & Release](https://github.com/Manaiakalani/DuneBox/actions/workflows/build.yml/badge.svg)](https://github.com/Manaiakalani/DuneBox/actions/workflows/build.yml)

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
| **Depth Sensor** | Kinect v1 (default), Kinect v2, Azure Kinect, or Orbbec Femto Bolt |
| **Projector** | Short-throw, 4:3 aspect, HDMI output |
| **PC** | x86 quad-core + Nvidia GPU for water sim (Quadro P620 minimum) |
| **Sandbox** | 40"×30" plywood box (4:3 ratio), Sandtastik White Play Sand |

## Supported Sensors

| Sensor | `kinectVersion` | Addon Required | Depth Resolution | Compile Flag |
|---|---|---|---|---|
| Kinect v1 (Xbox 360) | `1` (default) | ofxKinect (built-in) | 640×480 | — |
| Kinect v2 (Xbox One) | `2` | ofxKinectForWindows2 + Kinect for Windows SDK 2.0 | 512×424 | `DUNEBOX_USE_KINECT_FOR_WINDOWS2` |
| Azure Kinect DK | `3` | ofxAzureKinect + Azure Kinect SDK | 640×576 | `DUNEBOX_USE_AZURE_KINECT` |
| Orbbec Femto Bolt | `3` | ofxAzureKinect + Azure Kinect SDK | 640×576 | `DUNEBOX_USE_AZURE_KINECT` |

### Switching sensors

Edit `bin/data/settings/kinectProjectorSettings.xml` and add/change:
```xml
<kinectVersion>2</kinectVersion>
```

Values: `1` = Kinect V1, `2` = Kinect V2, `3` = Azure Kinect / Orbbec Femto Bolt.

> **The pre-built Windows release already includes Kinect v2 support.** To use a
> Kinect v2 on Windows: install the [Kinect for Windows Runtime 2.0](https://www.microsoft.com/download/details.aspx?id=44559)
> (or the full SDK), connect the sensor via its powered adapter to a **USB 3.0**
> port, set `<kinectVersion>2</kinectVersion>` in
> `bin/data/settings/kinectProjectorSettings.xml`, and launch. No rebuild needed.

### Building with Kinect V2 support (Windows)
The Windows CI build links Kinect v2 automatically. To reproduce locally:
1. Install the [Kinect for Windows SDK 2.0](https://www.microsoft.com/download/details.aspx?id=44561)
   (sets the `KINECTSDK20_DIR` environment variable).
2. Clone [ofxKinectForWindows2](https://github.com/elliotwoods/ofxKinectForWindows2)
   into `openFrameworks/addons/`.
3. Add `ofxKinectForWindows2` to `addons.make`.
4. Add `DUNEBOX_USE_KINECT_FOR_WINDOWS2` to the project's preprocessor definitions.
5. Regenerate the project (projectGenerator) and rebuild.

### Building with Azure Kinect / Orbbec Femto Bolt support
1. Install [Azure Kinect SDK v1.4+](https://learn.microsoft.com/azure/kinect-dk/sensor-sdk-download)
2. Clone [ofxAzureKinect](https://github.com/prisonerjohn/ofxAzureKinect) into `openFrameworks/addons/`
3. Add `ofxAzureKinect` to `addons.make`
4. Add `-DDUNEBOX_USE_AZURE_KINECT` to compiler flags
5. Rebuild

> **Note:** The Orbbec Femto Bolt uses Azure Kinect-compatible firmware and works with the same SDK/addon.

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

### Put it on your desktop
Double-click **`Install Desktop Shortcut.cmd`** once to add a **DuneBox
(Magic-Sand C++)** icon to your desktop, then launch it with a double-click.
The installer is safe to re-run. (If you also have the `DuneBox-sandcam` repo
checked out, its `Install Desktop Shortcuts.cmd` sets up both apps at once.)

### Build from source (if you want to modify the code)
This repo ships **no IDE project files** — generate them for your platform with
OpenFrameworks' projectGenerator (the committed app is otherwise OF-0.9 stale).

> **Git LFS:** the reference map imagery under `bin/data/` is stored with
> [Git LFS](https://git-lfs.com). Install it (`git lfs install`) before cloning
> so those assets download as real files rather than pointer stubs.
```bash
# 1. Install OpenFrameworks 0.12.0 from openframeworks.cc
# 2. Clone this repo into  openFrameworks/apps/myApps/Magic-Sand
#    (keep the folder name "Magic-Sand" so the binary is Magic-Sand.exe)
# 3. Install community addons into openFrameworks/addons/:
#      ofxCv, ofxParagraph, ofxModal, and the thomwolf fork of ofxDatGui
#      (ofxKinect, ofxOpenCv, ofxXmlSettings ship with OpenFrameworks)
# 4. Generate the project for your platform, then build:
#    Windows: run projectGenerator (import the folder) -> open the generated
#             Magic-Sand.sln -> x64 Release -> Build
#    macOS:   run projectGenerator (import the folder) -> open the generated
#             Magic-Sand.xcodeproj -> Build
#    Linux:   make && make run
```
> The CI workflow (`.github/workflows/build.yml`) is the reference for the exact
> addon pins and build flags. It builds and releases on **Windows** (MSVC, via
> projectGenerator) and compile-checks every push on **Linux** (GCC, via
> OpenFrameworks' template Makefiles); a **macOS** build (Clang) runs on demand
> via *Run workflow* and on release tags. Windows is the only release target.

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
| `w` | Toggle water simulation on/off |
| `l` | Toggle lava simulation (auto-switches to Volcanic theme) |
| `t` | Cycle color themes (Topo → Ocean → Volcanic → Ice Age → Alien) |
| `n` | Toggle day/night cycle |
| `v` | Trigger volcano eruption at center |
| `b` | Send ping to Python bridge |
| `space` | Start map game (if idle) / advance game step / start app from setup |
| `f` or `r` | Start fish game (boid mode 2) / end map game |
| `1`–`4` | Start boid game at difficulty 0–3 |
| `m` | Start "seek mother" game |
| `c` | Save Kinect color image to disk |
| `d` | Save filtered depth image to disk |
| `T` | Run real-time test (debug) |
| `W` | Run debug test |

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
