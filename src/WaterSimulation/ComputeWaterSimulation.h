/***********************************************************************
ComputeWaterSimulation.h - GPU compute-shader-based shallow water
simulation for OpenGL 4.3+.

Drop-in replacement for WaterSimulation (fragment shader version).
Uses glDispatchCompute() with image2D bindings instead of FBO
ping-pong draw calls, reducing driver overhead and enabling
dual-fluid (water + lava) support.

Implements the same Saint-Venant shallow-water equations:
- Kurganov-Petrova central-upwind scheme
- Minmod flux limiter
- Two-stage Heun (RK2) Runge-Kutta integration

Original shader algorithms: Oliver Kreylos, 2012-2018 (GPL v2)
Compute shader port: DuneBox project, 2025

This file is part of DuneBox, a fork of Magic Sand.
***********************************************************************/

#pragma once

#include "ofMain.h"

class ComputeWaterSimulation {
public:
    /// Fluid type enum for dual-fluid support
    enum FluidType {
        FLUID_WATER = 0,
        FLUID_LAVA  = 1
    };

    ComputeWaterSimulation();
    ~ComputeWaterSimulation();

    /// Initialize textures and load compute shaders.
    void setup(int width, int height);

    /// Run one simulation step (RK2 integration).
    void update(ofTexture& depthTexture, float dt);

    /// Draw the water overlay to the current framebuffer.
    void draw();

    /// Draw scaled to given dimensions.
    void draw(float w, float h);

    /// Add water/lava at a point. Coordinates in simulation grid space.
    void addWater(float x, float y, float radius, float amount);

    /// Get the rendered overlay texture for compositing.
    ofTexture& getOutputTexture();

    /// Get the current water quantity texture.
    ofTexture& getQuantityTexture();

    /// Get the current bathymetry texture.
    ofTexture& getBathymetryTexture();

    // --- Configuration (same interface as WaterSimulation) ---
    void setGravity(float g);
    float getGravity() const { return gravity; }

    void setAttenuation(float a);
    float getAttenuation() const { return attenuation; }

    void setTheta(float t);
    float getTheta() const { return theta; }

    void setEpsilon(float e);
    float getEpsilon() const { return epsilon; }

    void setCellSize(float cs);
    float getCellSize() const { return cellSize; }

    void setWaterOpacity(float opacity);
    float getWaterOpacity() const { return waterOpacity; }

    void setMaxStepsPerFrame(int steps);
    int getMaxStepsPerFrame() const { return maxStepsPerFrame; }

    void setEnabled(bool enabled);
    bool isEnabled() const { return enabled; }

    /// True once setup() successfully loaded all compute shaders and allocated
    /// textures. If false after setup(), the caller should fall back to the
    /// fragment-shader water simulation.
    bool isInitialized() const { return initialized; }

    void loadSettings(const std::string& path = "settings/waterSettings.xml");
    void saveSettings(const std::string& path = "settings/waterSettings.xml");

    int getSimWidth() const { return simWidth; }
    int getSimHeight() const { return simHeight; }

    // --- Dual-fluid extensions ---
    void setFluidType(FluidType type);
    FluidType getFluidType() const { return fluidType; }

    void setLavaTemperature(float temp);
    float getLavaTemperature() const { return lavaTemperature; }

    void setEvaporationRate(float rate);
    float getEvaporationRate() const { return evaporationRate; }

    /// Check if OpenGL 4.3+ compute shaders are available
    static bool isComputeSupported();

private:
    // --- Compute dispatch helpers ---
    void dispatchBathymetryUpdate();
    void dispatchWaterStep(int mode); // 0=predictor, 1=corrector
    void dispatchBoundary();
    void dispatchWaterAdd(float stepSize);
    void dispatchWaterRender();

    /// Load a compute shader from a .glsl file
    GLuint loadComputeShader(const std::string& path);

    /// Allocate a GL texture (RGBA32F or RGBA8)
    GLuint allocateTexture(int w, int h, GLenum internalFormat);

    // --- GL texture handles ---
    // Quantity textures: (w, hu, hv) in RGB. Triple-buffered.
    GLuint quantityTex[3];   // [0],[1] ping-pong, [2] Euler scratch (q*)

    // Bathymetry textures: terrain elevation. Double-buffered.
    GLuint bathymetryTex[2];

    // Derivative scratch texture
    GLuint derivativeTex;

    // Rendered output (RGBA8)
    GLuint outputTex;

    // --- Compute shader programs ---
    GLuint bathymetryUpdateProgram;
    GLuint waterStepProgram;
    GLuint boundaryProgram;
    GLuint waterAddProgram;
    GLuint waterRenderProgram;

    // --- ofTexture wrappers (for getOutputTexture() etc.) ---
    ofTexture quantityOfTex;
    ofTexture bathymetryOfTex;
    ofTexture outputOfTex;

    // --- Water addition queue ---
    struct WaterAddition {
        float x, y, radius, amount;
    };
    std::vector<WaterAddition> pendingWaterAdds;

    // --- State ---
    int currentQuantity;
    int currentBathymetry;
    bool bathymetryDirty;

    // --- Parameters ---
    float gravity;
    float attenuation;
    float theta;
    float epsilon;
    float cellSize;
    float waterOpacity;
    float fixedDt;
    int maxStepsPerFrame;
    bool enabled;
    bool initialized;
    int simWidth, simHeight;

    // --- Dual-fluid ---
    FluidType fluidType;
    float lavaTemperature; // 0.0 = cooled, 1.0 = molten
    float evaporationRate;

    // --- Workgroup dimensions ---
    int groupsX, groupsY;

    // --- FBO for depth->bathymetry format conversion (R32F -> RGBA32F) ---
    ofFbo depthConversionFbo;
};
