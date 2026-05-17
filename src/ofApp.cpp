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
	waterSim.setup(waterW, waterH);

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

	// --- Water simulation update ---
	if (waterSim.isEnabled()) {
		// Determine depth texture source
		if (kinectProjector->isKinectConnected() &&
		    kinectProjector->GetApplicationState() == KinectProjector::APPLICATION_STATE_RUNNING)
		{
			// Use live Kinect depth data
			useTestTerrain = false;
			ofTexture& depthTex = kinectProjector->getTexture();
			waterSim.update(depthTex, ofGetLastFrameTime());
		}
		else
		{
			// No Kinect — use procedural test terrain
			useTestTerrain = true;
			generateTestTerrain();
			ofTexture& testTex = testTerrainFbo.getTexture();
			waterSim.update(testTex, ofGetLastFrameTime());
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
}

void ofApp::drawProjWindow(ofEventArgs &args) 
{
	if (kinectProjector->GetApplicationState() == KinectProjector::APPLICATION_STATE_RUNNING)
	{
		sandSurfaceRenderer->drawProjectorWindow();

		// Water overlay — composited on top of terrain, under game layers
		if (waterSim.isEnabled()) {
			float projW = projWindow->getWidth();
			float projH = projWindow->getHeight();
			waterSim.draw(projW, projH);
		}

		mapGameController.drawProjectorWindow();
		boidGameController.drawProjectorWindow();
	}
	else if (useTestTerrain && waterSim.isEnabled())
	{
		// No Kinect running — still show water on test terrain
		float projW = projWindow->getWidth();
		float projH = projWindow->getHeight();
		ofSetColor(40, 30, 20); // Dark brown background for test terrain
		ofDrawRectangle(0, 0, projW, projH);
		ofSetColor(255);
		waterSim.draw(projW, projH);
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
		mapGameController.setDebug(kinectProjector->getDumpDebugFiles());
		mapGameController.RealTimeTestMe();
	}
	else if (key == 'w')
	{
		// Toggle water simulation
		waterSim.setEnabled(!waterSim.isEnabled());
		ofLogNotice("ofApp") << "Water simulation: " << (waterSim.isEnabled() ? "ON" : "OFF");
		cout << "Water simulation: " << (waterSim.isEnabled() ? "ON" : "OFF") << endl;
	}
	else if (key == 'W')
	{
		// Original debug test (moved from 'w')
		mapGameController.setDebug(kinectProjector->getDumpDebugFiles());
		mapGameController.DebugTestMe();
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
	int startX = (int)roi.x;
	int startY = (int)roi.y;
	int endX = (int)(roi.x + roi.width);
	int endY = (int)(roi.y + roi.height);

	for (int y = startY; y < endY; y += step) {
		for (int x = startX; x < endX; x += step) {
			float elev = kinectProjector->elevationAtKinectCoord(x, y);
			if (elev > rainThreshold) {
				// Map kinect pixel to water sim grid coordinates
				float simX = ((float)(x - startX) / roi.width) * waterSim.getSimWidth();
				float simY = ((float)(y - startY) / roi.height) * waterSim.getSimHeight();
				waterSim.addWater(simX, simY, 15.0f, 0.3f);
			}
		}
	}
}

