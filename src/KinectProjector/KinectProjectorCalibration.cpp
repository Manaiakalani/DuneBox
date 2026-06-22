/***********************************************************************
KinectProjectorCalibration.cpp - KinectProjectorCalibration compute
the calibration of the kinect and projector.
Copyright (c) 2016 Thomas Wolf

--- Adapted from ofxKinectProjectorToolkit by Gene Kogan:
https://github.com/genekogan/ofxKinectProjectorToolkit
Copyright (c) 2014 Gene Kogan

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
with the Magic Sand; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
***********************************************************************/

#include "KinectProjectorCalibration.h"

ofxKinectProjectorToolkit::ofxKinectProjectorToolkit(ofVec2f sprojRes, ofVec2f skinectRes) {
	projRes = sprojRes;
	kinectRes = skinectRes;
    calibrated = false;
}

void ofxKinectProjectorToolkit::calibrate(vector<ofVec3f> pairsKinect,
                                          vector<ofVec2f> pairsProjector) {
    int nPairs = pairsKinect.size();
    A.set_size(nPairs*2, 11);
    y.set_size(nPairs*2, 1);
    
    for (int i=0; i<nPairs; i++) {
        A(2*i, 0) = pairsKinect[i].x;
        A(2*i, 1) = pairsKinect[i].y;
        A(2*i, 2) = pairsKinect[i].z;
        A(2*i, 3) = 1;
        A(2*i, 4) = 0;
        A(2*i, 5) = 0;
        A(2*i, 6) = 0;
        A(2*i, 7) = 0;
        A(2*i, 8) = -pairsKinect[i].x * pairsProjector[i].x;
        A(2*i, 9) = -pairsKinect[i].y * pairsProjector[i].x;
        A(2*i, 10) = -pairsKinect[i].z * pairsProjector[i].x;
        
        A(2*i+1, 0) = 0;
        A(2*i+1, 1) = 0;
        A(2*i+1, 2) = 0;
        A(2*i+1, 3) = 0;
        A(2*i+1, 4) = pairsKinect[i].x;
        A(2*i+1, 5) = pairsKinect[i].y;
        A(2*i+1, 6) = pairsKinect[i].z;
        A(2*i+1, 7) = 1;
        A(2*i+1, 8) = -pairsKinect[i].x * pairsProjector[i].y;
        A(2*i+1, 9) = -pairsKinect[i].y * pairsProjector[i].y;
        A(2*i+1, 10) = -pairsKinect[i].z * pairsProjector[i].y;
        
        y(2*i, 0) = pairsProjector[i].x;
        y(2*i+1, 0) = pairsProjector[i].y;
    }
    
    dlib::qr_decomposition<dlib::matrix<double, 0, 11> > qrd(A);
    x = qrd.solve(y);
    cout << "x: "<< x << endl;
    projMatrice = ofMatrix4x4(x(0,0), x(1,0), x(2,0), x(3,0),
                              x(4,0), x(5,0), x(6,0), x(7,0),
                              x(8,0), x(9,0), x(10,0), 1,
                              0, 0, 0, 1);
    calibrated = true;
}

ofMatrix4x4 ofxKinectProjectorToolkit::getProjectionMatrix() {
    return projMatrice;
}

ofVec2f ofxKinectProjectorToolkit::getProjectedPoint(ofVec3f worldPoint) {
    ofVec4f pts = ofVec4f(worldPoint);
    pts.w = 1;
    ofVec4f rst = projMatrice*(pts);
    ofVec2f projectedPoint(rst.x/rst.z, rst.y/rst.z);
    return projectedPoint;
}

vector<double> ofxKinectProjectorToolkit::getCalibration()
{
    vector<double> coefficients;
    for (int i=0; i<11; i++) {
        coefficients.push_back(x(i, 0));
    }
    return coefficients;
}

bool ofxKinectProjectorToolkit::loadCalibration(string path){
    ofXml xml;
    if (!xml.load(path)) {
        ofLogWarning("ofxKinectProjectorToolkit")
            << "loadCalibration(): no calibration file at '" << path
            << "'. Run 'Automatically calibrate kinect & projector' in the GUI to create one.";
        return false;
    }
	auto calibration = xml.getChild("CALIBRATION");
	auto resolutions = calibration.getChild("RESOLUTIONS");
	ofVec2f sprojRes = resolutions.getChild("PROJECTOR").getValue<ofVec2f>();
	ofVec2f skinectRes = resolutions.getChild("KINECT").getValue<ofVec2f>();
	if (sprojRes!=projRes || skinectRes!=kinectRes) {
		ofLogWarning("ofxKinectProjectorToolkit")
			<< "loadCalibration(): '" << path << "' was calibrated for projector "
			<< sprojRes << " / kinect " << skinectRes << ", but this rig is projector "
			<< projRes << " / kinect " << kinectRes
			<< ". Ignoring the stale calibration - re-run 'Automatically calibrate kinect & projector'.";
		return false;
	}
    auto coefficients = calibration.getChild("COEFFICIENTS");
    for (int i=0; i<11; i++) {
        x(i, 0) = coefficients.getChild("COEFF"+ofToString(i)).getFloatValue();
    }
    // Use the same homogeneous bottom row (0,0,0,1) as the live calibrate()
    // path above. The two construction sites must stay identical so a loaded
    // calibration behaves exactly like a freshly-computed one.
    projMatrice = ofMatrix4x4(x(0,0), x(1,0), x(2,0), x(3,0),
                              x(4,0), x(5,0), x(6,0), x(7,0),
                              x(8,0), x(9,0), x(10,0), 1,
                              0, 0, 0, 1);
    calibrated = true;
    return true;
}

bool ofxKinectProjectorToolkit::saveCalibration(string path){
    ofXml xml;
	auto calibration = xml.appendChild("CALIBRATION");
	auto resolutions = calibration.appendChild("RESOLUTIONS");
	resolutions.appendChild("PROJECTOR").set(projRes);
	resolutions.appendChild("KINECT").set(kinectRes);
	auto coefficients = calibration.appendChild("COEFFICIENTS");
	for (int i=0; i<11; i++) {
        coefficients.appendChild("COEFF"+ofToString(i)).set(x(i, 0));
    }
    return xml.save(path);
}


