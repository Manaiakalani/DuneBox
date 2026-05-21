/***********************************************************************
Main.cpp
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


#include "ofMain.h"
#include "ofApp.h"

const std::string MagicSandVersion = "1.5.4.2";

bool setWindowDimensions(ofGLFWWindowSettings& settings, int windowsNum) {
	int count;
	GLFWmonitor** monitors = glfwGetMonitors(&count);
	cout << "Number of screens found: " << count << endl;
	if (count > windowsNum) {
		int xM; int yM;
		glfwGetMonitorPos(monitors[windowsNum], &xM, &yM); // We take the first monitor
		const GLFWvidmode * desktopMode = glfwGetVideoMode(monitors[windowsNum]);

        cout << "Monitor " << windowsNum << " size: " << desktopMode->width << "x" << desktopMode->height << endl;
		
		if (windowsNum == 0)
		{
			// Make main window almost full screen - but just a bit of space around to be able to grab other windows
			settings.width = desktopMode->width * 4.0 / 5.0;
			settings.height = desktopMode->height * 4.0 / 5.0;
		}
		else
		{
			// Projector window full screen
			settings.width = desktopMode->width;
			settings.height = desktopMode->height;
		}

		settings.setPosition(ofVec2f(xM, yM));

		return true;
	}
	else {
		settings.width = 1600; // Default settings
		settings.height = 800;
		settings.setPosition(ofVec2f(0, 0));
		return false;
	}

}

//========================================================================
int main() {
	ofGLFWWindowSettings settings;

	// Try GL 4.3 first (needed for compute shaders in water simulation).
	// If the driver/hardware doesn't support it, fall back to GL 3.2.
	bool useGL43 = true;
	settings.setGLVersion(4, 3);
	settings.width = 1600;
	settings.height = 800;
	settings.setPosition(ofVec2f(0, 0));
	settings.resizable = true;
	settings.decorated = true;
	settings.title = "Magic-Sand " + MagicSandVersion;

	shared_ptr<ofAppBaseWindow> mainWindow;
	try {
		mainWindow = ofCreateWindow(settings);
	} catch (...) {
		mainWindow = nullptr;
	}

	if (!mainWindow || mainWindow->getWindowSize().x == 0) {
		// GL 4.3 failed — fall back to GL 3.2
		ofLogNotice("main") << "GL 4.3 not available, falling back to GL 3.2";
		useGL43 = false;
		settings.setGLVersion(3, 2);
		mainWindow = ofCreateWindow(settings);
	} else {
		ofLogNotice("main") << "GL 4.3 context created — compute shaders available";
	}
    
	setWindowDimensions(settings, 0);
	mainWindow->setWindowPosition(ofGetScreenWidth() / 2 - settings.width / 2, ofGetScreenHeight() / 2 - settings.height / 2);
    mainWindow->setWindowShape(settings.width, settings.height);
    
	setWindowDimensions(settings, 1);
	settings.resizable = false;
	settings.decorated = false;
	settings.shareContextWith = mainWindow;
	shared_ptr<ofAppBaseWindow> secondWindow = ofCreateWindow(settings);
	secondWindow->setVerticalSync(false);

	shared_ptr<ofApp> mainApp(new ofApp);
	ofAddListener(secondWindow->events().draw, mainApp.get(), &ofApp::drawProjWindow);
	mainApp->projWindow = secondWindow;
		
	ofRunApp(mainWindow, mainApp);
	ofRunMainLoop();
}
