/***********************************************************************
KinectGrabber - KinectGrabber takes care of the communication with
the kinect and the filtering of depth frame.
Copyright (c) 2016 Thomas Wolf

--- Adapted from FrameFilter of the Augmented Reality Sandbox
Copyright (c) 2012-2015 Oliver Kreylos

This file is part of the Magic Sand.

The Magic Sand is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the
License, or (at your option) any later version.

The Magic Sand is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.
***********************************************************************/

#pragma once

// --- DuneBox: force-enable the Kinect for Windows v2 backend on Windows ---
// The CI Windows build always vendors the Kinect for Windows SDK 2.0 and the
// ofxKinectForWindows2 addon. Defining the flag directly in source (instead of
// relying on the build system to inject /D into every translation unit, which
// proved unreliable with the OpenFrameworks VS project) guarantees the v2 code
// path is actually compiled in. macOS/Linux builds do not ship the addon, so the
// flag stays off there. Define DUNEBOX_NO_KINECT_V2 to opt out on Windows.
#if defined(_WIN32) && !defined(DUNEBOX_NO_KINECT_V2)
#  ifndef DUNEBOX_USE_KINECT_FOR_WINDOWS2
#    define DUNEBOX_USE_KINECT_FOR_WINDOWS2 1
#  endif
#endif

#include "ofMain.h"
#include "ofxOpenCv.h"
#include "ofxCv.h"
#include "ofxKinect.h"

#ifdef DUNEBOX_USE_KINECT_FOR_WINDOWS2
// Kinect for Windows v2 (official Microsoft SDK) backend. Kinect.h pulls in
// <windows.h>, which by default includes the legacy <winsock.h>; make sure the
// modern <winsock2.h> is seen first so it does not clash with the rest of the
// app's networking headers (Bridge uses winsock2).
#ifdef TARGET_WIN32
#include <winsock2.h>
#endif
#include "ofxKinectForWindows2.h"
#endif

#include "Utils.h"

class KinectGrabber: public ofThread {
public:
	typedef unsigned short RawDepth; // Data type for raw depth values
	typedef float FilteredDepth; // Data type for filtered depth values

	KinectGrabber();
	~KinectGrabber();
    void start();
    void stop();
    void performInThread(std::function<void(KinectGrabber&)> action);
    bool setup();
	bool openKinect();
	void setupFramefilter(int gradFieldresolution, float newMaxOffset, ofRectangle ROI, bool spatialFilter, bool followBigChange, int numAveragingSlots);
    void initiateBuffers(void); // Reinitialise buffers
    void resetBuffers(void);
    
    ofVec3f getStatBuffer(int x, int y);
    float getAveragingBuffer(int x, int y, int slotNum);
    float getValidBuffer(int x, int y);
    
    void setFollowBigChange(bool newfollowBigChange);
    void setKinectROI(ofRectangle skinectROI);
    void setAveragingSlotsNumber(int snumAveragingSlots);
    void setGradFieldResolution(int sgradFieldresolution);
    
    void decStoredframes(){
        storedframes -= 1;
    }
    
    bool isImageStabilized(){
        return firstImageReady;
    }
    
    bool isFrameNew(){
        return newFrame;
    }
    
    ofVec2f getKinectSize(){
        return ofVec2f(width, height);
    }

    // Select the depth sensor backend before calling setup(): 1 = Kinect v1
    // (ofxKinect), 2 = Kinect v2 (ofxKinectForWindows2). v2 only takes effect
    // when the binary is compiled with DUNEBOX_USE_KINECT_FOR_WINDOWS2.
    void setKinectVersion(int v){
        kinectVersion = v;
    }
    
    float getRawDepthAt(int x, int y){
        return kinectDepthImage.getData()[(int)(y*width+x)];
    }
    
	ofMatrix4x4 getWorldMatrix();
    
    int getNumAveragingSlots(){
        return numAveragingSlots;
    }
    
    void setMaxOffset(float newMaxOffset){
        maxOffset = newMaxOffset;
    }
    
    void setSpatialFiltering(bool newspatialFilter){
        spatialFilter = newspatialFilter;
    }
    
	void setInPainting(bool inp)
	{
		doInPaint = inp;
	}

	// Should the entire frame be filtered and thereby ignoring the KinectROI
	void setFullFrameFiltering(bool ff, ofRectangle ROI);

	ofThreadChannel<ofFloatPixels> filtered;
	ofThreadChannel<ofPixels> colored;
	ofThreadChannel<ofVec2f*> gradient;
    
private:
	void threadedFunction() override;
    void filter();
    bool isInsideROI(int x, int y); // test is x, y is inside ROI
    void applySpaceFilter();
    void updateGradientField();
    
	// A simple inpainting algorithm to remove outliers in the depth
	// Since the shader has no way of filtering outliers (0 and 4000 values mainly) it creates visual artifacts if they are not 
	// removed prior to the shader pass
	void applySimpleOutlierInpainting();
	float findInpaintValue(float *data, int x, int y);
	double ROIAverageValue = 0;
	int setToLocalAvg = 0;
	int setToGlobalAvg = 0;


	bool newFrame;
    bool bufferInitiated;
    bool firstImageReady;
    int storedframes;
    
    // Thread lambda functions (actions)
	vector<std::function<void(KinectGrabber&)> > actions;
	ofMutex actionsLock;
    
    // Kinect parameters
	bool kinectOpened;
    ofxKinect               kinect;
    int kinectVersion = 1; // 1 = Kinect v1 (ofxKinect); 2 = Kinect v2 (ofxKinectForWindows2)
#ifdef DUNEBOX_USE_KINECT_FOR_WINDOWS2
    ofxKFW2::Device                         kinectV2Device;
    std::shared_ptr<ofxKFW2::Source::Depth> kinectV2Depth;
    std::shared_ptr<ofxKFW2::Source::Color> kinectV2Color;
    void updateKinectV2ColorInDepthFrame();
#endif
    unsigned int width, height; // Width and height of kinect frames
	int minX, maxX; // , ROIwidth; // ROI definition
	int minY, maxY; //, ROIheight;
    
    // General buffers
    ofxCvColorImage         kinectColorImage;
    ofShortPixels     kinectDepthImage;
    ofFloatPixels filteredframe;
    ofVec2f* gradField;
    
    // Filtering buffers
	float* averagingBuffer; // Buffer to calculate running averages of each pixel's depth value
	float* statBuffer; // Buffer retaining the running means and variances of each pixel's depth value
	float* validBuffer; // Buffer holding the most recent stable depth value for each pixel
    
    // Gradient computation variables
    int gradFieldcols, gradFieldrows;
    int gradFieldresolution;           //Resolution of grid relative to window width and height in pixels
    float maxgradfield, depthrange;
    
    // Frame filter parameters
	int numAveragingSlots; // Number of slots in each pixel's averaging buffer
	int averagingSlotIndex; // Index of averaging slot in which to store the next frame's depth values
	unsigned int minNumSamples; // Minimum number of valid samples needed to consider a pixel stable
	float maxVariance; // Maximum variance to consider a pixel stable
    float initialValue;
 //   float outsideROIValue;
	float hysteresis; // Amount by which a new filtered value has to differ from the current value to update the display
    bool followBigChange;
    float bigChange; // Amount of change over which the averaging slot is reset to new value
//	float instableValue; // Value to assign to instable pixels if retainValids is false
	bool spatialFilter; // Flag whether to apply a spatial filter to time-averaged depth values
    float maxOffset;
    
    int minInitFrame; // Minimal number of frame to consider the kinect initialized
    int currentInitFrame;

	bool doInPaint;

	bool doFullFrameFiltering;
    // Debug
//    int blockX, blockY;
};
