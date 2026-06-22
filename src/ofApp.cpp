/***********************************************************************
ofApp.cpp - main openframeworks app
Copyright (c) 2016-2017 Thomas Wolf and Rasmus R. Paulsen (people.compute.dtu.dk/rapa)

This file is part of the Magic Sand.

The Magic Sand is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the
License, or (at your option) any later version.

The Magic Sand is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License along
with the Augmented Reality Sandbox; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
***********************************************************************/

#include "ofApp.h"

ofApp::~ofApp() {
	delete sandSurfaceRenderer;
	sandSurfaceRenderer = nullptr;
}

void ofApp::setup() {
	// OF basics
	ofSetFrameRate(60);
	ofBackground(0);
	ofSetVerticalSync(true);
	ofSetLogLevel(OF_LOG_VERBOSE);
	ofSetLogLevel("ofThread", OF_LOG_WARNING);
	ofSetLogLevel("ofFbo", OF_LOG_ERROR);
	ofSetLogLevel("ofShader", OF_LOG_ERROR);
	ofSetLogLevel("ofxKinect", OF_LOG_WARNING);

	// Setup kinectProjector
	kinectProjector = std::make_shared<KinectProjector>(projWindow);
	kinectProjector->setup(true);
	
	// Setup sandSurfaceRenderer
	sandSurfaceRenderer = new SandSurfaceRenderer(kinectProjector, projWindow);
	sandSurfaceRenderer->setup(true);

	// Theme display overlay state
	themeDisplayName = sandSurfaceRenderer->getThemeName();
	themeDisplayTimer = 0;
	preLavaThemeIndex = 0;
	
	// Retrieve variables
	ofVec2f kinectRes = kinectProjector->getKinectRes();
	ofVec2f projRes = ofVec2f(projWindow->getWidth(), projWindow->getHeight());
	ofRectangle kinectROI = kinectProjector->getKinectROI();
	////mainWindowROI = ofRectangle(600, 30, 600, 450);
	//mainWindowROI = ofRectangle(0, 0, 640, 480);
	mainWindowROI = ofRectangle((ofGetWindowWidth()-kinectRes.x)/2, (ofGetWindowHeight()-kinectRes.y)/2, kinectRes.x, kinectRes.y);

	mapGameController.setup(kinectProjector);
	mapGameController.setProjectorRes(projRes);
	mapGameController.setKinectRes(kinectRes);
	mapGameController.setKinectROI(kinectROI);

	boidGameController.setup(kinectProjector);
	boidGameController.setProjectorRes(projRes);
	boidGameController.setKinectRes(kinectRes);
	boidGameController.setKinectROI(kinectROI);

	// Setup water simulation at Kinect depth resolution
	int waterW = (int)kinectRes.x;
	int waterH = (int)kinectRes.y;
	if (waterW == 0) waterW = 640;
	if (waterH == 0) waterH = 480;
	waterSimSetup(waterW, waterH);

	// Allocate test terrain FBO for no-Kinect fallback
	useTestTerrain = false;
	ofFboSettings terrainSettings;
	terrainSettings.width = waterW;
	terrainSettings.height = waterH;
	terrainSettings.internalformat = GL_RGBA32F;
	terrainSettings.textureTarget = GL_TEXTURE_2D;
	terrainSettings.minFilter = GL_LINEAR;
	terrainSettings.maxFilter = GL_LINEAR;
	terrainSettings.useDepth = false;
	testTerrainFbo.allocate(terrainSettings);
	generateTestTerrain();

	ofLogNotice("ofApp") << "Water simulation initialized (" << waterW << "x" << waterH
	                     << "). Press 'w' to toggle.";

	// Run startup diagnostics. Pass the real Kinect status from kinectProjector
	// (set up above) so the report shows the configured v1/v2 sensor correctly
	// instead of only probing the legacy v1 API. Results are logged to the
	// console (no blocking on-screen overlay — see Diagnostics.h).
	diagnostics.probe(kinectProjector->getKinectVersion(), kinectProjector->isKinectConnected());
	diagnostics.log();

	// Inter-app bridge to DuneBox-sandcam
	bridge.setup("127.0.0.1", 9876);
}


void ofApp::update() {
    // Call kinectProjector->update() first during the update function()
	kinectProjector->update();
   	sandSurfaceRenderer->update();
    
    //if (kinectProjector->isROIUpdated())
	if (kinectProjector->getKinectROI() != mapGameController.getKinectROI())
	{
		ofRectangle kinectROI = kinectProjector->getKinectROI();
		mapGameController.setKinectROI(kinectROI);
		boidGameController.setKinectROI(kinectROI);
	}

	mapGameController.update();
	boidGameController.update();

	// --- Inter-app bridge ---
	bridge.update();
	for (auto& msg : bridge.poll()) {
		std::string type = msg.value("type", "");
		if (type == "marker_detected") {
			int markerId = msg.value("marker_id", -1);
			ofLogNotice("Bridge") << "Marker detected: " << markerId;
		} else if (type == "mode_change") {
			std::string mode = msg.value("mode", "");
			bool on = msg.value("enabled", false);
			if (mode == "water") {
				if (useComputeWaterSim) waterSimCompute.setEnabled(on);
				else waterSimFragment.setEnabled(on);
				ofLogNotice("Bridge") << "Water simulation: " << (on ? "ON" : "OFF");
			}
		} else if (type == "pong") {
			ofLogVerbose("Bridge") << "Pong received";
		} else if (type == "volcano_eruption") {
			// Eruption from Python side: enable lava and add lava source
			float vx = msg.value("x", 0.5f);
			float vy = msg.value("y", 0.5f);
			if (!waterSimIsLavaMode()) {
				preLavaThemeIndex = sandSurfaceRenderer->getThemeIndex();
				sandSurfaceRenderer->setTheme(2); // Volcanic
				themeDisplayName = "Volcanic (Eruption!)";
				themeDisplayTimer = 3.0f;
				if (!waterSimIsEnabled()) waterSimToggleEnabled();
				waterSimSetLavaMode(true);
			}
			float simX = vx * waterSimGetSimWidth();
			float simY = vy * waterSimGetSimHeight();
			volcanoEruptionActive = true;
			volcanoEruptionTimer = 3.0f;
			volcanoSourceX = simX;
			volcanoSourceY = simY;
			ofLogNotice("Bridge") << "Volcano eruption at (" << vx << ", " << vy << ")";
		}
	}

	// Send water status every 60 frames
	bridgeFrameCounter++;
	if (bridgeFrameCounter >= 60) {
		bridgeFrameCounter = 0;
		if (bridge.isConnected()) {
			ofJson status;
			status["enabled"] = waterSimIsEnabled();
			bridge.send("water_status", status);
		}
	}

	// Theme display timer
	if (themeDisplayTimer > 0)
		themeDisplayTimer -= ofGetLastFrameTime();

	// Volcano eruption: inject lava source over several frames
	if (volcanoEruptionActive) {
		volcanoEruptionTimer -= ofGetLastFrameTime();
		if (volcanoEruptionTimer > 0) {
			waterSimAddWater(volcanoSourceX, volcanoSourceY, 20.0f, 0.5f);
		} else {
			volcanoEruptionActive = false;
		}
	}

	// --- Water simulation update ---
	if (waterSimIsEnabled()) {
		// Determine depth texture source
		if (kinectProjector->isKinectConnected() &&
		    kinectProjector->GetApplicationState() == KinectProjector::APPLICATION_STATE_RUNNING)
		{
			// Use live Kinect depth data
			useTestTerrain = false;
			ofTexture& depthTex = kinectProjector->getTexture();
			waterSimUpdate(depthTex, ofGetLastFrameTime());
		}
		else
		{
			// No Kinect — use procedural test terrain
			useTestTerrain = true;
			generateTestTerrain();
			ofTexture& testTex = testTerrainFbo.getTexture();
			waterSimUpdate(testTex, ofGetLastFrameTime());
		}

		// Rain gesture detection
		detectRainGesture();
	}
}


void ofApp::draw() 
{
	float x = mainWindowROI.x;
	float y = mainWindowROI.y;
	float w = mainWindowROI.width;
	float h = mainWindowROI.height;

	if (kinectProjector->GetApplicationState() == KinectProjector::APPLICATION_STATE_RUNNING)
	{
		sandSurfaceRenderer->drawMainWindow(x, y, w, h);//400, 20, 400, 300);
		boidGameController.drawMainWindow(x, y, w, h);
	}

	kinectProjector->drawMainWindow(x, y, w, h);

	if (kinectProjector->GetApplicationState() != KinectProjector::APPLICATION_STATE_RUNNING)
	{
		// Overlay the setup guidance ON TOP of the raw Kinect color view, which
		// is otherwise meaningless to a setup operator and looks like a broken
		// running screen.
		std::string hint =
			"DuneBox - SETUP (not yet calibrated)\n"
			"The sand topography appears here once calibration is loaded.\n"
			"\n"
			"Press SPACE to start. If it stays in SETUP, the projector/kinect\n"
			"calibration is missing or was made for a different sensor.\n"
			"Open the GUI panel and run 'Automatically calibrate kinect & projector',\n"
			"then SPACE again. Watch 'Application state' / 'Calibration Step' in the GUI.";
		int hx = ofGetWidth() / 2 - 240;
		int hy = ofGetHeight() / 2 - 30;
		// Dark backing box so the white text is readable over any color feed.
		ofSetColor(0, 0, 0, 170);
		ofDrawRectangle(hx - 12, hy - 24, 560, 150);
		ofSetColor(255);
		ofDrawBitmapString(hint, hx, hy);
		ofSetColor(255);
	}

	// Bottom-left status indicator. Only advertise the active theme once the
	// app is actually RUNNING; during SETUP show the real state so the screen
	// doesn't look like a broken "Theme: Topo" running view.
	{
		bool running = kinectProjector->GetApplicationState() == KinectProjector::APPLICATION_STATE_RUNNING;
		std::string name = sandSurfaceRenderer->getThemeName();
		std::string label = running
			? ("Theme: " + name)
			: "SETUP - not calibrated (calibrate via the GUI panel)";
		float alpha = 180;
		if (running && themeDisplayTimer > 0) {
			// Bright flash when recently switched
			alpha = 255;
			if (themeDisplayTimer < 0.5f)
				alpha = ofMap(themeDisplayTimer, 0, 0.5f, 180, 255);
		}
		int tx = 12;
		int ty = ofGetHeight() - 14;
		// Shadow
		ofSetColor(0, 0, 0, (int)(alpha * 0.7f));
		ofDrawBitmapString(label, tx + 1, ty + 1);
		// Foreground
		ofSetColor(255, 255, 255, (int)alpha);
		ofDrawBitmapString(label, tx, ty);
		ofSetColor(255);
	}

	// Theme switch banner (temporary large text)
	if (kinectProjector->GetApplicationState() == KinectProjector::APPLICATION_STATE_RUNNING && themeDisplayTimer > 0) {
		float fade = (themeDisplayTimer < 0.5f) ? themeDisplayTimer / 0.5f : 1.0f;
		int cx = ofGetWidth() / 2 - (int)(themeDisplayName.length() * 4);
		int cy = ofGetHeight() / 2;
		ofSetColor(0, 0, 0, (int)(200 * fade));
		ofDrawBitmapString(themeDisplayName, cx + 1, cy + 1);
		ofSetColor(255, 255, 80, (int)(255 * fade));
		ofDrawBitmapString(themeDisplayName, cx, cy);
		ofSetColor(255);
	}
}

void ofApp::drawProjWindow(ofEventArgs &args) 
{
	if (kinectProjector->GetApplicationState() == KinectProjector::APPLICATION_STATE_RUNNING)
	{
		sandSurfaceRenderer->drawProjectorWindow();

		// Water overlay — composited on top of terrain, under game layers
		if (waterSimIsEnabled()) {
			float projW = projWindow->getWidth();
			float projH = projWindow->getHeight();
			waterSimDraw(projW, projH);
		}

		mapGameController.drawProjectorWindow();
		boidGameController.drawProjectorWindow();
	}
	else if (useTestTerrain && waterSimIsEnabled())
	{
		// No Kinect running — still show water on test terrain
		float projW = projWindow->getWidth();
		float projH = projWindow->getHeight();
		ofSetColor(40, 30, 20); // Dark brown background for test terrain
		ofDrawRectangle(0, 0, projW, projH);
		ofSetColor(255);
		waterSimDraw(projW, projH);
	}
	kinectProjector->drawProjectorWindow();
}

void ofApp::keyPressed(int key) 
{
	if (key == 'c')
	{
		kinectProjector->SaveKinectColorImage();
	}
	else if (key == 'd')
	{
		kinectProjector->SaveFilteredDepthImage();
	}
	else if (key == ' ')
	{
		if (kinectProjector->GetApplicationState() == KinectProjector::APPLICATION_STATE_RUNNING && 
			boidGameController.isIdle()) // do not start map game if boidgame is not idle
		{
			if (mapGameController.isIdle())
			{
				mapGameController.setDebug(kinectProjector->getDumpDebugFiles());
				mapGameController.StartGame();
			}
			else
			{
				mapGameController.ButtonPressed();
			}
		}
		else if (kinectProjector->GetApplicationState() == KinectProjector::APPLICATION_STATE_SETUP)
		{
			// Try to start the application
			kinectProjector->startApplication();
		}
	}
	else if (key == 'f' || key == 'r')
	{
		if (kinectProjector->GetApplicationState() == KinectProjector::APPLICATION_STATE_RUNNING)
		{
			if (mapGameController.isIdle())
			{
				boidGameController.setDebug(kinectProjector->getDumpDebugFiles());
				boidGameController.StartGame(2);
			}
			else 
			{
				mapGameController.EndButtonPressed();
			}
		}
	}
	else if (key == '1') // Absolute beginner
	{
		if (kinectProjector->GetApplicationState() == KinectProjector::APPLICATION_STATE_RUNNING && mapGameController.isIdle())
		{
			boidGameController.setDebug(kinectProjector->getDumpDebugFiles());
			boidGameController.StartGame(0);
		}
	}
	else if (key == '2') 
	{
		if (kinectProjector->GetApplicationState() == KinectProjector::APPLICATION_STATE_RUNNING && mapGameController.isIdle())
		{
			boidGameController.setDebug(kinectProjector->getDumpDebugFiles());
			boidGameController.StartGame(1);
		}
	}
	else if (key == '3')
	{
		if (kinectProjector->GetApplicationState() == KinectProjector::APPLICATION_STATE_RUNNING && mapGameController.isIdle())
		{
			boidGameController.setDebug(kinectProjector->getDumpDebugFiles());
			boidGameController.StartGame(2);
		}
	}
	else if (key == '4')
	{
		if (kinectProjector->GetApplicationState() == KinectProjector::APPLICATION_STATE_RUNNING && mapGameController.isIdle())
		{
			boidGameController.setDebug(kinectProjector->getDumpDebugFiles());
			boidGameController.StartGame(3);
		}
	}
	else if (key == 'm')
	{
		if (kinectProjector->GetApplicationState() == KinectProjector::APPLICATION_STATE_RUNNING && mapGameController.isIdle())
		{
			boidGameController.setDebug(kinectProjector->getDumpDebugFiles());
			boidGameController.StartSeekMotherGame();
		}
	}
	else if (key == 't')
	{
		// Cycle color theme (biome mode)
		sandSurfaceRenderer->cycleTheme();
		themeDisplayName = sandSurfaceRenderer->getThemeName();
		themeDisplayTimer = 2.5f; // show name for 2.5 seconds
		ofLogNotice("ofApp") << "Theme: " << themeDisplayName;
	}
	else if (key == 'T')
	{
		// Debug: RealTimeTestMe (was on 't')
		mapGameController.setDebug(kinectProjector->getDumpDebugFiles());
		mapGameController.RealTimeTestMe();
	}
	else if (key == 'w')
	{
		// Toggle water simulation
		waterSimToggleEnabled();
		ofLogNotice("ofApp") << "Water simulation: " << (waterSimIsEnabled() ? "ON" : "OFF");
		cout << "Water simulation: " << (waterSimIsEnabled() ? "ON" : "OFF") << endl;
	}
	else if (key == 'W')
	{
		// Original debug test (moved from 'w')
		mapGameController.setDebug(kinectProjector->getDumpDebugFiles());
		mapGameController.DebugTestMe();
	}
	else if (key == 'b')
	{
		// Send a ping to the Python bridge
		if (bridge.isConnected()) {
			bridge.send("ping");
			ofLogNotice("Bridge") << "Ping sent";
		} else {
			ofLogNotice("Bridge") << "Not connected";
		}
	}
	else if (key == 'n')
	{
		// Toggle day/night cycle
		sandSurfaceRenderer->toggleDayNight();
	}
	else if (key == 'l')
	{
		// Toggle lava simulation mode
		bool newLavaState = !waterSimIsLavaMode();
		if (newLavaState) {
			// Save current theme and switch to Volcanic
			preLavaThemeIndex = sandSurfaceRenderer->getThemeIndex();
			sandSurfaceRenderer->setTheme(2); // Volcanic
			themeDisplayName = "Volcanic (Lava)";
			themeDisplayTimer = 2.5f;
			// Enable water sim if not already on
			if (!waterSimIsEnabled()) {
				waterSimToggleEnabled();
			}
		} else {
			// Restore previous theme
			sandSurfaceRenderer->setTheme(preLavaThemeIndex);
			themeDisplayName = sandSurfaceRenderer->getThemeName();
			themeDisplayTimer = 2.5f;
		}
		waterSimSetLavaMode(newLavaState);
		ofLogNotice("ofApp") << "Lava mode: " << (newLavaState ? "ON" : "OFF");
	}
	else if (key == 'v')
	{
		// Manual volcano eruption at center of sandbox
		if (!waterSimIsLavaMode()) {
			preLavaThemeIndex = sandSurfaceRenderer->getThemeIndex();
			sandSurfaceRenderer->setTheme(2); // Volcanic
			themeDisplayName = "Volcanic (Eruption!)";
			themeDisplayTimer = 3.0f;
			if (!waterSimIsEnabled()) waterSimToggleEnabled();
			waterSimSetLavaMode(true);
		}
		float cx = waterSimGetSimWidth() * 0.5f;
		float cy = waterSimGetSimHeight() * 0.5f;
		volcanoEruptionActive = true;
		volcanoEruptionTimer = 3.0f;
		volcanoSourceX = cx;
		volcanoSourceY = cy;
		ofLogNotice("ofApp") << "Volcano eruption triggered at center";
	}
}

void ofApp::keyReleased(int key) {

}

void ofApp::mouseMoved(int x, int y) {

}

void ofApp::mouseDragged(int x, int y, int button) {

	// We assume that we only use this during ROI annotation
	kinectProjector->mouseDragged(x - mainWindowROI.x, y - mainWindowROI.y, button);
}

void ofApp::mousePressed(int x, int y, int button) 
{
	if (mainWindowROI.inside((float)x, (float)y))
	{
		kinectProjector->mousePressed(x-mainWindowROI.x, y-mainWindowROI.y, button);
	}
}

void ofApp::mouseReleased(int x, int y, int button) {
	// We assume that we only use this during ROI annotation
	kinectProjector->mouseReleased(x - mainWindowROI.x, y - mainWindowROI.y, button);

}

void ofApp::mouseEntered(int x, int y) {

}

void ofApp::mouseExited(int x, int y) {

}

void ofApp::windowResized(int w, int h) {

}

void ofApp::gotMessage(ofMessage msg) {

}

void ofApp::dragEvent(ofDragInfo dragInfo) {

}

// ─── Water simulation helpers ───────────────────────────────────────

void ofApp::generateTestTerrain() {
	testTerrainFbo.begin();
	ofClear(0, 0, 0, 0);

	// Draw a sine-wave heightfield as a grayscale image.
	// The water sim treats the red channel as terrain elevation.
	float t = ofGetElapsedTimef() * 0.1f; // very slow drift for visual interest
	int w = testTerrainFbo.getWidth();
	int h = testTerrainFbo.getHeight();

	ofMesh mesh;
	mesh.setMode(OF_PRIMITIVE_POINTS);

	for (int y = 0; y < h; y += 2) {
		for (int x = 0; x < w; x += 2) {
			float fx = (float)x / w;
			float fy = (float)y / h;
			// Rolling hills with a central valley
			float elev = 0.5f
			    + 0.2f * sin(fx * 4.0f * PI + t)
			    + 0.15f * sin(fy * 3.0f * PI + t * 0.7f)
			    + 0.1f * cos((fx + fy) * 5.0f * PI);
			// Clamp to [0,1]
			elev = ofClamp(elev, 0.0f, 1.0f);

			ofFloatColor c(elev, 0.0f, 0.0f, 1.0f);
			mesh.addVertex(ofVec3f(x, y, 0));
			mesh.addColor(c);
		}
	}

	ofSetColor(255);
	mesh.draw();
	testTerrainFbo.end();
}

void ofApp::detectRainGesture() {
	// Only detect rain when Kinect is running and calibrated
	if (!kinectProjector->isKinectConnected() ||
	    kinectProjector->GetApplicationState() != KinectProjector::APPLICATION_STATE_RUNNING)
	{
		return;
	}

	// Threshold-based rain detection: scan a coarse grid of the depth image.
	// Pixels with elevation > rainThreshold (mm above base plane) are treated
	// as a hand — add water at those kinect-space coordinates.
	const float rainThreshold = 50.0f; // mm above base plane = hand
	const int step = 8; // sample every 8th pixel (80×60 grid)

	ofVec2f kinectRes = kinectProjector->getKinectRes();
	ofRectangle roi = kinectProjector->getKinectROI();
	if (roi.width <= 0 || roi.height <= 0) return;

	int startX = (int)roi.x;
	int startY = (int)roi.y;
	int endX = (int)(roi.x + roi.width);
	int endY = (int)(roi.y + roi.height);

	for (int y = startY; y < endY; y += step) {
		for (int x = startX; x < endX; x += step) {
			float elev = kinectProjector->elevationAtKinectCoord(x, y);
			if (elev > rainThreshold) {
				// Map kinect pixel to water sim grid coordinates
				float simX = ((float)(x - startX) / roi.width) * waterSimGetSimWidth();
				float simY = ((float)(y - startY) / roi.height) * waterSimGetSimHeight();
				waterSimAddWater(simX, simY, 15.0f, 0.3f);
			}
		}
	}
}

// ─── Unified water simulation accessors ─────────────────────────────

void ofApp::waterSimSetup(int w, int h) {
	// Auto-detect compute shader support (requires GL 4.3+)
	useComputeWaterSim = ComputeWaterSimulation::isComputeSupported();

	if (useComputeWaterSim) {
		ofLogNotice("ofApp") << "GL 4.3+ detected — using COMPUTE SHADER water simulation";
		waterSimCompute.setup(w, h);
		// setup() can fail (e.g. a compute shader fails to compile/link). Don't
		// silently run a no-op water sim — fall back to the fragment path.
		if (!waterSimCompute.isInitialized()) {
			ofLogError("ofApp") << "Compute water simulation failed to initialize — falling back to FRAGMENT SHADER water simulation";
			useComputeWaterSim = false;
			waterSimFragment.setup(w, h);
		}
	} else {
		ofLogNotice("ofApp") << "GL < 4.3 — using FRAGMENT SHADER water simulation (fallback)";
		waterSimFragment.setup(w, h);
	}
}

void ofApp::waterSimUpdate(ofTexture& depthTex, float dt) {
	if (useComputeWaterSim) waterSimCompute.update(depthTex, dt);
	else waterSimFragment.update(depthTex, dt);
}

void ofApp::waterSimDraw(float w, float h) {
	if (useComputeWaterSim) waterSimCompute.draw(w, h);
	else waterSimFragment.draw(w, h);
}

void ofApp::waterSimAddWater(float x, float y, float radius, float amount) {
	if (useComputeWaterSim) waterSimCompute.addWater(x, y, radius, amount);
	else waterSimFragment.addWater(x, y, radius, amount);
}

bool ofApp::waterSimIsEnabled() const {
	if (useComputeWaterSim) return waterSimCompute.isEnabled();
	else return waterSimFragment.isEnabled();
}

void ofApp::waterSimToggleEnabled() {
	if (useComputeWaterSim) waterSimCompute.setEnabled(!waterSimCompute.isEnabled());
	else waterSimFragment.setEnabled(!waterSimFragment.isEnabled());
}

int ofApp::waterSimGetSimWidth() const {
	if (useComputeWaterSim) return waterSimCompute.getSimWidth();
	else return waterSimFragment.getSimWidth();
}

int ofApp::waterSimGetSimHeight() const {
	if (useComputeWaterSim) return waterSimCompute.getSimHeight();
	else return waterSimFragment.getSimHeight();
}

void ofApp::waterSimSetLavaMode(bool enabled) {
	if (useComputeWaterSim) {
		waterSimCompute.setFluidType(enabled
			? ComputeWaterSimulation::FLUID_LAVA
			: ComputeWaterSimulation::FLUID_WATER);
	} else {
		waterSimFragment.setLavaMode(enabled);
	}
}

bool ofApp::waterSimIsLavaMode() const {
	if (useComputeWaterSim) return waterSimCompute.getFluidType() == ComputeWaterSimulation::FLUID_LAVA;
	else return waterSimFragment.isLavaMode();
}
