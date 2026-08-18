/***********************************************************************
ComputeWaterSimulation.cpp - GPU compute-shader-based shallow water
simulation for OpenGL 4.3+.

Uses glDispatchCompute() with image2D bindings instead of FBO
ping-pong rendering. Merges the 8-pass fragment shader pipeline
into 5 compute dispatches per substep:
  1. BathymetryUpdate  (terrain sync)
  2. WaterStep mode=0  (derivative + Euler predictor)
  3. WaterStep mode=1  (derivative + RK2 corrector)
  4. Boundary           (edge conditions)
  5. WaterAdd           (rain/evaporation, when needed)
  + WaterRender        (final color output)

Original shader algorithms: Oliver Kreylos, 2012-2018 (GPL v2)
Compute shader port: DuneBox project, 2025

This file is part of DuneBox, a fork of Magic Sand.
***********************************************************************/

#include "ComputeWaterSimulation.h"
#include "ofXml.h"
#include <fstream>
#include <sstream>
#include <vector>

static const string COMPUTE_SHADER_PATH = "shaders/water/compute/";

// --- Construction ---------------------------------------------------

ComputeWaterSimulation::ComputeWaterSimulation()
    : currentQuantity(0)
    , currentBathymetry(0)
    , bathymetryDirty(true)
    , gravity(9.81f)
    , attenuation(0.99f)
    , theta(1.5f)
    , epsilon(0.01f)
    , cellSize(1.0f)
    , waterOpacity(5.0f)
    , fixedDt(0.01f)
    , maxStepsPerFrame(5)
    , enabled(false)
    , initialized(false)
    , simWidth(0)
    , simHeight(0)
    , fluidType(FLUID_WATER)
    , lavaTemperature(1.0f)
    , evaporationRate(0.0f)
    , baseAttenuation(0.99f)
    , baseWaterOpacity(5.0f)
    , groupsX(0)
    , groupsY(0)
    , bathymetryUpdateProgram(0)
    , waterStepProgram(0)
    , boundaryProgram(0)
    , waterAddProgram(0)
    , waterRenderProgram(0)
    , derivativeTex(0)
    , outputTex(0)
{
    for (int i = 0; i < 3; i++) quantityTex[i] = 0;
    for (int i = 0; i < 2; i++) bathymetryTex[i] = 0;
}

ComputeWaterSimulation::~ComputeWaterSimulation() {
    if (derivativeTex) glDeleteTextures(1, &derivativeTex);
    if (outputTex) glDeleteTextures(1, &outputTex);
    for (int i = 0; i < 3; i++) {
        if (quantityTex[i]) glDeleteTextures(1, &quantityTex[i]);
    }
    for (int i = 0; i < 2; i++) {
        if (bathymetryTex[i]) glDeleteTextures(1, &bathymetryTex[i]);
    }
    if (bathymetryUpdateProgram) glDeleteProgram(bathymetryUpdateProgram);
    if (waterStepProgram) glDeleteProgram(waterStepProgram);
    if (boundaryProgram) glDeleteProgram(boundaryProgram);
    if (waterAddProgram) glDeleteProgram(waterAddProgram);
    if (waterRenderProgram) glDeleteProgram(waterRenderProgram);
}

// --- Compute shader support check ----------------------------------

bool ComputeWaterSimulation::isComputeSupported() {
    int major = ofGetGLRenderer()->getGLVersionMajor();
    int minor = ofGetGLRenderer()->getGLVersionMinor();
    return (major > 4) || (major == 4 && minor >= 3);
}

// Check if glClearTexImage (GL 4.4) is available
static bool hasGlClearTexImage() {
    int major = ofGetGLRenderer()->getGLVersionMajor();
    int minor = ofGetGLRenderer()->getGLVersionMinor();
    return (major > 4) || (major == 4 && minor >= 4);
}

// --- GL texture allocation -----------------------------------------

GLuint ComputeWaterSimulation::allocateTexture(int w, int h, GLenum internalFormat) {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    
    GLenum format = (internalFormat == GL_RGBA8) ? GL_RGBA : GL_RGBA;
    GLenum type = (internalFormat == GL_RGBA8) ? GL_UNSIGNED_BYTE : GL_FLOAT;
    
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, w, h, 0, format, type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    // Clear to zero - use glClearTexImage (4.4) if available, else glTexSubImage2D
    if (hasGlClearTexImage()) {
        if (internalFormat == GL_RGBA32F) {
            float clearColor[4] = {0, 0, 0, 0};
            glClearTexImage(tex, 0, GL_RGBA, GL_FLOAT, clearColor);
        } else {
            unsigned char clearColor[4] = {0, 0, 0, 0};
            glClearTexImage(tex, 0, GL_RGBA, GL_UNSIGNED_BYTE, clearColor);
        }
    } else {
        // Fallback for GL 4.3: upload a zero-filled buffer via glTexSubImage2D
        size_t pixelSize = (internalFormat == GL_RGBA32F) ? sizeof(float) * 4 : 4;
        std::vector<unsigned char> zeros(w * h * pixelSize, 0);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, format, type, zeros.data());
    }
    
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

// --- Compute shader loading ----------------------------------------

GLuint ComputeWaterSimulation::loadComputeShader(const std::string& path) {
    string fullPath = ofToDataPath(path);
    
    std::ifstream file(fullPath);
    if (!file.is_open()) {
        ofLogError("ComputeWaterSimulation") << "Cannot open shader: " << fullPath;
        return 0;
    }
    
    std::stringstream ss;
    ss << file.rdbuf();
    string source = ss.str();
    const char* src = source.c_str();
    
    GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        ofLogError("ComputeWaterSimulation") << "Shader compile error (" << path << "): " << log;
        glDeleteShader(shader);
        return 0;
    }
    
    GLuint program = glCreateProgram();
    glAttachShader(program, shader);
    glLinkProgram(program);
    
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[1024];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        ofLogError("ComputeWaterSimulation") << "Shader link error (" << path << "): " << log;
        glDeleteProgram(program);
        glDeleteShader(shader);
        return 0;
    }
    
    glDeleteShader(shader);
    ofLogNotice("ComputeWaterSimulation") << "Loaded compute shader: " << path;
    return program;
}

// --- Setup ----------------------------------------------------------

void ComputeWaterSimulation::setup(int width, int height) {
    simWidth = width;
    simHeight = height;

    ofLogNotice("ComputeWaterSimulation") << "Setting up " << simWidth << "x" << simHeight
        << " compute-shader simulation grid";

    // Compute workgroup counts (16x16 threads per group)
    groupsX = (simWidth + 15) / 16;
    groupsY = (simHeight + 15) / 16;

    // --- Load compute shaders ---
    bathymetryUpdateProgram = loadComputeShader(COMPUTE_SHADER_PATH + "bathymetry_update.glsl");
    waterStepProgram        = loadComputeShader(COMPUTE_SHADER_PATH + "water_step.glsl");
    boundaryProgram         = loadComputeShader(COMPUTE_SHADER_PATH + "boundary.glsl");
    waterAddProgram         = loadComputeShader(COMPUTE_SHADER_PATH + "water_add.glsl");
    waterRenderProgram      = loadComputeShader(COMPUTE_SHADER_PATH + "water_render.glsl");

    bool allLoaded = bathymetryUpdateProgram && waterStepProgram
        && boundaryProgram && waterAddProgram && waterRenderProgram;

    if (!allLoaded) {
        ofLogError("ComputeWaterSimulation") << "One or more compute shaders failed to load";
        return;
    }

    // --- Allocate textures ---
    for (int i = 0; i < 3; i++) {
        quantityTex[i] = allocateTexture(simWidth, simHeight, GL_RGBA32F);
    }
    for (int i = 0; i < 2; i++) {
        bathymetryTex[i] = allocateTexture(simWidth, simHeight, GL_RGBA32F);
    }
    derivativeTex = allocateTexture(simWidth, simHeight, GL_RGBA32F);
    outputTex     = allocateTexture(simWidth, simHeight, GL_RGBA8);

    // --- Wrap in ofTexture for getOutputTexture() etc. ---
    // We set the texture ID without taking ownership (ofTexture won't delete it)
    outputOfTex.setUseExternalTextureID(outputTex);
    outputOfTex.texData.width = simWidth;
    outputOfTex.texData.height = simHeight;
    outputOfTex.texData.tex_w = simWidth;
    outputOfTex.texData.tex_h = simHeight;
    outputOfTex.texData.textureTarget = GL_TEXTURE_2D;
    outputOfTex.texData.glInternalFormat = GL_RGBA8;
    outputOfTex.texData.bAllocated = true;

    // Allocate FBO for depth->bathymetry format conversion (R32F -> RGBA32F).
    // glCopyImageSubData requires compatible texel formats; since the depth
    // texture is single-channel (R32F) and bathymetryTex is RGBA32F, we render
    // through this FBO to perform the conversion.
    // Force GL_TEXTURE_2D: openFrameworks defaults to ARB rectangle textures,
    // which would make the FBO texture GL_TEXTURE_RECTANGLE and mismatch the
    // GL_TEXTURE_2D bathymetry target passed to glCopyImageSubData below.
    {
        ofFboSettings fboSettings;
        fboSettings.width = simWidth;
        fboSettings.height = simHeight;
        fboSettings.internalformat = GL_RGBA32F;
        fboSettings.textureTarget = GL_TEXTURE_2D;
        fboSettings.numColorbuffers = 1;
        fboSettings.useDepth = false;
        fboSettings.useStencil = false;
        depthConversionFbo.allocate(fboSettings);
    }
    depthConversionFbo.begin();
    ofClear(0, 0, 0, 0);
    depthConversionFbo.end();

    currentQuantity = 0;
    currentBathymetry = 0;
    bathymetryDirty = true;
    initialized = true;

    ofLogNotice("ComputeWaterSimulation") << "Setup complete. Textures: "
        << simWidth << "x" << simHeight << " (compute shaders)";

    loadSettings();
}

// --- Update ---------------------------------------------------------

void ComputeWaterSimulation::update(ofTexture& depthTexture, float dt) {
    if (!enabled || !initialized) return;

    // Step 0: Copy depth texture into bathymetry
    // The depth texture is single-channel (R32F from ofxCvFloatImage) while
    // bathymetryTex is RGBA32F. glCopyImageSubData requires compatible formats,
    // so we render through an FBO to perform the conversion.
    {
        int newBathy = 1 - currentBathymetry;

        // Draw depth texture into the conversion FBO (R32F -> RGBA32F)
        depthConversionFbo.begin();
        ofClear(0, 0, 0, 0);
        depthTexture.draw(0, 0, simWidth, simHeight);
        depthConversionFbo.end();

        // Now copy the converted RGBA32F texture to bathymetry (format-compatible)
        GLuint srcTex = depthConversionFbo.getTexture().getTextureData().textureID;
        GLenum srcTarget = depthConversionFbo.getTexture().getTextureData().textureTarget;

        glCopyImageSubData(
            srcTex, srcTarget, 0, 0, 0, 0,
            bathymetryTex[newBathy], GL_TEXTURE_2D, 0, 0, 0, 0,
            simWidth, simHeight, 1
        );

        if (bathymetryDirty) {
            bathymetryDirty = false;
            currentBathymetry = newBathy;
        } else {
            // Set currentBathymetry BEFORE dispatchBathymetryUpdate so it reads correct old/new indices
            currentBathymetry = newBathy;
            // Run bathymetry update to adjust water heights
            dispatchBathymetryUpdate();
        }
    }

    // Fixed timestep simulation loop
    float timeRemaining = dt;
    int numSteps = 0;

    while (timeRemaining > 1e-8f && numSteps < maxStepsPerFrame) {
        float stepDt = min(fixedDt, timeRemaining);
        float atten = pow(attenuation, stepDt);

        // Predictor: compute derivative from q_n, Euler step -> q*
        {
            glUseProgram(waterStepProgram);
            glUniform2i(glGetUniformLocation(waterStepProgram, "gridSize"), simWidth, simHeight);
            glUniform2f(glGetUniformLocation(waterStepProgram, "cellSize"), cellSize, cellSize);
            glUniform1f(glGetUniformLocation(waterStepProgram, "theta"), theta);
            glUniform1f(glGetUniformLocation(waterStepProgram, "g"), gravity);
            glUniform1f(glGetUniformLocation(waterStepProgram, "epsilon"), epsilon);
            glUniform1f(glGetUniformLocation(waterStepProgram, "stepSize"), stepDt);
            glUniform1f(glGetUniformLocation(waterStepProgram, "attenuation"), atten);
            glUniform1i(glGetUniformLocation(waterStepProgram, "mode"), 0);
            glUniform1i(glGetUniformLocation(waterStepProgram, "fluidType"), (int)fluidType);

            glBindImageTexture(0, bathymetryTex[currentBathymetry], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
            glBindImageTexture(1, quantityTex[currentQuantity], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
            glBindImageTexture(2, quantityTex[2], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F); // q* output
            glBindImageTexture(3, derivativeTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
            glBindImageTexture(4, quantityTex[2], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F); // unused in mode 0

            glDispatchCompute(groupsX, groupsY, 1);
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        }

        // Corrector: compute derivative from q*, RK2 step -> q_new
        {
            int targetQ = 1 - currentQuantity;

            glUseProgram(waterStepProgram);
            glUniform1i(glGetUniformLocation(waterStepProgram, "mode"), 1);

            glBindImageTexture(0, bathymetryTex[currentBathymetry], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
            glBindImageTexture(1, quantityTex[currentQuantity], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
            glBindImageTexture(2, quantityTex[targetQ], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
            glBindImageTexture(3, derivativeTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
            glBindImageTexture(4, quantityTex[2], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F); // q* input

            glDispatchCompute(groupsX, groupsY, 1);
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

            currentQuantity = targetQ;
        }

        // Boundary conditions
        dispatchBoundary();

        timeRemaining -= stepDt;
        numSteps++;
    }

    // Apply water additions (rain gestures)
    if (!pendingWaterAdds.empty()) {
        dispatchWaterAdd(fixedDt);
        pendingWaterAdds.clear();
    }

    // Lava cooling
    if (fluidType == FLUID_LAVA) {
        lavaTemperature = max(0.0f, lavaTemperature - 0.001f * dt);
    }

    // Render water overlay
    dispatchWaterRender();
}

// --- Dispatch helpers -----------------------------------------------

void ComputeWaterSimulation::dispatchBathymetryUpdate() {
    int newBathy = currentBathymetry; // currentBathymetry was set to new before this call
    int oldBathy = 1 - currentBathymetry;
    int newQ = 1 - currentQuantity;

    glUseProgram(bathymetryUpdateProgram);
    glUniform2i(glGetUniformLocation(bathymetryUpdateProgram, "gridSize"), simWidth, simHeight);

    glBindImageTexture(0, bathymetryTex[oldBathy], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
    glBindImageTexture(1, bathymetryTex[newBathy], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
    glBindImageTexture(2, quantityTex[currentQuantity], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
    glBindImageTexture(3, quantityTex[newQ], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);

    glDispatchCompute(groupsX, groupsY, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    currentQuantity = newQ;
}

void ComputeWaterSimulation::dispatchBoundary() {
    int totalBoundary = 2 * simWidth + 2 * simHeight;
    int boundaryGroups = (totalBoundary + 255) / 256;

    glUseProgram(boundaryProgram);
    glUniform2i(glGetUniformLocation(boundaryProgram, "gridSize"), simWidth, simHeight);

    glBindImageTexture(0, bathymetryTex[currentBathymetry], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
    glBindImageTexture(1, quantityTex[currentQuantity], 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);

    glDispatchCompute(boundaryGroups, 1, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

void ComputeWaterSimulation::dispatchWaterAdd(float stepSize) {
    int targetQ = 1 - currentQuantity;

    glUseProgram(waterAddProgram);
    glUniform2i(glGetUniformLocation(waterAddProgram, "gridSize"), simWidth, simHeight);
    glUniform1f(glGetUniformLocation(waterAddProgram, "stepSize"), stepSize);
    glUniform1f(glGetUniformLocation(waterAddProgram, "evaporationRate"), evaporationRate);
    glUniform1i(glGetUniformLocation(waterAddProgram, "fluidType"), (int)fluidType);

    // Upload sources
    int numSources = min((int)pendingWaterAdds.size(), 32);
    glUniform1i(glGetUniformLocation(waterAddProgram, "numSources"), numSources);

    for (int i = 0; i < numSources; i++) {
        string name = "sources[" + ofToString(i) + "]";
        glUniform4f(glGetUniformLocation(waterAddProgram, name.c_str()),
            pendingWaterAdds[i].x, pendingWaterAdds[i].y,
            pendingWaterAdds[i].radius, pendingWaterAdds[i].amount);
    }

    glBindImageTexture(0, bathymetryTex[currentBathymetry], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
    glBindImageTexture(1, quantityTex[currentQuantity], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
    glBindImageTexture(2, quantityTex[targetQ], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);

    glDispatchCompute(groupsX, groupsY, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    currentQuantity = targetQ;
}

void ComputeWaterSimulation::dispatchWaterRender() {
    glUseProgram(waterRenderProgram);
    glUniform2i(glGetUniformLocation(waterRenderProgram, "gridSize"), simWidth, simHeight);
    glUniform1f(glGetUniformLocation(waterRenderProgram, "waterOpacity"), waterOpacity);
    glUniform1f(glGetUniformLocation(waterRenderProgram, "time"), ofGetElapsedTimef());
    glUniform1i(glGetUniformLocation(waterRenderProgram, "fluidType"), (int)fluidType);
    glUniform1f(glGetUniformLocation(waterRenderProgram, "lavaTemp"), lavaTemperature);

    glBindImageTexture(0, quantityTex[currentQuantity], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
    glBindImageTexture(1, bathymetryTex[currentBathymetry], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
    glBindImageTexture(2, outputTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

    glDispatchCompute(groupsX, groupsY, 1);
    // outputTex is written here via image store (binding 2) and then sampled as a
    // regular texture in draw() (outputOfTex). Image-access visibility alone is not
    // enough for a subsequent texture fetch, so also flush the texture-fetch barrier
    // to avoid sampling stale/partial results.
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
}

// --- Draw -----------------------------------------------------------

void ComputeWaterSimulation::draw() {
    if (!enabled || !initialized) return;

    ofEnableAlphaBlending();
    outputOfTex.draw(0, 0, simWidth, simHeight);
    ofDisableAlphaBlending();
}

void ComputeWaterSimulation::draw(float w, float h) {
    if (!enabled || !initialized) return;

    ofEnableAlphaBlending();
    outputOfTex.draw(0, 0, w, h);
    ofDisableAlphaBlending();
}

// --- Public API -----------------------------------------------------

void ComputeWaterSimulation::addWater(float x, float y, float radius, float amount) {
    pendingWaterAdds.push_back({x, y, radius, amount});
}

ofTexture& ComputeWaterSimulation::getOutputTexture() {
    return outputOfTex;
}

ofTexture& ComputeWaterSimulation::getQuantityTexture() {
    // Wrap current quantity texture for external access
    quantityOfTex.setUseExternalTextureID(quantityTex[currentQuantity]);
    quantityOfTex.texData.width = simWidth;
    quantityOfTex.texData.height = simHeight;
    quantityOfTex.texData.tex_w = simWidth;
    quantityOfTex.texData.tex_h = simHeight;
    quantityOfTex.texData.textureTarget = GL_TEXTURE_2D;
    quantityOfTex.texData.glInternalFormat = GL_RGBA32F;
    quantityOfTex.texData.bAllocated = true;
    return quantityOfTex;
}

ofTexture& ComputeWaterSimulation::getBathymetryTexture() {
    bathymetryOfTex.setUseExternalTextureID(bathymetryTex[currentBathymetry]);
    bathymetryOfTex.texData.width = simWidth;
    bathymetryOfTex.texData.height = simHeight;
    bathymetryOfTex.texData.tex_w = simWidth;
    bathymetryOfTex.texData.tex_h = simHeight;
    bathymetryOfTex.texData.textureTarget = GL_TEXTURE_2D;
    bathymetryOfTex.texData.glInternalFormat = GL_RGBA32F;
    bathymetryOfTex.texData.bAllocated = true;
    return bathymetryOfTex;
}

// --- Settings persistence -------------------------------------------

void ComputeWaterSimulation::loadSettings(const std::string& path) {
    ofXml xml;
    if (!xml.load(path)) {
        ofLogNotice("ComputeWaterSimulation") << "No settings file at " << path << ", using defaults";
        return;
    }
    auto root = xml.getChild("WATERSIMULATION");
    if (auto c = root.getChild("gravity")) gravity = c.getFloatValue();
    if (auto c = root.getChild("attenuation")) attenuation = c.getFloatValue();
    if (auto c = root.getChild("theta")) theta = c.getFloatValue();
    if (auto c = root.getChild("epsilon")) epsilon = c.getFloatValue();
    if (auto c = root.getChild("cellSize")) cellSize = c.getFloatValue();
    if (auto c = root.getChild("waterOpacity")) waterOpacity = c.getFloatValue();
    if (auto c = root.getChild("fixedDt")) fixedDt = c.getFloatValue();
    if (auto c = root.getChild("maxStepsPerFrame")) maxStepsPerFrame = c.getIntValue();
    if (auto c = root.getChild("enabled")) enabled = c.getBoolValue();

    ofLogNotice("ComputeWaterSimulation") << "Settings loaded from " << path;
}

void ComputeWaterSimulation::saveSettings(const std::string& path) {
    ofXml xml;
    auto root = xml.appendChild("WATERSIMULATION");
    root.appendChild("gravity").set(gravity);
    root.appendChild("attenuation").set(attenuation);
    root.appendChild("theta").set(theta);
    root.appendChild("epsilon").set(epsilon);
    root.appendChild("cellSize").set(cellSize);
    root.appendChild("waterOpacity").set(waterOpacity);
    root.appendChild("fixedDt").set(fixedDt);
    root.appendChild("maxStepsPerFrame").set(maxStepsPerFrame);
    root.appendChild("enabled").set(enabled);

    if (xml.save(path)) {
        ofLogNotice("ComputeWaterSimulation") << "Settings saved to " << path;
    } else {
        ofLogError("ComputeWaterSimulation") << "Failed to save settings to " << path;
    }
}

void ComputeWaterSimulation::setGravity(float g) { gravity = g; }
void ComputeWaterSimulation::setAttenuation(float a) { attenuation = a; }
void ComputeWaterSimulation::setTheta(float t) { theta = t; }
void ComputeWaterSimulation::setEpsilon(float e) { epsilon = e; }
void ComputeWaterSimulation::setCellSize(float cs) { cellSize = cs; }
void ComputeWaterSimulation::setWaterOpacity(float opacity) { waterOpacity = opacity; }
void ComputeWaterSimulation::setMaxStepsPerFrame(int steps) { maxStepsPerFrame = steps; }
void ComputeWaterSimulation::setEnabled(bool e) { enabled = e; }

void ComputeWaterSimulation::setFluidType(FluidType type) {
    if (type == fluidType) return;

    if (type == FLUID_LAVA) {
        baseAttenuation = attenuation;
        baseWaterOpacity = waterOpacity;
        if (attenuation > 0.85f) attenuation = 0.85f;
        waterOpacity = 8.0f;
        lavaTemperature = 1.0f;
    } else {
        attenuation = baseAttenuation;
        waterOpacity = baseWaterOpacity;
    }
    fluidType = type;
    ofLogNotice("ComputeWaterSimulation") << "Fluid type: "
        << (fluidType == FLUID_LAVA ? "LAVA" : "WATER");
}

void ComputeWaterSimulation::setLavaTemperature(float temp) {
    lavaTemperature = ofClamp(temp, 0.0f, 1.0f);
}

void ComputeWaterSimulation::setEvaporationRate(float rate) {
    evaporationRate = max(0.0f, rate);
}
