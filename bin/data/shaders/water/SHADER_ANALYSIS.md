# SARndbox Shader Pipeline Analysis for DuneBox Port

> Comprehensive reverse-engineering of the KeckCAVES/SARndbox-2.6 water simulation
> pipeline. Source: Oliver Kreylos, 2012–2018. GPL v2.

---

## 1. Overview

The SARndbox simulates water flow over a real-time deformable terrain using the
**Saint-Venant (shallow-water) equations** — a 2D hyperbolic conservation law
derived from depth-averaging the Navier-Stokes equations.

### Mathematical model

The conserved quantities per cell are stored as a 3-component vector **q**:

| Channel | Symbol | Meaning |
|---------|--------|---------|
| R       | w      | Water surface height (bathymetry + water depth) |
| G       | hu     | x-direction partial discharge (depth × x-velocity) |
| B       | hv     | y-direction partial discharge (depth × y-velocity) |

The temporal derivative **∂q/∂t** is computed from spatial fluxes using the
**Kurganov-Petrova central-upwind scheme** (2007):

- **Minmod flux limiter** with tunable θ parameter (default 1.3) prevents
  spurious oscillations.
- **Desingularized velocity** division avoids division-by-zero where water
  depth → 0 using `u = q_hu * √2·h / √(h⁴ + max(h⁴, ε))`.
- **Source terms** account for gravitational forcing due to bathymetry slope.

### Numerical integration

Each simulation step uses a **two-stage Heun/modified-Euler Runge-Kutta** method:

1. Compute derivative from current state → Euler half-step → intermediate state q*.
2. Compute derivative from q* → combine with original state → final RK step.

The time step is **adaptive**: the maximum stable Δt is computed per-cell from
the CFL condition and reduced across the entire grid via a GPU reduction pass.

### Attenuation

After each integration step, the partial discharges (hu, hv) are multiplied by
an attenuation factor `pow(attenuation, dt)` where `attenuation` defaults to
`127/128 ≈ 0.9921875`. This damps momentum, simulating friction.

---

## 2. Per-Shader Analysis

### 2.1 Water Simulation Shaders

---

#### Water2BathymetryUpdateShader.fs

**Purpose:** Adjusts the water surface height (q.x) after the bathymetry
(terrain) has changed. Ensures water that was at a certain height above old
terrain moves to the equivalent height above new terrain.

**Extensions:** `GL_ARB_texture_rectangle`

**Input uniforms:**

| Name | Type | Description |
|------|------|-------------|
| `oldBathymetrySampler` | `sampler2DRect` | Previous vertex-centered bathymetry grid (R32F) |
| `newBathymetrySampler` | `sampler2DRect` | Updated vertex-centered bathymetry grid (R32F) |
| `quantitySampler` | `sampler2DRect` | Current conserved quantities (RGB32F: w, hu, hv) |

**Input textures detail:**
- `oldBathymetrySampler`: 1-channel (R) float. Samples 4 surrounding vertex
  corners at `(x-1,y-1)`, `(x,y-1)`, `(x-1,y)`, `(x,y)` and averages to get
  cell-centered bathymetry. Bathymetry grid is `(size-1) × (size-1)` because it
  is vertex-centered while quantities are cell-centered.
- `newBathymetrySampler`: Same layout, the newly rendered bathymetry.
- `quantitySampler`: 3-channel (RGB) float. `q.x` = water surface height,
  `q.y` = hu, `q.z` = hv.

**Output:** `gl_FragColor = vec4(max(q.x - bOld, 0.0) + bNew, q.yz, 0.0)`
- R: new water surface height = old water depth clamped to ≥ 0 + new bathymetry
- G: hu (unchanged)
- B: hv (unchanged)
- A: 0.0

---

#### Water2BoundaryShader.fs

**Purpose:** Enforces dry boundary conditions at the edges of the simulation
domain. Sets the outermost ring of cells to "dry" state (water surface =
bathymetry, zero discharge). Applied by rendering a `GL_LINE_LOOP` over the
edge pixels.

**Extensions:** `GL_ARB_texture_rectangle`

**Input uniforms:**

| Name | Type | Description |
|------|------|-------------|
| `bathymetrySampler` | `sampler2DRect` | Current bathymetry (R32F) |

**Input textures detail:**
- Samples 4 corner vertices and averages to get cell-centered bathymetry `b`.

**Output:** `gl_FragColor = vec4(b, 0.0, 0.0, 0.0)`
- R: bathymetry elevation (water surface = ground level = dry)
- G: 0.0 (zero x-discharge)
- B: 0.0 (zero y-discharge)
- A: 0.0

---

#### Water2EulerStepShader.fs

**Purpose:** Performs a forward Euler integration step: `q_new = q + dq/dt * dt`.
Used as the first half of the two-stage Runge-Kutta integrator. Applies
momentum attenuation to the discharge components.

**Extensions:** `GL_ARB_texture_rectangle`

**Input uniforms:**

| Name | Type | Description |
|------|------|-------------|
| `stepSize` | `float` | Time step Δt for this integration step |
| `attenuation` | `float` | `pow(base_attenuation, dt)` — momentum damping factor |
| `quantitySampler` | `sampler2DRect` | Current conserved quantities (RGB32F) |
| `derivativeSampler` | `sampler2DRect` | Temporal derivative dq/dt (RGB32F) |

**Output:** `gl_FragColor = vec4(newQ, 0.0)`
- `newQ = q + qt * stepSize`
- `newQ.yz *= attenuation` (damp momentum)

---

#### Water2RungeKuttaStepShader.fs

**Purpose:** Performs the second (corrector) stage of the two-stage Heun
Runge-Kutta method: `q_final = 0.5 * (q_original + q_star + dq*/dt * dt)`.
This averages the original state with the Euler-predicted state plus its
derivative to achieve second-order accuracy.

**Extensions:** `GL_ARB_texture_rectangle`

**Input uniforms:**

| Name | Type | Description |
|------|------|-------------|
| `stepSize` | `float` | Time step Δt |
| `attenuation` | `float` | Momentum damping factor |
| `quantitySampler` | `sampler2DRect` | Original conserved quantities q₀ (RGB32F) |
| `quantityStarSampler` | `sampler2DRect` | Intermediate (Euler) quantities q* (RGB32F) |
| `derivativeSampler` | `sampler2DRect` | Derivative of intermediate state dq*/dt (RGB32F) |

**Output:** `gl_FragColor = vec4(newQ, 0.0)`
- `newQ = (q + qStar + qt * stepSize) * 0.5`
- `newQ.yz *= attenuation`

---

#### Water2SlopeShader.fs

**Purpose:** Computes spatial partial derivatives (slopes) of the conserved
quantities using a minmod flux limiter. Outputs slopes in both x and y
directions using MRT (multiple render targets). Also enforces the constraint
that the reconstructed water surface at face centers cannot be below the
bathymetry (positivity preservation).

**Extensions:** `GL_ARB_texture_rectangle`, `GL_ARB_draw_buffers`

**Input uniforms:**

| Name | Type | Description |
|------|------|-------------|
| `cellSize` | `vec2` | Physical size of each grid cell (dx, dy) |
| `theta` | `float` | Minmod limiter parameter (1.0–2.0, default 1.3) |
| `bathymetrySampler` | `sampler2DRect` | Vertex-centered bathymetry (R32F) |
| `quantitySampler` | `sampler2DRect` | Cell-centered quantities (RGB32F) |

**Output:**
- `gl_FragData[0]` = vec4(slopeX, 0.0) — x-direction slope (∂q/∂x) for all 3 components
- `gl_FragData[1]` = vec4(slopeY, 0.0) — y-direction slope (∂q/∂y) for all 3 components

**Note:** This shader is defined but **not used** in the current pipeline.
`Water2SlopeAndFluxAndDerivativeShader.fs` combines slope, flux, and derivative
computation into a single pass instead.

---

#### Water2FluxAndDerivativeShader.fs

**Purpose:** Computes temporal derivatives directly from pre-computed slopes
(from `Water2SlopeShader`). Uses the Kurganov-Petrova central-upwind scheme to
compute inter-cell fluxes and source terms from gravity.

**Extensions:** `GL_ARB_texture_rectangle`, `GL_ARB_draw_buffers`

**Input uniforms:**

| Name | Type | Description |
|------|------|-------------|
| `cellSize` | `vec2` | Physical cell size (dx, dy) |
| `g` | `float` | Gravitational acceleration (default 9.81) |
| `epsilon` | `float` | Desingularization parameter for velocity computation |
| `bathymetrySampler` | `sampler2DRect` | Vertex-centered bathymetry (R32F) |
| `quantitySampler` | `sampler2DRect` | Cell-centered quantities (RGB32F) |
| `slopeXSampler` | `sampler2DRect` | X-direction slopes from SlopeShader (RGB32F) |
| `slopeYSampler` | `sampler2DRect` | Y-direction slopes from SlopeShader (RGB32F) |

**Output:**
- `gl_FragData[0]` = vec4(derivative, 0.0) — temporal derivative dq/dt (3 components)
- `gl_FragData[1]` = vec4(maxStepSize) — CFL-limited maximum stable time step

**Note:** Like `Water2SlopeShader`, this is defined but **not used** in the
current pipeline. It was part of a 2-pass (slope → flux) approach that was
replaced by the combined single-pass shader.

---

#### Water2SlopeAndFluxAndDerivativeShader.fs ⭐ (PRIMARY)

**Purpose:** **This is the main derivative computation shader** used in the
actual pipeline. It combines slope estimation (minmod limiter), inter-cell flux
computation (Kurganov-Petrova central-upwind), and temporal derivative
calculation into a single rendering pass, avoiding the need for intermediate
slope textures.

**Extensions:** `GL_ARB_texture_rectangle`, `GL_ARB_draw_buffers`

**Input uniforms:**

| Name | Type | Description |
|------|------|-------------|
| `cellSize` | `vec2` | Physical cell dimensions (dx, dy) in world units |
| `theta` | `float` | Minmod limiter parameter (default 1.3) |
| `g` | `float` | Gravitational acceleration (default 9.81) |
| `epsilon` | `float` | Desingularization coefficient (default 0.01 × max(cellSize)) |
| `bathymetrySampler` | `sampler2DRect` | Vertex-centered bathymetry grid (R32F) |
| `quantitySampler` | `sampler2DRect` | Cell-centered conserved quantities (RGB32F) |

**Input textures detail:**
- `bathymetrySampler`: reads a 5×5 stencil of corner elevations to compute
  face-centered bathymetry for all 4 faces of each cell plus neighbors needed
  for slope limiting.
- `quantitySampler`: reads a 5×5 stencil of cells (current cell + 2 neighbors
  in each direction) for second-order reconstruction.

**Key algorithms in this shader:**
1. `calcSlope()`: Computes minmod-limited slopes with positivity check against
   face-centered bathymetry. Uses left/central/right differences scaled by θ.
2. `calcUv()`: Desingularized velocity computation: `u = q_hu * √2·h / √(h⁴ + max(h⁴, ε))`
3. `calcPartialFluxX()` / `calcPartialFluxY()`: Central-upwind numerical flux
   with local speed estimates. Returns CFL-limited maximum step size.

**Output:**
- `gl_FragData[0]` = vec4(dq/dt, 0.0) — temporal derivative
  - R: ∂w/∂t = -∂Fx/∂x - ∂Fy/∂y (conservation of mass)
  - G: ∂(hu)/∂t = source_x - ∂Fx_momentum/∂x - ∂Fy_momentum_x/∂y
  - B: ∂(hv)/∂t = source_y - ∂Fx_momentum_y/∂x - ∂Fy_momentum/∂y
- `gl_FragData[1]` = vec4(maxStepSize, 0.0, 0.0, 0.0) — CFL stability limit

---

#### Water2MaxStepSizeShader.fs

**Purpose:** Performs a parallel reduction (downsample by 2×2) of the maximum
step size texture to find the global minimum across the entire grid. Applied
iteratively until a 1×1 texture remains, which is then read back to the CPU.

**Extensions:** `GL_ARB_texture_rectangle`

**Input uniforms:**

| Name | Type | Description |
|------|------|-------------|
| `fullTextureSize` | `vec2` | Width-1 and height-1 of the current reduction level |
| `maxStepSizeSampler` | `sampler2DRect` | Max step size texture from previous reduction (R32F) |

**Output:** `gl_FragData[0] = vec4(minOf2x2tile, 0.0, 0.0, 0.0)`
- Takes the minimum of a 2×2 block of pixels (guards against edge cases where
  texture is odd-sized by checking against `fullTextureSize`).

---

#### Water2WaterAddShader.vs

**Purpose:** Vertex shader for water-adding geometry (rain disks, local water
tools). Transforms vertices from camera space to water-grid clip space and
computes the scaled water amount to add.

**Extensions:** None

**Input uniforms:**

| Name | Type | Description |
|------|------|-------------|
| `pmv` | `mat4` | Combined projection-modelview from camera space to water grid clip space |
| `stepSize` | `float` | Current simulation time step Δt |

**Input attributes:**

| Name | Type | Description |
|------|------|-------------|
| `waterAmount` | `float` (attribute) | Per-vertex water rate (positive = rain, negative = drain). Bound to vertex attribute index 1 via `glVertexAttrib1fARB(1, ...)`. |

**Varyings (output to fragment shader):**
- `scaledWaterAmount` = `waterAmount * stepSize`

**Output:** `gl_Position = pmv * gl_Vertex`

---

#### Water2WaterAddShader.fs

**Purpose:** Fragment shader for water-adding geometry. Writes the scaled water
amount into the additive water texture. All fragments from a rain disk get the
same water amount.

**Extensions:** `GL_ARB_texture_rectangle`

**Input uniforms:**

| Name | Type | Description |
|------|------|-------------|
| `waterSampler` | `sampler2DRect` | Water texture (not actually sampled — appears vestigial) |

**Varyings (from vertex shader):**
- `scaledWaterAmount` — amount of water to add (can be negative for draining)

**Output:** `gl_FragColor = vec4(scaledWaterAmount)` — all 4 channels set to the
same value. Only the R channel is used downstream. Rendered with **additive
blending** (`GL_ONE, GL_ONE`) so overlapping rain disks accumulate.

---

#### Water2WaterUpdateShader.fs

**Purpose:** Applies the accumulated water additions/removals from the water
texture to the conserved quantity grid. Adjusts water depth and scales momentum
appropriately — new water is added with zero velocity; draining removes water
at current velocity (momentum proportionally reduced).

**Extensions:** `GL_ARB_texture_rectangle`

**Input uniforms:**

| Name | Type | Description |
|------|------|-------------|
| `bathymetrySampler` | `sampler2DRect` | Vertex-centered bathymetry (R32F) |
| `quantitySampler` | `sampler2DRect` | Current conserved quantities (RGB32F) |
| `waterSampler` | `sampler2DRect` | Additive water texture (R32F, from WaterAdd pass) |

**Output:** `gl_FragColor = vec4(q, 0.0)`
- R: updated water surface height = max(old_depth + water_addition, 0) + bathymetry
- G: updated hu — if new depth is 0, hu=0; if depth decreased, scale by
  (new/old); if increased, keep unchanged
- B: updated hv — same logic as G
- A: 0.0

---

#### Water2WaterAdaptShader.fs

**Purpose:** Adapts a new water level grid (uploaded from CPU) to the current
bathymetry. Ensures the water surface height is never below the terrain floor.
Used when externally setting water levels (e.g., `setWaterLevel()` API).

**Extensions:** `GL_ARB_texture_rectangle`

**Input uniforms:**

| Name | Type | Description |
|------|------|-------------|
| `bathymetrySampler` | `sampler2DRect` | Vertex-centered bathymetry (R32F) |
| `newQuantitySampler` | `sampler2DRect` | New quantity grid being adapted (RGB32F) |

**Output:** `gl_FragColor = vec4(max(qNew.x, b), qNew.yz, 0.0)`
- R: water surface = max(new water surface, cell-centered bathymetry)
- G: hu (unchanged)
- B: hv (unchanged)

---

### 2.2 Water Rendering Shaders

---

#### WaterRenderingShader.vs

**Purpose:** Vertex shader that renders the water surface as a 3D mesh. Reads
the water surface height from the quantity texture, computes normals from
central differences, performs per-vertex Gouraud lighting, and uses the water
depth (surface - bathymetry) as the alpha channel for transparency.

**Extensions:** `GL_ARB_texture_rectangle`

**Input uniforms:**

| Name | Type | Description |
|------|------|-------------|
| `quantitySampler` | `sampler2DRect` | Conserved quantities (RGB32F) — only R channel (w) used |
| `bathymetrySampler` | `sampler2DRect` | Vertex-centered bathymetry (R32F) |
| `modelviewGridMatrix` | `mat4` | Grid space → eye space transform |
| `tangentModelviewGridMatrix` | `mat4` | Grid space → eye space for tangent/normal planes |
| `projectionModelviewGridMatrix` | `mat4` | Grid space → clip space transform |

**Built-in OpenGL state used:**
- `gl_LightSource[0]` — position, ambient, diffuse, specular, spot parameters, attenuation
- `gl_FrontMaterial` — ambient, diffuse, specular, shininess
- `gl_LightModel.ambient`

**Varyings (output to fragment shader):**
- `color` (vec4) — Gouraud-shaded color (RGB) with alpha = water depth

**Output:** `gl_Position = projectionModelviewGridMatrix * vertexGc`

---

#### WaterRenderingShader.fs

**Purpose:** Simple fragment shader that outputs the interpolated Gouraud color
from the vertex shader. Discards near-transparent fragments (water depth ≈ 0)
to avoid z-fighting with the terrain surface.

**Extensions:** None

**Varyings (from vertex shader):**
- `color` (vec4)

**Output:** `gl_FragColor = color` (or discard if `color.a < 0.0025`)

---

### 2.3 Surface Rendering Shaders

---

#### SurfaceDepthShader.vs

**Purpose:** Renders the terrain surface's depth only (for shadow mapping or
depth pre-pass). Reads z from the depth image texture and transforms the vertex
directly from depth image space to clip space.

**Extensions:** `GL_ARB_texture_rectangle`

**Input uniforms:**

| Name | Type | Description |
|------|------|-------------|
| `depthSampler` | `sampler2DRect` | Depth image elevation texture (R32F) |
| `projectionModelviewDepthProjection` | `mat4` | Depth image space → clip space |

**Output:** `gl_Position` only.

#### SurfaceDepthShader.fs

**Purpose:** No-op fragment shader. Depth is written automatically by the
hardware pipeline.

---

#### SurfaceElevationShader.vs

**Purpose:** Computes the elevation of each vertex relative to a base plane in
depth image space. Used to render the surface elevation into an FBO (e.g., for
contour line computation).

**Extensions:** `GL_ARB_texture_rectangle`

**Input uniforms:**

| Name | Type | Description |
|------|------|-------------|
| `depthSampler` | `sampler2DRect` | Depth image elevation texture (R32F) |
| `basePlaneDic` | `vec4` | Base plane equation in depth image coords |
| `weightDic` | `vec4` | Weight equation for normalization |
| `projectionModelviewDepthProjection` | `mat4` | Depth image space → clip space |

**Varyings:** `elevation` (float) — signed distance from base plane.

#### SurfaceElevationShader.fs

**Purpose:** Writes elevation directly to framebuffer.

**Output:** `gl_FragColor = vec4(elevation, 0.0, 0.0, 1.0)`

---

#### SurfaceGlobalAmbientHeightMapShader.vs

**Purpose:** Renders terrain with ambient lighting, height color map, and water
level overlay. Computes the height color map texture coordinate and water level
texture coordinate for each vertex.

**Extensions:** `GL_ARB_texture_rectangle`

**Input uniforms:**

| Name | Type | Description |
|------|------|-------------|
| `depthSampler` | `sampler2DRect` | Depth image elevation texture |
| `depthProjection` | `mat4` | Depth image space → camera space |
| `basePlane` | `vec4` | Base plane equation in camera space |
| `heightColorMapTransformation` | `vec2` | (scale, offset) for elevation → color map texcoord |
| `waterLevelTextureTransformation` | `mat4` | Camera space → water level texture space |

**Varyings:**
- `heightColorMapTexCoord` — 1D texture coordinate into the height color map
- `waterLevelTexCoord` — 2D texture coordinate into the water level texture
- `diffColor` — ambient light color

#### SurfaceGlobalAmbientHeightMapShader.fs

**Purpose:** Applies contour lines, height color mapping, ambient modulation,
and water color overlay.

**Input uniforms:**

| Name | Type | Description |
|------|------|-------------|
| `pixelCornerElevationSampler` | `sampler2DRect` | Half-pixel offset elevation for contour lines |
| `contourLineFactor` | `float` | 1.0 / contour line spacing |
| `heightColorMapSampler` | `sampler1D` | Elevation → color lookup table |
| `waterLevelSampler` | `sampler2DRect` | Water level texture |
| `waterOpacity` | `float` | Controls water transparency scaling |

**Output:** `gl_FragColor` — final colored and water-tinted surface pixel.

---

#### SurfaceShadowedIlluminatedHeightMapShader.vs

**Purpose:** Full-featured terrain vertex shader with: depth texture lookup,
camera space transform, elevation computation for color mapping, water level
texture coordinate, surface normal computation from depth image derivatives,
per-vertex Phong lighting, and shadow map texture coordinates.

**Extensions:** `GL_ARB_texture_rectangle`

**Input uniforms:**

| Name | Type | Description |
|------|------|-------------|
| `depthSampler` | `sampler2DRect` | Depth image elevation texture |
| `depthProjection` | `mat4` | Depth image space → camera space |
| `tangentDepthProjection` | `mat4` | For transforming tangent planes |
| `basePlane` | `vec4` | Base plane equation |
| `heightColorMapTransformation` | `vec2` | Elevation → color map transform |
| `waterLevelTextureTransformation` | `mat4` | Camera → water texture space |
| `shadowProjection` | `mat4` | Camera → shadow texture space |

**Built-in state:** `gl_LightSource[0]`, `gl_FrontMaterial`, `gl_ModelViewMatrix`,
`gl_NormalMatrix`, `gl_ModelViewProjectionMatrix`

**Varyings:**
- `heightColorMapTexCoord`, `waterLevelTexCoord`, `diffColor`, `specColor`, `vertexSc`

#### SurfaceShadowedIlluminatedHeightMapShader.fs

**Purpose:** Fragment shader with contour lines, height color map, Phong
illumination modulation, shadow mapping via `shadow2DProj()`, and water overlay.

**Input uniforms:**

| Name | Type | Description |
|------|------|-------------|
| `pixelCornerElevationSampler` | `sampler2DRect` | For contour line detection |
| `contourLineFactor` | `float` | Contour spacing inverse |
| `heightColorMapSampler` | `sampler1D` | Height → color |
| `waterLevelSampler` | `sampler2DRect` | Water level |
| `waterOpacity` | `float` | Water transparency factor |
| `shadowTextureSampler` | `sampler2DShadow` | Shadow map |

---

#### SurfaceAddContourLines.fs (included as shader fragment)

**Purpose:** Shader function (not standalone) that adds black topographic
contour lines to a base color. Uses pixel-corner elevation values to detect
contour line crossings with a 4-connectivity thinning algorithm.

**Input uniforms:**

| Name | Type | Description |
|------|------|-------------|
| `pixelCornerElevationSampler` | `sampler2DRect` | Half-pixel offset elevation |
| `contourLineFactor` | `float` | 1.0 / contour line interval |

**Function:** `void addContourLines(in vec2 fragCoord, inout vec4 baseColor)`

---

#### SurfaceAddWaterColor.fs (included as shader fragment)

**Purpose:** Shader function that tints the surface blue where underwater. Uses
simplex noise + turbulence for animated water surface effects (several modes
available: specular highlights, noise, advected noise). Computes water surface
normal for specular glints.

**Input uniforms:**

| Name | Type | Description |
|------|------|-------------|
| `bathymetrySampler` | `sampler2DRect` | Bathymetry for water depth calculation |
| `quantitySampler` | `sampler2DRect` | Water quantities for surface height |
| `waterCellSize` | `vec2` | Cell size for normal computation |
| `waterOpacity` | `float` | Water transparency scaling |
| `waterAnimationTime` | `float` | Time for animated noise effects |

**Varyings:** `waterTexCoord` (vec2) — water grid texture coordinate

**Functions:**
- `void addWaterColor(in vec2 fragCoord, inout vec4 baseColor)` — main water
  shading with specular highlights
- `void addWaterColorAdvected(inout vec4 baseColor)` — alternative with
  texture advection (currently `#if 0` disabled)

Contains full implementations of:
- 3D simplex noise (`snoise()`)
- Turbulence (`turb()` — 6-octave fbm of |noise|)

---

#### SurfaceIlluminate.fs (included as shader fragment)

**Purpose:** Modulates a surface base color with pre-computed diffuse and
specular illumination.

**Function:** `void illuminate(inout vec4 baseColor)` →
`baseColor = baseColor * diffColor + specColor`

---

## 3. Pipeline Diagram

### Per-frame simulation loop (from `Sandbox::display()` → `WaterTable2::runSimulationStep()`)

The simulation runs N times per frame (up to `waterMaxSteps`, default 30),
consuming a total time budget of `frameTime * waterSpeed`:

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    FRAME START (Sandbox::display)                       │
│                                                                         │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │  0. UPDATE BATHYMETRY (WaterTable2::updateBathymetry)            │  │
│  │     Only when depth image version changes                         │  │
│  │                                                                   │  │
│  │  a) Render depth surface → new bathymetry texture                 │  │
│  │     FBO: bathymetryFramebufferObject                              │  │
│  │     Viewport: (size[0]-1) × (size[1]-1)                           │  │
│  │     Uses: depthImageRenderer->renderElevation()                   │  │
│  │     Output: bathymetryTextureObjects[1-current] (R32F)            │  │
│  │                                                                   │  │
│  │  b) Run BathymetryUpdateShader                                    │  │
│  │     FBO: integrationFramebufferObject                             │  │
│  │     Input: old bathy + new bathy + current quantities             │  │
│  │     Output: quantityTextureObjects[1-current] (RGB32F)            │  │
│  │     Then flip: currentBathymetry, currentQuantity                 │  │
│  └───────────────────────────────────────────────────────────────────┘  │
│                                                                         │
│  ┌───────────────────────────────── LOOP ────────────────────────────┐  │
│  │  while (totalTimeStep > 1e-8 && numSteps < waterMaxSteps-1)      │  │
│  │                                                                   │  │
│  │  ╔═══════════════════════════════════════════════════════════════╗ │  │
│  │  ║  STEP 1: COMPUTE DERIVATIVE (calcDerivative)                 ║ │  │
│  │  ║                                                               ║ │  │
│  │  ║  Shader: Water2SlopeAndFluxAndDerivativeShader.fs             ║ │  │
│  │  ║  FBO: derivativeFramebufferObject (MRT: 2 outputs)            ║ │  │
│  │  ║  Viewport: size[0] × size[1]                                  ║ │  │
│  │  ║                                                               ║ │  │
│  │  ║  Texture unit 0: bathymetryTextures[current]     (R32F)       ║ │  │
│  │  ║  Texture unit 1: quantityTextures[current]       (RGB32F)     ║ │  │
│  │  ║                                                               ║ │  │
│  │  ║  Output attachment 0: derivativeTexture          (RGB32F)     ║ │  │
│  │  ║  Output attachment 1: maxStepSizeTextures[0]     (R32F)       ║ │  │
│  │  ╚═══════════════════════════════════════════════════════════════╝ │  │
│  │                           │                                       │  │
│  │                           ▼                                       │  │
│  │  ╔═══════════════════════════════════════════════════════════════╗ │  │
│  │  ║  STEP 1b: REDUCE MAX STEP SIZE                               ║ │  │
│  │  ║                                                               ║ │  │
│  │  ║  Shader: Water2MaxStepSizeShader.fs                           ║ │  │
│  │  ║  FBO: maxStepSizeFramebufferObject                            ║ │  │
│  │  ║  Iterative 2×2 reduction until 1×1                            ║ │  │
│  │  ║  Ping-pongs between maxStepSizeTextures[0] and [1]            ║ │  │
│  │  ║                                                               ║ │  │
│  │  ║  Final: glReadPixels(0,0,1,1) → stepSize (CPU float)         ║ │  │
│  │  ║  stepSize = min(stepSize, maxStepSize)                        ║ │  │
│  │  ╚═══════════════════════════════════════════════════════════════╝ │  │
│  │                           │                                       │  │
│  │                           ▼                                       │  │
│  │  ╔═══════════════════════════════════════════════════════════════╗ │  │
│  │  ║  STEP 2: EULER INTEGRATION STEP                              ║ │  │
│  │  ║                                                               ║ │  │
│  │  ║  Shader: Water2EulerStepShader.fs                             ║ │  │
│  │  ║  FBO: integrationFramebufferObject                            ║ │  │
│  │  ║  Output: quantityTextures[2]  (the 3rd/scratch texture)       ║ │  │
│  │  ║                                                               ║ │  │
│  │  ║  Texture unit 0: quantityTextures[current]       (RGB32F)     ║ │  │
│  │  ║  Texture unit 1: derivativeTexture               (RGB32F)     ║ │  │
│  │  ║                                                               ║ │  │
│  │  ║  q* = q + dq/dt * stepSize;  q*.yz *= attenuation             ║ │  │
│  │  ╚═══════════════════════════════════════════════════════════════╝ │  │
│  │                           │                                       │  │
│  │                           ▼                                       │  │
│  │  ╔═══════════════════════════════════════════════════════════════╗ │  │
│  │  ║  STEP 3: COMPUTE DERIVATIVE OF INTERMEDIATE STATE            ║ │  │
│  │  ║                                                               ║ │  │
│  │  ║  Same shader: Water2SlopeAndFluxAndDerivativeShader.fs        ║ │  │
│  │  ║  FBO: derivativeFramebufferObject                             ║ │  │
│  │  ║                                                               ║ │  │
│  │  ║  Texture unit 0: bathymetryTextures[current]     (R32F)       ║ │  │
│  │  ║  Texture unit 1: quantityTextures[2]  (q*)       (RGB32F)     ║ │  │
│  │  ║                                                               ║ │  │
│  │  ║  Output: derivativeTexture (dq*/dt)              (RGB32F)     ║ │  │
│  │  ║  (maxStepSize NOT read this time — calcMaxStepSize=false)     ║ │  │
│  │  ╚═══════════════════════════════════════════════════════════════╝ │  │
│  │                           │                                       │  │
│  │                           ▼                                       │  │
│  │  ╔═══════════════════════════════════════════════════════════════╗ │  │
│  │  ║  STEP 4: RUNGE-KUTTA CORRECTOR STEP                         ║ │  │
│  │  ║                                                               ║ │  │
│  │  ║  Shader: Water2RungeKuttaStepShader.fs                        ║ │  │
│  │  ║  FBO: integrationFramebufferObject                            ║ │  │
│  │  ║  Output: quantityTextures[1-current]             (RGB32F)     ║ │  │
│  │  ║                                                               ║ │  │
│  │  ║  Texture unit 0: quantityTextures[current] (q₀)  (RGB32F)    ║ │  │
│  │  ║  Texture unit 1: quantityTextures[2]       (q*)  (RGB32F)    ║ │  │
│  │  ║  Texture unit 2: derivativeTexture         (dq*/dt) (RGB32F) ║ │  │
│  │  ║                                                               ║ │  │
│  │  ║  q_new = (q₀ + q* + dq*/dt * dt) * 0.5;  .yz *= atten       ║ │  │
│  │  ╚═══════════════════════════════════════════════════════════════╝ │  │
│  │                           │                                       │  │
│  │                           ▼                                       │  │
│  │  ╔═══════════════════════════════════════════════════════════════╗ │  │
│  │  ║  STEP 4b: BOUNDARY CONDITIONS (if dryBoundary enabled)      ║ │  │
│  │  ║                                                               ║ │  │
│  │  ║  Shader: Water2BoundaryShader.fs                              ║ │  │
│  │  ║  Geometry: GL_LINE_LOOP around edge pixels                    ║ │  │
│  │  ║  Output: overwrites edge cells of quantityTextures[1-current] ║ │  │
│  │  ╚═══════════════════════════════════════════════════════════════╝ │  │
│  │                           │                                       │  │
│  │           flip: currentQuantity = 1 - currentQuantity             │  │
│  │           totalTimeStep -= stepSize;  numSteps++                   │  │
│  └───────────────────────────── END LOOP ────────────────────────────┘  │
│                                                                         │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │  STEP 5: WATER ADDITION (if waterDeposit≠0 or renderFunctions)   │  │
│  │                                                                   │  │
│  │  a) Clear water FBO with waterDeposit*stepSize (evaporation)      │  │
│  │     FBO: waterFramebufferObject → waterTextureObject (R32F)       │  │
│  │     Enable additive blending (GL_ONE, GL_ONE)                     │  │
│  │                                                                   │  │
│  │  b) Bind Water2WaterAddShader (.vs + .fs)                         │  │
│  │     Set pmv = waterAddPmvMatrix                                   │  │
│  │     Set stepSize                                                  │  │
│  │     Call all registered renderFunctions (rain disks, local tools)  │  │
│  │                                                                   │  │
│  │  c) Run Water2WaterUpdateShader.fs                                │  │
│  │     FBO: integrationFramebufferObject                             │  │
│  │     Input: bathymetry + quantities + water texture                │  │
│  │     Output: updated quantityTextures[1-current]                   │  │
│  │     Then flip: currentQuantity = 1 - currentQuantity              │  │
│  └───────────────────────────────────────────────────────────────────┘  │
│                                                                         │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │  RENDERING PASS                                                   │  │
│  │                                                                   │  │
│  │  Either: WaterRenderer (3D mesh with Gouraud lighting)            │  │
│  │     Uses WaterRenderingShader.vs + .fs                            │  │
│  │     Renders quad strip mesh, samples quantity + bathy textures     │  │
│  │                                                                   │  │
│  │  Or: SurfaceRenderer with water overlay                           │  │
│  │     Uses SurfaceAddWaterColor.fs functions                        │  │
│  │     Samples water level as tint on terrain color map              │  │
│  └───────────────────────────────────────────────────────────────────┘  │
│                                                                         │
│                           FRAME END                                     │
└─────────────────────────────────────────────────────────────────────────┘
```

### Simplified data-flow summary

```
                    Kinect depth
                         │
                         ▼
              DepthImageRenderer
              renderElevation()
                         │
                         ▼
               ┌─────────────────┐
               │  Bathymetry FBO │  R32F  (size-1)×(size-1)
               │  (vertex grid)  │
               └────────┬────────┘
                        │
     ┌──────────────────┼──────────────────────┐
     │                  │                      │
     ▼                  ▼                      ▼
BathymetryUpdate   SlopeFluxDerivative    WaterRendering
  Shader              Shader (×2)           Shader
     │                  │                      │
     ▼                  ▼                      ▼
┌─────────┐    ┌──────────────┐    ┌────────────────────┐
│Quantity  │    │  Derivative  │    │  Water surface     │
│Textures  │◄──│  + MaxStep   │    │  3D mesh output    │
│(RGB32F)  │    │  Textures    │    └────────────────────┘
│ 3× ping  │    └──────────────┘
│  pong    │
└─────────┘
     ▲
     │
     ├── EulerStep / RK Step
     ├── BoundaryShader
     └── WaterUpdateShader ◄── WaterAddShader (rain geometry)
```

---

## 4. FBO Setup

### Texture inventory (from `WaterTable2::initContext()`)

| Texture | Count | Format | Size | Content |
|---------|-------|--------|------|---------|
| `bathymetryTextureObjects` | 2 (double-buffered) | `GL_R32F` | `(size[0]-1) × (size[1]-1)` | Vertex-centered terrain elevation |
| `quantityTextureObjects` | 3 (triple-buffered) | `GL_RGB32F` | `size[0] × size[1]` | Cell-centered (w, hu, hv) |
| `derivativeTextureObject` | 1 | `GL_RGB32F` | `size[0] × size[1]` | Temporal derivative (∂w/∂t, ∂hu/∂t, ∂hv/∂t) |
| `maxStepSizeTextureObjects` | 2 (ping-pong) | `GL_R32F` | `size[0] × size[1]` | CFL max step size (reduction buffer) |
| `waterTextureObject` | 1 | `GL_R32F` | `size[0] × size[1]` | Additive water source/sink |

**Default grid size:** 640×480 (configurable via `waterTableSize`).

### Texture parameters (all textures)

```
GL_TEXTURE_MIN_FILTER = GL_NEAREST
GL_TEXTURE_MAG_FILTER = GL_NEAREST
GL_TEXTURE_WRAP_S = GL_CLAMP
GL_TEXTURE_WRAP_T = GL_CLAMP
```

### FBO inventory

| FBO | Color attachments | Purpose |
|-----|-------------------|---------|
| `bathymetryFramebufferObject` | `bathymetryTextures[0]` @ att0, `[1]` @ att1 | Render depth surface → bathymetry |
| `derivativeFramebufferObject` | `derivativeTexture` @ att0, `maxStepSizeTextures[0]` @ att1 | MRT: derivative + max step size |
| `maxStepSizeFramebufferObject` | `maxStepSizeTextures[0]` @ att0, `[1]` @ att1 | Iterative reduction (ping-pong) |
| `integrationFramebufferObject` | `quantityTextures[0]` @ att0, `[1]` @ att1, `[2]` @ att2 | Euler/RK output selection |
| `waterFramebufferObject` | `waterTexture` @ att0 | Accumulate rain/drain geometry |

### Ping-pong / triple-buffer strategy

**Bathymetry (2 textures):** Standard double-buffer ping-pong.
`currentBathymetry` alternates between 0 and 1.

**Quantities (3 textures):** Triple-buffer:
- `quantityTextures[currentQuantity]` = current state (read)
- `quantityTextures[1-currentQuantity]` = next state (write target)
- `quantityTextures[2]` = scratch texture for Euler intermediate state q*

The Euler step always writes to texture index 2. The RK step writes to
`1-currentQuantity`. After the RK step, `currentQuantity` is flipped.

**Max step size (2 textures):** Ping-pong during reduction. After derivative
computation writes to `maxStepSizeTextures[0]`, the reduction shader
alternates reading from [0]→writing [1], reading [1]→writing [0], etc.

### Vertex shader for simulation passes

All simulation fragment shaders (except `WaterAddShader`) use a **dynamically
generated** vertex shader that maps pixel coordinates to NDC:

```glsl
void main() {
    gl_Position = vec4(gl_Vertex.x * (2.0/WIDTH) - 1.0,
                       gl_Vertex.y * (2.0/HEIGHT) - 1.0,
                       0.0, 1.0);
}
```

A fullscreen quad `(0,0)→(size[0],size[1])` is drawn to cover all cells.

---

## 5. Depth Map Integration

### Kinect → Bathymetry pipeline

1. **Kinect camera** captures depth frames at 640×480 (or configured resolution).
2. **FrameFilter** averages depth over `numAveragingSlots` frames (default 30)
   with variance rejection to smooth the surface.
3. **DepthImageRenderer** stores the filtered depth image as a
   `GL_TEXTURE_RECTANGLE_ARB` texture (`depthSampler`). It provides a
   `renderElevation()` method that renders the depth surface as a triangle mesh.
4. **WaterTable2::updateBathymetry()** renders this surface into the bathymetry
   FBO using the `bathymetryPmv` projection matrix.

### Bathymetry update mechanism

In `WaterTable2::updateBathymetry()`:

1. Check if depth image version has changed (`bathymetryVersion !=
   depthImageRenderer->getDepthImageVersion()`).
2. If changed:
   a. Bind bathymetry FBO, target the *non-current* bathymetry texture.
   b. Clear with `domain.min[2]` (floor elevation).
   c. Set viewport to `(size[0]-1) × (size[1]-1)` (vertex grid size).
   d. Call `depthImageRenderer->renderElevation(bathymetryPmv, contextData)`.
      This renders the depth mesh into the texture, producing a vertex-centered
      elevation grid.
   e. Bind integration FBO, run `BathymetryUpdateShader` to adjust water surface
      heights for the new terrain.
   f. Flip `currentBathymetry` and `currentQuantity`.

### Coordinate transforms

**Camera space → Elevation map space:**
`baseTransform` = orthonormal transform (rotation + translation) that aligns
the sandbox base plane to the XY plane with Z pointing up.

**Elevation map space → Bathymetry grid NDC:**
`bathymetryPmv` = orthographic projection:
- X: `[domain.min[0] + cellSize/2, domain.max[0] - cellSize/2]` → [-1, 1]
  (half-cell inset for vertex centering)
- Y: `[domain.min[1] + cellSize/2, domain.max[1] - cellSize/2]` → [-1, 1]
- Z: `[-domain.max[2], -domain.min[2]]` → [-1, 1]
- Pre-multiplied by `baseTransform`

**Camera space → Water texture space:**
`waterTextureTransform` scales camera-space XY into the quantity grid pixel
coordinates:
- X: `[domain.min[0], domain.max[0]]` → `[0, size[0]]`
- Y: `[domain.min[1], domain.max[1]]` → `[0, size[1]]`
- Pre-multiplied by `baseTransform`

### Grid relationship

- Bathymetry grid: `(size[0]-1) × (size[1]-1)` — **vertex-centered**.
  Bathymetry values are at cell corners.
- Quantity grid: `size[0] × size[1]` — **cell-centered**. Conserved quantities
  are at cell centers.
- A cell-centered bathymetry value is obtained by averaging 4 surrounding
  vertex values (the `(x-1,y-1), (x,y-1), (x-1,y), (x,y)` pattern seen in
  every shader).

---

## 6. Rain/Water Addition Gesture

### Two water tools

**GlobalWaterTool** — uniform rain/evaporation across the entire grid:
- Two buttons: "Rain" and "Dry".
- On button press: `waterAmount = rainStrength / waterSpeed` (or negative for
  drain).
- Calls `waterTable->setWaterDeposit()` which adds to the `waterDeposit` field.
- This amount is applied as the clear color of the water FBO:
  `glClearColor(waterDeposit * stepSize, 0, 0, 0)`.
- Every cell gets the same base deposit before any local additions.

**LocalWaterTool** — localized rain cylinder at a tracked position:
- Two buttons: "Rain" and "Dry".
- Registers a render function (`addWater()`) with the water table.
- On each simulation step, renders a **32-sided polygon disk** at the tracked
  device position, projected into water grid space.
- Disk radius = `pointPickDistance * 3` (from Vrui).
- Water amount sent via `glVertexAttrib1fARB(1, adding / waterSpeed)` where
  vertex attribute 1 maps to the `waterAmount` attribute in `Water2WaterAddShader.vs`.

### Hand-based rain (Sandbox::addWater)

The main Sandbox class also has an `addWater()` function registered with the
water table. It uses the `HandExtractor` to detect hands held above the sandbox:
- Each detected hand is rendered as a 32-sided polygon disk with radius =
  `hand.radius * 0.75`.
- `glVertexAttrib1fARB(1, rainStrength / waterSpeed)` sets the per-vertex water
  rate.

### Water addition pipeline (per simulation step)

1. **Clear** `waterFramebufferObject` with `waterDeposit * stepSize` (global
   rain/evaporation applied to every cell).
2. **Enable additive blending** (`GL_ONE, GL_ONE`).
3. **Bind** `Water2WaterAddShader` with:
   - `pmv` = `waterAddPmvMatrix` (camera → water grid projection)
   - `stepSize` = current time step
4. **Call all render functions** — each renders geometry (polygonal disks) into
   the water FBO. The vertex shader scales `waterAmount * stepSize`; the
   fragment shader outputs `scaledWaterAmount` to all channels.
5. **Disable blending**.
6. **Run** `Water2WaterUpdateShader` to merge the accumulated water texture into
   the quantity grid:
   - Computes old water depth = `q.x - bathymetry`
   - Adds water texture value: `new_depth = max(old_depth + water, 0)`
   - Updates water surface: `q.x = new_depth + bathymetry`
   - Scales momentum: if depth decreased, proportionally reduce (hu, hv); if
     depth increased, keep existing momentum (new water enters at zero velocity).

### Water addition projection

`waterAddPmv` is an orthographic projection from camera space into water grid
clip space:
- X: `[domain.min[0], domain.max[0]]` → `[-1, 1]`
- Y: `[domain.min[1], domain.max[1]]` → `[-1, 1]`
- Z: `[-domain.max[2]*5, -domain.min[2]]` → `[-1, 1]` (extended near plane to
  capture rain geometry above the surface)
- Pre-multiplied by `baseTransform`

---

## 7. Vrui-Specific Conventions

### Matrix uniforms

| SARndbox name | Vrui origin | DuneBox equivalent |
|---------------|-------------|-------------------|
| `pmv` | Custom orthographic projection × `baseTransform` | `ofMatrix4x4` computed manually |
| `modelviewGridMatrix` | `gridTransform` left-multiplied by Vrui modelview | `ofCamera.getModelViewMatrix()` × grid transform |
| `tangentModelviewGridMatrix` | Transposed inverse of modelview × grid | Compute manually from modelview |
| `projectionModelviewGridMatrix` | projection × modelview × grid | `ofCamera.getModelViewProjectionMatrix()` × grid |
| `depthProjection` | Depth image space → camera space | Custom matrix from depth sensor calibration |
| `tangentDepthProjection` | For normal transformation | Transpose of inverse of `depthProjection` |
| `gl_ModelViewProjectionMatrix` | Vrui-managed | `ofGetCurrentMatrix(OF_MATRIX_MODELVIEW_PROJECTION)` |
| `gl_ModelViewMatrix` | Vrui-managed | `ofGetCurrentMatrix(OF_MATRIX_MODELVIEW)` |
| `gl_NormalMatrix` | Vrui-managed | `ofGetCurrentNormalMatrix()` |

### Texture binding conventions

Vrui/SARndbox uses the `GLARBMultitexture` extension pattern:
```cpp
glActiveTextureARB(GL_TEXTURE0_ARB);
glBindTexture(GL_TEXTURE_RECTANGLE_ARB, textureId);
glUniform1iARB(uniformLoc, 0); // texture unit index
```

In OpenFrameworks, use `ofFbo::getTexture().bind()` or `shader.setUniformTexture()`.

### Shader compilation

SARndbox uses `ShaderHelper.h` functions:
- `compileFragmentShader("ShaderName")` — loads from `share/SARndbox-2.6/Shaders/ShaderName.fs`
- `compileVertexShader("ShaderName")` — loads `.vs` file
- `linkVertexAndFragmentShader("ShaderName")` — loads and links both
- Simulation vertex shaders are generated from a template string (not from files)

### Vrui toolkit dependencies

| Dependency | Where used | Replacement |
|------------|-----------|-------------|
| `Vrui::Tool` / `Vrui::ToolManager` | GlobalWaterTool, LocalWaterTool | ofxGui or custom input handling |
| `Vrui::getFrameTime()` | Simulation time budget | `ofGetLastFrameTime()` |
| `Vrui::getApplicationTime()` | Frame deduplication | `ofGetElapsedTimef()` |
| `Vrui::getInverseNavigationTransformation()` | Camera → world | `ofCamera.getModelViewMatrix()` inverse |
| `Vrui::getPointPickDistance()` | Rain disk radius | Fixed or configurable value |
| `Vrui::DisplayState` | Modelview matrix | `ofCamera` |
| `GLObject` / `GLContextData` | Per-context GL state management | `ofFbo`, `ofShader`, `ofTexture` |
| `GL/GLTransformationWrappers.h` | Matrix upload helpers | `ofShader::setUniformMatrix4f()` |
| `Vrui::Lightsource` | Scene lighting | `ofLight` |
| `HandExtractor` | Hand detection for rain | Kinect skeleton or blob tracking |

### Vertex attributes

SARndbox uses legacy OpenGL vertex attributes:
- `gl_Vertex` — vertex position (from `glVertex*()` or VBO)
- `glVertexAttrib1fARB(1, value)` — sends `waterAmount` via generic attribute
  index 1, declared as `attribute float waterAmount` in the shader

---

## 8. Porting Notes for OpenFrameworks

### 8.1 sampler2DRect → sampler2D

SARndbox uses `GL_TEXTURE_RECTANGLE_ARB` exclusively — textures are addressed
by pixel coordinates `(0..width, 0..height)` rather than normalized `(0..1)`.

**Options for DuneBox:**
1. **Keep `GL_TEXTURE_RECTANGLE`**: OpenFrameworks supports this via
   `ofFbo::Settings::textureTarget = GL_TEXTURE_RECTANGLE_ARB`. Preserves
   integer coordinate addressing. Simplest port path.
2. **Convert to `sampler2D`**: Use `GL_TEXTURE_2D` with normalized coords.
   Replace all `texture2DRect(sampler, coord)` with
   `texture(sampler, coord / textureSize)`. Requires passing texture dimensions
   as uniforms.

**Recommendation:** Use option 1 for simulation shaders (preserves the
numerical code without modifications). Use option 2 for rendering shaders if
you want broader compatibility (e.g., OpenGL ES).

### 8.2 GLSL version compatibility

SARndbox shaders use GLSL 1.10/1.20 conventions:
- `varying` → `in`/`out` (GLSL 1.30+)
- `gl_FragColor` → user-defined `out vec4 fragColor`
- `gl_FragData[N]` → multiple `layout(location=N) out vec4`
- `texture2DRect()` → `texture()` (GLSL 1.30+)
- `texture1D()` → `texture()` (GLSL 1.30+)
- `shadow2DProj()` → `textureProj()` (GLSL 1.30+)
- `attribute` → `in`
- `gl_Vertex` → user-defined `in vec4 position`
- `gl_ModelViewProjectionMatrix` → explicit uniform

**Recommended GLSL target:** `#version 150` (OpenGL 3.2 core) or
`#version 330` for broader feature support.

### 8.3 FBO setup in OpenFrameworks

```cpp
// Quantity FBO (triple-buffered)
ofFbo quantityFbo[3];
ofFbo::Settings s;
s.width = gridWidth;
s.height = gridHeight;
s.internalformat = GL_RGB32F;
s.textureTarget = GL_TEXTURE_RECTANGLE_ARB;
s.minFilter = GL_NEAREST;
s.maxFilter = GL_NEAREST;
s.wrapModeHorizontal = GL_CLAMP;
s.wrapModeVertical = GL_CLAMP;
s.numSamples = 0;
s.useDepth = false;
for (int i = 0; i < 3; i++) quantityFbo[i].allocate(s);

// Bathymetry FBO (double-buffered)
s.width = gridWidth - 1;
s.height = gridHeight - 1;
s.internalformat = GL_R32F;
ofFbo bathymetryFbo[2];
for (int i = 0; i < 2; i++) bathymetryFbo[i].allocate(s);

// Derivative FBO (MRT: 2 attachments)
// Use ofFbo with multiple color buffers
ofFbo derivativeFbo;
derivativeFbo.allocate(gridWidth, gridHeight);
derivativeFbo.createAndAttachTexture(GL_RGB32F, 0); // derivative
derivativeFbo.createAndAttachTexture(GL_R32F, 1);   // maxStepSize
```

### 8.4 Multiple Render Targets (MRT)

`Water2SlopeAndFluxAndDerivativeShader` and `Water2SlopeShader` use
`gl_FragData[0]` and `gl_FragData[1]` (MRT). In modern GLSL:

```glsl
layout(location = 0) out vec4 derivative;
layout(location = 1) out vec4 maxStepSize;
```

OpenFrameworks `ofFbo` supports MRT via `ofFbo::setActiveDrawBuffers()`.

### 8.5 Fullscreen quad rendering

Replace the SARndbox `glBegin(GL_QUADS)` pattern with an `ofMesh` quad or
`ofDrawRectangle()`:

```cpp
shader.begin();
// Set uniforms...
ofDrawRectangle(0, 0, gridWidth, gridHeight);
shader.end();
```

Or use a VBO quad with the dynamic vertex shader equivalent.

### 8.6 Additive blending for water addition

```cpp
ofEnableBlendMode(OF_BLENDMODE_ADD);
// Render rain geometry
ofDisableBlendMode();
```

### 8.7 Simulation parameters

Default values to match SARndbox behavior:

| Parameter | Default | Notes |
|-----------|---------|-------|
| `theta` | 1.3 | Minmod limiter (1.0 = most diffusive, 2.0 = least) |
| `g` | 9.81 | Gravity |
| `epsilon` | 0.01 × max(cellSize) | Velocity desingularization |
| `attenuation` | 127/128 ≈ 0.992 | Momentum damping (per unit time) |
| `maxStepSize` | 1.0 | Maximum Δt per RK step |
| `waterMaxSteps` | 30 | Max RK iterations per frame |
| `waterSpeed` | 1.0 | Time scale (realtime = 1.0) |
| `rainStrength` | 0.25 | Water rate when raining |
| `evaporationRate` | 0.0 | Global water deposit (negative = evaporation) |
| `dryBoundary` | true | Zero-flux boundaries at edges |

### 8.8 GL_LINE_LOOP for boundary conditions

The boundary shader uses `GL_LINE_LOOP` to rasterize only the outermost pixel
ring. In modern OpenGL / OpenFrameworks:

```cpp
ofPolyline boundary;
boundary.addVertex(0.5, 0.5);
boundary.addVertex(width - 0.5, 0.5);
boundary.addVertex(width - 0.5, height - 0.5);
boundary.addVertex(0.5, height - 0.5);
boundary.close();
boundary.draw(); // with shader bound
```

### 8.9 Key porting checklist

- [ ] Port `Water2SlopeAndFluxAndDerivativeShader.fs` (primary derivative)
- [ ] Port `Water2EulerStepShader.fs` (Euler half-step)
- [ ] Port `Water2RungeKuttaStepShader.fs` (RK corrector)
- [ ] Port `Water2MaxStepSizeShader.fs` (CFL reduction)
- [ ] Port `Water2BoundaryShader.fs` (edge conditions)
- [ ] Port `Water2WaterAddShader.vs/.fs` (rain input)
- [ ] Port `Water2WaterUpdateShader.fs` (apply water changes)
- [ ] Port `Water2BathymetryUpdateShader.fs` (terrain changes)
- [ ] Port `Water2WaterAdaptShader.fs` (external water level set)
- [ ] Port `WaterRenderingShader.vs/.fs` (water surface mesh)
- [ ] Implement the simulation loop matching the RK2 sequence above
- [ ] Set up 3× quantity + 2× bathymetry + derivative + 2× maxStep + water FBOs
- [ ] Implement max step size GPU reduction (or use `glReadPixels` directly)
- [ ] Replace Vrui input handling with ofxKinect / ofxGui
- [ ] Replace Vrui matrix pipeline with ofCamera
- [ ] Update GLSL syntax to #version 150 or 330

### 8.10 Shaders NOT needed for DuneBox water simulation

These shaders are only needed if you want the SARndbox terrain rendering
(height-colored contour map projected onto the sand surface):

- `SurfaceDepthShader` — depth pre-pass
- `SurfaceElevationShader` — elevation for contour lines
- `SurfaceGlobalAmbientHeightMapShader` — ambient lit terrain
- `SurfaceShadowedIlluminatedHeightMapShader` — full Phong + shadows
- `SurfaceAddContourLines` — contour line function
- `SurfaceAddWaterColor` — water tint on terrain (alternative to 3D water mesh)
- `SurfaceIlluminate` — lighting function

For DuneBox, you likely want the **WaterRenderingShader** approach (3D water
mesh) rather than the terrain-overlay approach, as it produces a more
physically convincing water surface with specular highlights and transparency.

---

*Analysis generated from SARndbox-2.6 source code. Copyright Oliver Kreylos,
2012–2018. GPL v2.*
