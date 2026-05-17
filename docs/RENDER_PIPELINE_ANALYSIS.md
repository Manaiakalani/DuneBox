# DuneBox Render Pipeline Analysis

> Analysis of the Magic-Sand (DuneBox) render pipeline for SARndbox water simulation integration.

---

## 1. Architecture Overview

### Data Flow Diagram

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        KINECT HARDWARE                                  │
│                   (ofxKinect, 640×480 depth)                            │
└──────────────┬──────────────────────────────────────────────────────────┘
               │ Raw depth (unsigned short) + RGB pixels
               ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                    KinectGrabber (separate thread)                       │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │  1. kinect.update() → getRawDepthPixels()                       │   │
│  │  2. filter() → temporal averaging + variance check              │   │
│  │  3. applySimpleOutlierInpainting() (optional)                   │   │
│  │  4. applySpaceFilter() (optional spatial smoothing)             │   │
│  │  5. updateGradientField() → compute slope vectors               │   │
│  └──────────────────────────────────────────────────────────────────┘   │
│  Output channels (ofThreadChannel):                                     │
│    filtered  → ofFloatPixels (640×480, single-channel float depth)      │
│    colored   → ofPixels (640×480, RGB color image)                      │
│    gradient  → ofVec2f* (gradient field array)                          │
└──────────────┬──────────────────────────────────────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                    KinectProjector (main thread)                        │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │  update():                                                      │   │
│  │  1. Receive filtered depth → FilteredDepthImage (ofxCvFloat)    │   │
│  │  2. FilteredDepthImage.updateTexture()                          │   │
│  │  3. Receive color image → kinectColorImage                      │   │
│  │  4. Receive gradient field → gradField                          │   │
│  │  5. Draw debug view into fboMainWindow                          │   │
│  │  6. Draw calibration overlays into fboProjWindow                │   │
│  └──────────────────────────────────────────────────────────────────┘   │
│  Exposes:                                                               │
│    getTexture() → FilteredDepthImage.getTexture() (sampler2DRect)      │
│    bind()/unbind() → bind depth texture to GL tex unit 0               │
│    getTransposedKinectWorldMatrix() → kinect→world transform           │
│    getTransposedKinectProjMatrix() → world→projector transform         │
│    getBasePlaneEq() → vec4 plane equation                              │
│    elevationAtKinectCoord(x,y) → float elevation in mm                 │
│    gradientAtKinectCoord(x,y) → ofVec2f slope                         │
│    kinectCoordToProjCoord(x,y) → ofVec2f projector pixel               │
└──────────────┬──────────────────────────────────────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                 SandSurfaceRenderer (main thread)                       │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │  update():                                                      │   │
│  │  1. Check for ROI/basePlane/calibration changes                 │   │
│  │  2. prepareContourLinesFbo()   ← PASS 1 (elevation FBO)        │   │
│  │  3. drawSandbox()              ← PASS 2 (color + contours)     │   │
│  └──────────────────────────────────────────────────────────────────┘   │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │  PASS 1: Contour Line Elevation FBO                             │   │
│  │    FBO: contourLineFramebufferObject (projResX+1 × projResY+1)  │   │
│  │    Shader: elevationShader                                      │   │
│  │    Input: FilteredDepthImage texture (tex0, bound via KP)       │   │
│  │    Output: Per-pixel elevation in red channel (0..1 range)      │   │
│  │    Purpose: Half-pixel elevation map for contour edge detect    │   │
│  └──────────────────────────────────────────────────────────────────┘   │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │  PASS 2: Height Color Map + Contour Lines                       │   │
│  │    FBO: fboProjWindow (projResX × projResY, GL_RGBA)            │   │
│  │    Shader: heightMapShader                                      │   │
│  │    Inputs:                                                      │   │
│  │      tex0 (unit 0) = FilteredDepthImage texture                 │   │
│  │      heightColorMapSampler (unit 2) = color map 1D texture      │   │
│  │      pixelCornerElevationSampler (unit 3) = contour line FBO    │   │
│  │    Output: Final colored topographic image with contour lines   │   │
│  └──────────────────────────────────────────────────────────────────┘   │
└──────────────┬──────────────────────────────────────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                    ofApp Draw Compositing                               │
│                                                                         │
│  drawProjWindow() — sequential draw to projector:                      │
│    1. sandSurfaceRenderer->drawProjectorWindow()  ← terrain+contours   │
│    2. mapGameController.drawProjectorWindow()     ← map game overlay   │
│    3. boidGameController.drawProjectorWindow()    ← boid game overlay  │
│    4. kinectProjector->drawProjectorWindow()      ← calibration/debug  │
│                                                                         │
│  All layers draw their pre-rendered FBOs at (0,0) on the projector     │
│  window using default alpha blending (painter's algorithm).            │
└──────────────┬──────────────────────────────────────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                     PROJECTOR OUTPUT                                    │
│              (second GLFW window, full-screen on monitor 1)            │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Key Classes and Their Roles

| Class | File | Role |
|-------|------|------|
| `KinectGrabber` | `src/KinectProjector/KinectGrabber.{h,cpp}` | Threaded Kinect acquisition + depth filtering. Runs `ofxKinect`, applies temporal averaging (multi-slot running mean/variance), optional inpainting, optional spatial smoothing, and gradient field computation. Communicates via `ofThreadChannel`. |
| `KinectProjector` | `src/KinectProjector/KinectProjector.{h,cpp}` | Central hub: receives filtered depth from grabber, manages coordinate transforms (kinect↔world↔projector), handles calibration workflow, exposes depth texture and matrices for shaders. Owns `FilteredDepthImage` (ofxCvFloatImage). |
| `SandSurfaceRenderer` | `src/SandSurfaceRenderer/SandSurfaceRenderer.{h,cpp}` | Two-pass GPU rendering: elevation FBO for contour lines, then final color mapping with height color map texture lookup. Owns both shaders and both rendering FBOs. |
| `ColorMap` | `src/SandSurfaceRenderer/ColorMap.{h,cpp}` | Manages height→color lookup table. Loads/saves XML color map files. Generates a 512-entry `ofTexture` used as a 1D lookup in the `heightMapShader`. |
| `ofApp` | `src/ofApp.{h,cpp}` | Main application: orchestrates update/draw loop, composites all layers, manages two GLFW windows (main UI + projector). |
| `CBoidGameController` | `src/Games/BoidGameController.{h,cpp}` | Fish/rabbit/shark BOID game. Draws animated sprites into its own `fboVehicles` (projector-resolution), composited on top of terrain via alpha blending. |
| `CMapGameController` | `src/Games/MapGameController.{h,cpp}` | Map/geography game. Draws reference maps and score overlays into its own `fboProjWindow`. |
| `ofxKinectProjectorToolkit` | `src/KinectProjector/KinectProjectorCalibration.{h,cpp}` | Computes the 4×4 kinect→projector calibration matrix from chessboard correspondences using DLT (dlib). |

---

## 3. Depth Data Flow (Detailed)

### 3.1 Acquisition
- **Source**: `ofxKinect` (libfreenect wrapper for Kinect v1)
- **Resolution**: 640×480 (hardcoded in ofxKinect)
- **Raw format**: `unsigned short` (RawDepth), depth in mm, 0 = invalid, range ~400–4000mm
- **Registration**: `kinect.setRegistration(true)` — RGB/depth are aligned
- **File**: `KinectGrabber.cpp:59-69`

### 3.2 Filtering (in KinectGrabber thread)
1. **Temporal averaging** (`filter()`, line 198): Multi-slot running average with variance check. Only pixels within `maxOffset` (below ceiling plane) are considered. Hysteresis prevents jitter.
2. **Outlier inpainting** (`applySimpleOutlierInpainting()`): Replaces invalid pixels (0 or 4000) with neighborhood averages.
3. **Spatial smoothing** (`applySpaceFilter()`): Optional 3×3 average within ROI.
4. **Output**: `filteredframe` — `ofFloatPixels`, 640×480, single-channel float, values in raw Kinect depth units (mm from sensor).

### 3.3 Transfer to Main Thread
- Via `ofThreadChannel<ofFloatPixels> filtered` — lock-free SPSC channel
- `KinectProjector::update()` receives at line 283:
  ```cpp
  FilteredDepthImage.setFromPixels(filteredframe.getData(), kinectRes.x, kinectRes.y);
  FilteredDepthImage.updateTexture();
  ```
- **Result**: `FilteredDepthImage` is an `ofxCvFloatImage` (640×480 float), with its GL texture updated each frame.

### 3.4 Texture for Shaders
- `FilteredDepthImage.getTexture()` → `GL_TEXTURE_RECTANGLE` (sampler2DRect)
- Bound via `kinectProjector->bind()` which calls `FilteredDepthImage.getTexture().bind()` (to texture unit 0)
- **Native scale normalization**: `setNativeScale(scaleMin, scaleMax)` maps raw mm values to 0..1 range before upload to GPU
  - `scaleMin = basePlaneOffset.z + elevationMax` (furthest valid depth)
  - `scaleMax = basePlaneOffset.z + elevationMin` (closest valid depth)
- In the vertex shader, the normalized value is denormalized: `depth = texel.r * depthTransformation.x + depthTransformation.y`

### 3.5 Coordinate Transforms
Three coordinate spaces:
1. **Kinect image space**: (x, y) pixel + z depth in mm
2. **World space**: 3D mm coordinates, computed via `kinectWorldMatrix * (x,y,z,1) * z`
3. **Projector image space**: 2D pixel, computed via `kinectProjMatrix * worldPos`, then perspective divide by z

The base plane equation (`basePlaneEq`, vec4) defines sea level: `elevation = dot(basePlaneEq, worldPos)`

---

## 4. Render Loop Step-by-Step

### 4.1 `ofApp::update()` (line 64)
```
1. kinectProjector->update()
   └── Receive filtered depth from grabber thread
   └── Update FilteredDepthImage texture
   └── Receive color + gradient
   └── Draw debug views to fboMainWindow
   └── Draw calibration overlays to fboProjWindow

2. sandSurfaceRenderer->update()
   └── Check for ROI/basePlane/calibration changes
   └── PASS 1: prepareContourLinesFbo()
   │   └── contourLineFramebufferObject.begin()
   │   └── Bind depth texture (tex unit 0)
   │   └── elevationShader: transform mesh vertices through kinect→world→projector
   │   └── Output: per-vertex elevation normalized to [0,1] in red channel
   │   └── contourLineFramebufferObject.end()
   └── PASS 2: drawSandbox()
       └── fboProjWindow.begin()
       └── Bind depth texture (tex unit 0)
       └── heightMapShader with:
       │   ├── tex unit 2: heightColorMapSampler (1D color LUT)
       │   └── tex unit 3: pixelCornerElevationSampler (contour FBO)
       └── Vertex shader: depth → world → elevation → color map coord
       └── Fragment shader: color lookup + contour line edge detection
       └── fboProjWindow.end()

3. mapGameController.update()
4. boidGameController.update()
```

### 4.2 `ofApp::drawProjWindow()` (line 98)
```
Sequential draw to projector window (painter's algorithm):
1. sandSurfaceRenderer->drawProjectorWindow()  → fboProjWindow.draw(0,0)
2. mapGameController.drawProjectorWindow()      → own fboProjWindow.draw(0,0)
3. boidGameController.drawProjectorWindow()     → fboVehicles.draw(0,0)
4. kinectProjector->drawProjectorWindow()       → own fboProjWindow.draw(0,0)

All FBOs are GL_RGBA — alpha channel controls transparency for compositing.
Games use ofClear(255,255,255,0) to start transparent, then draw opaque sprites.
```

---

## 5. Shaders

### 5.1 Shader Directory Structure
```
bin/data/shaders/
├── shadersGL2/          ← GLSL 1.20 (legacy fixed-function)
│   ├── elevationShader.vert
│   ├── elevationShader.frag
│   ├── heightMapShader.vert
│   └── heightMapShader.frag
└── shadersGL3/          ← GLSL 1.50 (programmable pipeline)  ← DEFAULT ON MODERN macOS
    ├── elevationShader.vert
    ├── elevationShader.frag
    ├── heightMapShader.vert
    └── heightMapShader.frag
```

### 5.2 Shader Selection Logic
```cpp
// SandSurfaceRenderer.cpp:116
if (ofIsGLProgrammableRenderer()) {
    // GL3+ path → shadersGL3/ (GLSL 150)
} else {
    // Legacy path → shadersGL2/ (GLSL 120)
}
```

### 5.3 Elevation Shader (Pass 1)
- **Purpose**: Render per-pixel elevation values into contour line FBO
- **Vertex**: Reads depth from `tex0`, transforms through kinectWorld→basePlane dot product, outputs normalized elevation as `depthfrag`
- **Fragment**: Writes `depthfrag` directly to red channel: `outputColor = vec4(depthfrag, 0, 0, 1)`

### 5.4 Height Map Shader (Pass 2)
- **Purpose**: Final terrain coloring with contour lines
- **Vertex**: Same depth→world→elevation pipeline, but outputs `depthfrag` as a color map texture coordinate
- **Fragment**:
  1. Look up color from `heightColorMapSampler` using elevation coordinate
  2. If contour lines enabled: sample 4 pixel corners from `pixelCornerElevationSampler`, check if any contour line crosses the pixel, color black if so
  3. Output final RGBA color

### 5.5 Mesh Geometry
- **Type**: `ofMesh` (indexed triangle list)
- **Size**: ROI width × ROI height vertices (one per Kinect pixel in ROI)
- **Vertex positions**: Kinect pixel coordinates (x, y, 0) — z set from depth texture in shader
- **Tex coords**: Same as vertex positions (sampler2DRect uses pixel coordinates)
- **File**: `SandSurfaceRenderer.cpp:179-207`

---

## 6. FBO Inventory

| FBO | Owner | Size | Format | Purpose |
|-----|-------|------|--------|---------|
| `contourLineFramebufferObject` | SandSurfaceRenderer | projResX+1 × projResY+1 | GL_RGBA | Elevation values for contour line edge detection |
| `fboProjWindow` | SandSurfaceRenderer | projResX × projResY | GL_RGBA | Final terrain render (colors + contours) |
| `fboProjWindow` | KinectProjector | projRes.x × projRes.y | GL_RGBA | Calibration overlays, ROI display |
| `fboMainWindow` | KinectProjector | kinectRes.x × kinectRes.y | GL_RGBA | Debug depth/color view |
| `fboProjWindow` | CMapGameController | projRes.x × projRes.y | GL_RGBA | Map game overlays |
| `fboVehicles` | CBoidGameController | projRes.x × projRes.y | GL_RGBA | Boid game animal sprites |

---

## 7. OpenGL Context

### 7.1 Version
- **No explicit GL version request** in `main.cpp` — openFrameworks defaults are used
- `ofGLFWWindowSettings` without `setGLVersion()` → OF defaults to **GL 2.1** or the **programmable renderer (GL 3.2+)** depending on OF version and platform
- Shader files provide **both GL2 (GLSL 120) and GL3 (GLSL 150)** variants
- Runtime check: `ofIsGLProgrammableRenderer()` selects the appropriate shader set
- **On modern macOS**: OF typically uses the programmable renderer → **OpenGL 3.2 core profile**, GLSL 150

### 7.2 Texture Type
- All depth textures are `GL_TEXTURE_RECTANGLE` (via ofxCvFloatImage/ofTexture defaults in OF)
- Shaders use `sampler2DRect` and `texture()` / `texture2DRect()` accordingly
- Color map texture is a standard `ofTexture` from `ofImage` (likely `GL_TEXTURE_2D` but used as `sampler2DRect`)

### 7.3 Multi-pass Rendering
- **Yes** — two FBO passes are already in use (elevation → contour, then final color)
- FBO allocation uses `GL_RGBA` (8-bit per channel)
- No depth attachments on FBOs (all 2D rendering)

---

## 8. Game System Integration Pattern

Games hook into the pipeline via a simple interface:
1. **`setup(kinectProjector)`** — receive shared pointer to KinectProjector for depth/coordinate queries
2. **`update()`** — game logic, draw into own FBO
3. **`drawProjectorWindow()`** — draw own FBO at (0,0) on projector (alpha-composited)
4. **`drawMainWindow(x,y,w,h)`** — draw debug view on main window

Games query the terrain via `kinectProjector->elevationAtKinectCoord(x,y)` (CPU-side) and `kinectProjector->gradientAtKinectCoord(x,y)` for slope. The boid game uses elevation to determine land vs. water (fish swim below sea level, rabbits walk above).

---

## 9. Recommended Integration Points for Water Simulation

### 9.1 Strategy: Insert Water Sim as a New Render Pass

The water simulation should be a **third render pass** between the existing terrain render and the game overlay compositing:

```
PASS 1: elevationShader → contourLineFramebufferObject  (existing)
PASS 2: heightMapShader → fboProjWindow                 (existing)
PASS 3: waterSimShader  → waterFBO (NEW)                 ← WATER SIMULATION
PASS 4: compositeShader → fboProjWindow                  ← BLEND WATER ON TERRAIN
```

### 9.2 Specific Insertion Point

**File**: `src/SandSurfaceRenderer/SandSurfaceRenderer.cpp`
**Method**: `SandSurfaceRenderer::update()` (line 209)
**Location**: After `drawSandbox()` (line 222), before GUI updates (line 225)

```cpp
// SandSurfaceRenderer.cpp:209
void SandSurfaceRenderer::update(){
    // ... existing checks ...

    if (drawContourLines)
        prepareContourLinesFbo();   // PASS 1
    drawSandbox();                  // PASS 2

    // ──── INSERT WATER SIMULATION HERE ────
    // updateWaterSimulation();     // PASS 3: Run SARndbox water sim shader
    // compositeWater();            // PASS 4: Blend water onto fboProjWindow
    // ──────────────────────────────────────

    // GUI updates follow...
}
```

### 9.3 Depth Map Input for Water Sim

The SARndbox water simulation needs an elevation/bathymetry texture. Two options:

**Option A — Use the contour line FBO (recommended)**:
- `contourLineFramebufferObject` already contains per-pixel elevation in the red channel
- Resolution: projector resolution (high quality)
- Already in projector coordinate space (no transform needed)
- **File**: `SandSurfaceRenderer.h:122` — `ofFbo contourLineFramebufferObject`

**Option B — Use the raw filtered depth texture**:
- `kinectProjector->getTexture()` — the `FilteredDepthImage` texture
- Resolution: 640×480 (Kinect resolution)
- In Kinect image space — would need the `kinectWorldMatrix` and `kinectProjMatrix` transforms
- Raw depth in mm, not elevation — would need `basePlaneEq` to compute elevation

**Recommendation**: Option A is better because it's already in projector space and contains elevation values. The water sim shader can sample it directly.

### 9.4 Water Simulation FBO

Add a new FBO to `SandSurfaceRenderer`:

```cpp
// In SandSurfaceRenderer.h:
ofFbo waterFBO;         // Water depth/velocity state (ping-pong double-buffered)
ofFbo waterFBO_back;    // Back buffer for ping-pong
ofFbo waterRenderFBO;   // Final water appearance for compositing
ofShader waterSimShader; // SARndbox water simulation compute shader
ofShader waterRenderShader; // Water surface rendering shader
```

### 9.5 Compositing Water onto Terrain

After the water sim pass, composite onto the existing `fboProjWindow`:

```cpp
void SandSurfaceRenderer::compositeWater() {
    fboProjWindow.begin();
    // Enable alpha blending
    ofEnableAlphaBlending();
    waterRenderFBO.draw(0, 0);  // Water layer on top of terrain
    ofDisableAlphaBlending();
    fboProjWindow.end();
}
```

This preserves the existing game overlay pipeline — games draw on top of (terrain + water).

### 9.6 Alternative: Integrate Water into heightMapShader

Instead of a separate composite pass, modify `heightMapShader.frag` to sample a water depth texture and blend water color when `waterDepth > 0`:

```glsl
uniform sampler2DRect waterDepthSampler;  // Water sim output
uniform int enableWater;

// In main():
if (enableWater == 1) {
    float waterDepth = texture(waterDepthSampler, gl_FragCoord.xy).r;
    if (waterDepth > 0.001) {
        vec4 waterColor = vec4(0.1, 0.3, 0.8, min(waterDepth * 2.0, 0.85));
        color = mix(color, waterColor, waterColor.a);
    }
}
```

This approach has the advantage of a single final pass but couples the water rendering tightly to the terrain shader.

---

## 10. Potential Issues and Blockers

### 10.1 OpenGL Version Compatibility
- **Risk**: SARndbox uses GLSL 400+ features (imageLoad/imageStore for compute-like water sim)
- **DuneBox status**: GLSL 150 (GL 3.2) on the GL3 path, GLSL 120 on the GL2 path
- **Impact**: SARndbox's `Water2UpdateShader.fs` uses `#version 400` and GL_ARB_shader_image_load_store
- **Mitigation**: On macOS, GL 4.1 is the max supported (via core profile). Must explicitly request `settings.setGLVersion(4, 1)` in `main.cpp`. On Linux, up to GL 4.6 is available.
- **Action required**: Modify `main.cpp` to request GL 4.1:
  ```cpp
  settings.setGLVersion(4, 1);
  ```
  Then provide GLSL 410 shader variants alongside existing GL2/GL3 shaders.

### 10.2 FBO Format Mismatch
- **Risk**: Current FBOs use `GL_RGBA` (8-bit). Water sim needs float-precision FBOs.
- **Water state texture**: Needs `GL_RGBA32F` or `GL_RG32F` for water depth + velocity
- **Mitigation**: Allocate water FBOs separately with float format:
  ```cpp
  waterFBO.allocate(projResX, projResY, GL_RGBA32F);
  ```
- The existing terrain FBOs (`GL_RGBA`) are fine — only water sim state needs float precision.

### 10.3 sampler2DRect vs sampler2D
- **Risk**: SARndbox shaders may use `sampler2D` while DuneBox uses `sampler2DRect`
- **Impact**: Texture coordinate conventions differ (rect uses pixel coords, 2D uses 0..1)
- **Mitigation**: Either port SARndbox shaders to use `sampler2DRect`, or allocate water FBOs with `ofFbo::Settings` using `textureTarget = GL_TEXTURE_2D`.

### 10.4 Depth Texture Resolution Mismatch
- Kinect depth: 640×480
- Projector: variable (typically 1024×768 or 1920×1080)
- Contour line FBO: projector resolution
- **Water sim should run at a reduced resolution** (e.g., 640×480 or lower) for performance, then be upsampled to projector resolution for display.

### 10.5 Ping-Pong Buffer Management
- SARndbox water sim requires reading from one buffer while writing to another (ping-pong)
- OF's `ofFbo` supports this but needs careful `begin()`/`end()` management
- Must not read and write the same FBO attachment simultaneously

### 10.6 Performance Budget
- Current pipeline: 2 shader passes (elevation + heightmap) at 60 FPS
- Water sim adds: 1+ simulation passes per frame + 1 render pass
- SARndbox runs water sim at a configurable sub-rate (e.g., every N frames)
- **Recommendation**: Make water sim update rate configurable; start with every-other-frame

### 10.7 Shared GL Context
- `main.cpp:86`: `settings.shareContextWith = mainWindow` — both windows share GL context
- This means FBOs and textures created in one window are accessible from the other ✓
- Water sim can run in the main update loop and its output can be drawn on the projector window

### 10.8 Existing "Sea Level" Concept
- The boid game already distinguishes land/water using `elevationAtKinectCoord()` > 0 (land) vs < 0 (water)
- Fish live "in water" (below base plane), rabbits on "land" (above base plane)
- The water sim would create actual visible water, which could conflict with or enhance this game mechanic
- **Recommendation**: Expose water depth to game controllers so boids can react to simulated water, not just elevation-based water

---

## 11. Key Code Locations Reference

| What | File | Line(s) |
|------|------|---------|
| GL window creation (set GL version here) | `src/main.cpp` | 66-96 |
| Kinect depth acquisition | `src/KinectProjector/KinectGrabber.cpp` | 157-185 |
| Depth filtering (temporal) | `src/KinectProjector/KinectGrabber.cpp` | 198-333 |
| FilteredDepthImage texture update | `src/KinectProjector/KinectProjector.cpp` | 283-289 |
| Depth texture bind/unbind | `src/KinectProjector/KinectProjector.h` | 105-110 |
| Elevation shader pass (contour FBO) | `src/SandSurfaceRenderer/SandSurfaceRenderer.cpp` | 273-288 |
| Height map shader pass (final render) | `src/SandSurfaceRenderer/SandSurfaceRenderer.cpp` | 253-271 |
| **Water sim insertion point** | `src/SandSurfaceRenderer/SandSurfaceRenderer.cpp` | **222** (after `drawSandbox()`) |
| Shader loading | `src/SandSurfaceRenderer/SandSurfaceRenderer.cpp` | 110-131 |
| FBO allocation (terrain) | `src/SandSurfaceRenderer/SandSurfaceRenderer.cpp` | 47, 134 |
| Projector window compositing | `src/ofApp.cpp` | 98-107 |
| Game draw calls (compositing order) | `src/ofApp.cpp` | 102-104 |
| Base plane equation | `src/KinectProjector/KinectProjector.h` | 131-133 |
| Coordinate transform matrices | `src/KinectProjector/KinectProjector.h` | 111-116 |
| Color map texture | `src/SandSurfaceRenderer/ColorMap.h` | 61 |
| GL3 elevation shader | `bin/data/shaders/shadersGL3/elevationShader.{vert,frag}` | — |
| GL3 height map shader | `bin/data/shaders/shadersGL3/heightMapShader.{vert,frag}` | — |

---

## 12. Recommended Next Steps

1. **Request GL 4.1** in `main.cpp` and verify all existing shaders still work with the higher GL context
2. **Create water simulation FBOs** (float-precision, ping-pong pair) in SandSurfaceRenderer
3. **Port SARndbox `Water2UpdateShader`** to work with DuneBox's texture conventions (sampler2DRect, coordinate spaces)
4. **Add water sim update loop** in `SandSurfaceRenderer::update()` after line 222
5. **Create water render shader** that reads water depth and produces a visually appealing water surface (transparency, caustics, color by depth)
6. **Composite water** onto `fboProjWindow` before game overlays
7. **Expose water state** to game controllers for interactive water-aware gameplay
