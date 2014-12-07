#include "ShapeDetector.h"
//--------------------------------------------------------------
void ShapeDetector::setup(){

	//Initialize the sensors with a depth and color image channel
	kinect.initSensor();
	kinect.initDepthStream(true);
	kinect.initColorStream();
	kinect.start();
	
	currentSelectedContour = -1;

	depthImageWidth  = kinect.getDepthPixelsRef().getWidth(); 
	depthImageHeight = kinect.getDepthPixelsRef().getHeight();

	zoomFbo.allocate(depthImageWidth,depthImageHeight,GL_RGB);
	depthColors.allocate(depthImageWidth, depthImageHeight,OF_IMAGE_COLOR);

	gui = new ofxUISuperCanvas("SHAPEGUI", 200, 0, 200, 700);
	gui->addLabel("FILTERS");
	gui->addSlider("MIN AREA", 0, 100, &minArea);
	gui->addSlider("MAX AREA", 0, 500, &maxArea);
	gui->addSlider("MIN COMPACTNESS", 0, 1.0, &minCompactness);

	gui->addLabel("PREVIEW OPTIONS");
	gui->addToggle("PREVIEW ELLIPSE",&previewEllipseFit);
	gui->addToggle("PREVIEW RECT",&previewRectFit);
	gui->addToggle("PREVIEW CIRCLE", &previewCircleFit);
	gui->addToggle("PREVIEW STATS", &previewStats);

	gui->addLabel("SEGMENTATION PARAMETERS");
	gui->addSlider("SIGMA", 0, 2.0, &imageSegmentation.sigma);
	gui->addSlider("K", 0, 500, &imageSegmentation.k);

	gui->loadSettings(getSettingsFilename());
}

//--------------------------------------------------------------
void ShapeDetector::update(){
	kinect.update();
	if(kinect.isFrameNew()){
		kinect.mapDepthToColor(depthColors.getPixelsRef());
		depthColors.update();
	}
}

//--------------------------------------------------------------
void ShapeDetector::revalidateContours(){
	validContours.clear();
	for(int i = 0; i < contours.size(); i++){
		contours[i].valid = 
			contours[i].segmentedColorImage.isAllocated() &&
			contours[i].segmentedDepthImage.isAllocated() &&
			contours[i].contourArea > minArea && contours[i].contourArea < maxArea  &&
			contours[i].compactness > minCompactness;

		//update the list of valid contours
		if(contours[i].valid){
			validContours.push_back(i);
		}
	}
	
	if(currentSelectedContour >= validContours.size()){
		currentSelectedContour  = validContours.size()-1;
	}
}

//--------------------------------------------------------------
void ShapeDetector::draw(){
	drawDebug(false);
	drawDebug(true);

	zoomFbo.draw(depthImageWidth,0);
	segmentedDepthColors.draw(depthImageWidth,depthImageWidth);
}

void ShapeDetector::drawDebug(bool zoom){

	if(zoom){
		zoomFbo.begin();
		ofClear(0,0,0);
		ofPushMatrix();
		ofScale(5,5);
		ofTranslate(-zoomPoint.x, -zoomPoint.y);
	}
	
	if(depthColors.isAllocated()){ 
		depthColors.draw(0,0);
	}

	if(showAllContours){
		for(int i = 0; i < contours.size(); i++){
			if(contours[i].valid) 
				drawContour(contours[i], previewStats && zoom);
		}
	}
	else{
		if(currentSelectedContour > 0 && currentSelectedContour < contours.size()){
			drawContour(contours[currentSelectedContour], previewStats);
		}
	}

	if(zoom){
		ofPopMatrix();
		zoomFbo.end();
	}
}

void ShapeDetector::drawContour(ShapeContour& contour, bool showStats){

	ofPushStyle();
	ofSetColor(ofColor::blueSteel);

	contour.contour.draw();	
	
	if(previewCircleFit){
		ofNoFill();
		ofSetColor(0,0,255);
		ofCircle(contour.circlePosition,contour.circleRadius);
	}

	if(previewEllipseFit){
		ofSetColor(255,0,255);
		ofMesh m;
		cv::Point2f rectpoints[4];
		contour.fitEllipse.points(rectpoints);
		m.addVertex( ofxCv::toOf(rectpoints[0]) );
		m.addVertex( ofxCv::toOf(rectpoints[1]) );
		m.addVertex( ofxCv::toOf(rectpoints[2]) );
		m.addVertex( ofxCv::toOf(rectpoints[3]) );
		m.setMode(OF_PRIMITIVE_LINE_LOOP);
		m.draw();
	}
				
	if(previewRectFit){
		ofSetColor(0,100,255);
		ofMesh m;
		cv::Point2f rectpoints[4];
		contour.fitRect.points(rectpoints);
		m.addVertex( ofxCv::toOf(rectpoints[0]) );
		m.addVertex( ofxCv::toOf(rectpoints[1]) );
		m.addVertex( ofxCv::toOf(rectpoints[2]) );
		m.addVertex( ofxCv::toOf(rectpoints[3]) );
		m.setMode(OF_PRIMITIVE_LINE_LOOP);
		m.draw();
	}

	if(showStats){
		string debugString = "TEST";
		/*
		stringstream posstr;
		posstr << contour.shape.position;
		string debugString = contour.shape.getDescription() +
							"\nLEVEL:  " + ofToString(contours[i].level) +
							"\nDEPTH:  " + ofToString(contour.depthPosition) +
							"\nPOS:    " + ofToString(posstr.str()) +
							"\nPERC ON: " + ofToString(contour.shape.onDepthRatio, 2) +
							"\nRADIUS:  " + ofToString(contour.coordRadius,4) +
							//"\nRECTSIDE:\t" + ofToString(contour.rectMaxSide,4) +
							"\nBOXY:   "+ ofToString(contour.shape.boxiness,4);
							*/
		ofSetColor(0);
		ofDrawBitmapString(debugString, contour.circlePosition + ofVec2f(contour.circleRadius,contour.circleRadius));
		ofSetColor(255);
		ofDrawBitmapString(debugString, contour.circlePosition + ofVec2f(contour.circleRadius-.2,contour.circleRadius+.2));
	}
	ofPopStyle();
}

void ShapeDetector::findShapes(){

	imageSegmentation.min = minArea;
	imageSegmentation.segment(depthColors);
	segmentedDepthColors.setFromPixels(imageSegmentation.getSegmentedPixels());
	segmentedDepthColors.update();

	contours.clear();
	contours.resize(imageSegmentation.numSegments);

	for(int segment = 0; segment < imageSegmentation.numSegments; segment++){

		ShapeContour& contour = contours[segment];

		ofxCv::ContourFinder contourFinder;
		cv::Point2f minCircleCenter;
		float minCircleRadius;
		contourFinder.setAutoThreshold(false);
		contourFinder.setMinArea(minArea);
		contourFinder.setMaxArea(maxArea);
		contourFinder.findContours(ofxCv::toCv(imageSegmentation.getSegmentMask(segment)));

		if(contourFinder.getContours().size() == 0){
			ofLogError("ShapeDetector::findShapes") << "No contours in segment";
			continue;
		}

		if(contourFinder.getContours().size() > 1){
			ofLogError("ShapeDetector::findShapes") << "Multiple contours in segment. only looking at first";
		}
		
		contour.segmentedColorImage.allocate(depthImageWidth,depthImageHeight,OF_IMAGE_COLOR);
		ofxCv::toCv(depthColors).copyTo( ofxCv::toCv(contour.segmentedColorImage), ofxCv::toCv(imageSegmentation.getSegmentMask(segment)) ); 
		contour.segmentedColorImage.update();

		contour.segmentedDepthImage.allocate(depthImageWidth,depthImageHeight,OF_IMAGE_GRAYSCALE);
		ofxCv::toCv(kinect.getRawDepthPixelsRef()).copyTo( ofxCv::toCv(contour.segmentedDepthImage), ofxCv::toCv(imageSegmentation.getSegmentMask(segment)) ); 
		contour.segmentedDepthImage.update();

		contour.contour = contourFinder.getPolyline(0);
		contour.contourArea = contourFinder.getContourArea(0);
		contour.boundingRect = contourFinder.getBoundingRect(0);
				
		//what is the min circle we can draw around the contour?
		cv::minEnclosingCircle(cv::Mat(contourFinder.getContour(0)),minCircleCenter,minCircleRadius);
		contour.circlePosition = ofxCv::toOf(minCircleCenter);
		contour.circleRadius = minCircleRadius;
		contour.fitRect = cv::minAreaRect( cv::Mat(contourFinder.getContour(0)) );
		//contour.fitRect.size.width  *= .85;
		//contour.fitRect.size.height *= .85;
		
		contour.rectMaxSide = MAX(contour.fitRect.size.width,contour.fitRect.size.height);

		//fit an elipse to the contour to compare the to the actual edge
		if(contourFinder.getContour(0).size() > 5){
			contour.fitEllipse = cv::fitEllipse( cv::Mat(contourFinder.getContour(0)) );
		}
		//TODO: consider "compactness"
	}

	revalidateContours();

	////////////////////////////////////////////////////////////////////
	/*
	//FIND SHAPES
	for(int shapeColor = 0; shapeColor < SHAPE_COLOR_COUNT; shapeColor++){
 
		colorDepthCompositeMask[shapeColor].getPixelsRef().set(0);

		//copy in the colors over the back scene
		ofxCv::toCv(kinect.getRawDepthPixelsRef()).copyTo(ofxCv::toCv(colorDepthCompositeMask[shapeColor]), ofxCv::toCv(colorMasks[shapeColor]));		
		//getPixelRange(colorDepthCompositeMask[ShapeColor].getPixelsRef(), minScanDistance, maxScanDistance, curPix);
		ofxCv::toCv(colorDepthCompositeMask[shapeColor]).convertTo(ofxCv::toCv(contourPix), CV_8U);

		colorDepthCompositeMask[shapeColor].update();

		ofxCv::ContourFinder contourFinder;
		cv::Point2f minCircleCenter;
		float minCircleRadius;

		contourFinder.setAutoThreshold(false);
		contourFinder.setMinArea(minArea);

		contourFinder.findContours(contourPix);
		for(int i = 0; i < contourFinder.getContours().size(); i++){
			ShapeContour contour;
			contour.color = shapeColor;

			contour.contour = contourFinder.getPolyline(i);
			contour.contourArea = contourFinder.getContourArea(i);
			contour.boundingRect = contourFinder.getBoundingRect(i);
				
			//what is the min circle we can draw around the contour?
			cv::minEnclosingCircle(cv::Mat(contourFinder.getContour(i)),minCircleCenter,minCircleRadius);
			contour.circlePosition = ofxCv::toOf(minCircleCenter);
			contour.circleRadius = minCircleRadius * .85;
			contour.fitRect = cv::minAreaRect( cv::Mat(contourFinder.getContour(i)) );
			contour.fitRect.size.width  *= .85;
			contour.fitRect.size.height *= .85;
				
			contour.rectMaxSide = MAX(contour.fitRect.size.width,contour.fitRect.size.height);

			//fit an elipse to the contour to compare the to the actual edge
			if(contourFinder.getContour(i).size() > 5){
				contour.fitEllipse = cv::fitEllipse( cv::Mat(contourFinder.getContour(i)) );
			}

			//TODO: consider "compactness"
			contours.push_back(contour);
		}
	}
	*/

	cout << "FOUND " << contours.size() << " CONTOURS" << endl;

	/*
	//analyze them!
	for(int shapeColor = 0; shapeColor < SHAPE_COLOR_COUNT; shapeColor++){
		for(int c = 0; c < contours.size(); c++){
			ShapeContour& contour = contours[c];
			if(contour.color == shapeColor){

				contour.shape.setTypeFromColor();

				//determine the error between the fit circle and the contour
				float averageValidDepth = 0;
				int validSamples = 0;
				int numSamples = 0;
				
				if(contour.shape.type == CURIO_SPHERE){
					for(int p = 0; p < contour.contour.size(); p++){
						contour.shape.fitError += abs(contour.contour.getVertices()[p].distance(contour.circlePosition) - contour.circleRadius);
					}

					//determine how much of the fit circle lies on depth, and what it's approx depth is
					for(float f = 0; f < 360; f += 1){
						ofVec2f point = contour.circlePosition + ofVec2f(0,1).getRotated(f)*contour.circleRadius;
						int depthPositionIndex = scanScene.getPixelIndex(int(point.x),int(point.y));
						float depthPosition = scanScene.getPixels()[depthPositionIndex];
						if(depthPosition >= contour.depthRangeMin && depthPosition <= contour.depthRangeMax){
							validSamples++;
							averageValidDepth += depthPosition;
						}
						numSamples++;
					}
					float shortSide = MIN(contour.fitEllipse.size.width,contour.fitEllipse.size.height);
					float longSide  = MAX(contour.fitEllipse.size.width,contour.fitEllipse.size.height);
					contour.shape.boxiness = shortSide/longSide;
				}
				else{

					//for(int p = 0; p < contour.contour.size(); p++){
						//TODO:: FIT ERROR BASED ON CONTOUR DISTANCE FROM POLY
						//contour.shape.fitError += abs(contour.contour.getVertices()[p].distance(contour.circlePosition) - contour.circleRadius);
					//}

					cv::Point2f rectpoints[4];
					contour.fitRect.points(rectpoints);
					
					for(int p = 0; p < 4; p++){
						int pp = (p+1) % 4;
						ofVec2f thisP = ofxCv::toOf(rectpoints[p]);
						ofVec2f nextP = ofxCv::toOf(rectpoints[pp]);
						for(float step = 0; step < 1.0; step += .1){
							ofVec2f point = thisP.getInterpolated(nextP,step);
							int depthPositionIndex = scanScene.getPixelIndex(int(point.x),int(point.y));
							float depthPosition = scanScene.getPixels()[depthPositionIndex];
							if(depthPosition >= contour.depthRangeMin && depthPosition <= contour.depthRangeMax){
								validSamples++;
								averageValidDepth += depthPosition;
							}
							numSamples++;
						}
					}

					float shortSide = MIN(contour.fitRect.size.width,contour.fitRect.size.height);
					float longSide  = MAX(contour.fitRect.size.width,contour.fitRect.size.height);
					contour.shape.boxiness = shortSide/longSide;

				}

				//WALK AROUND CONTOUR
				vector<pair<float,ofVec2f> > samples;
				ofRectangle boundingrect = ofxCv::toOf(contour.boundingRect);
				for(int i = 0; i< contour.contour.getVertices().size(); i++){
					int x = contour.contour.getVertices()[i].x;
					int y = contour.contour.getVertices()[i].y;
					int depthPositionIndex = scanScene.getPixelIndex(x,y);
					float depthPosition = scanScene.getPixels()[depthPositionIndex];
					samples.push_back(make_pair( depthPosition, contour.contour.getVertices()[i] ));
				}

				sort(samples.begin(), samples.end(), samplesort);
				int indx = samples.size() * .5; //filter out the outliers
				float medianDepth = samples[indx].first;
				//int minX = samples[indx].second.x;
				//int minY = samples[indx].second.y;
				//compute its physical size and determine what max size sphere would fit in this radius
				//if(validSamples > 0){
					averageValidDepth /= validSamples;
					//averageValidDepth  *= .1;
					vector<ofPoint> depthPoints;
					if(contour.shape.type == CURIO_SPHERE){
						//depthPoints.push_back(contour.circlePosition);
						depthPoints.push_back(contour.circlePosition + ofVec2f(0,contour.circleRadius));
						depthPoints.push_back(contour.circlePosition - ofVec2f(0,contour.circleRadius));
						depthPoints.push_back(contour.circlePosition);
					}
					else{
						ofVec2f center = ofxCv::toOf(contour.fitRect.center);
						float averageSide = (contour.fitRect.size.width + contour.fitRect.size.height)/2.0;
						//depthPoints.push_back(center);
						depthPoints.push_back(center - ofVec2f(0,averageSide/2.0));
						depthPoints.push_back(center + ofVec2f(0,averageSide/2.0));					
						depthPoints.push_back(center);
					}

					//center
					//depthPoints.push_back(ofPoint(minX,minY));
					
					for(int i = 0; i < contour.contour.getVertices().size(); i++){
						depthPoints.push_back(contour.contour.getVertices()[i]);
					}

					//ofShortPixels& depthImg = kinectSplitter->getKinect().getRawDepthPixelsRef();
					//plant the points in the depth image to fake a way to find our distance
					for(int i = 0; i < depthPoints.size(); i++){
						int index = fakeDepthImage.getPixelIndex(depthPoints[i].x,depthPoints[i].y);
						//fakeDepthImage.getPixels()[index] = unsigned short(averageValidDepth);
						if(i < 3){ //radius determining points are a cross sectino
							fakeDepthImage.getPixels()[index] = unsigned short(medianDepth);					
						}
						else{
							fakeDepthImage.getPixels()[index] = unsigned short(scanScene.getPixels()[index]);					
						}
					}

					vector<ofVec3f> cameraPoints = kinectSplitter->getKinect().mapDepthToSkeleton(depthPoints, fakeDepthImage);

					//add some more values to the contour
//					contour.shape.onDepthRatio = 1.0 * validSamples/numSamples;
//					contour.depthPosition = averageValidDepth;
					contour.depthPosition = medianDepth;
					ofVec3f coordPos1 = cameraPoints[1];
					ofVec3f coordPos2 = cameraPoints[2];
					coordPos1.z = 0;
					coordPos2.z = 0;
					contour.coordRadius = coordPos1.distance(coordPos2) * .8; //fudge it to be smaller

					contour.shape.detectionEdge1  = cameraPoints[0];
					contour.shape.detectionEdge2  = cameraPoints[1];
					contour.shape.detectionCenter = cameraPoints[2];
					for(int i = 3; i < cameraPoints.size(); i++){
						contour.shape.contour.push_back(cameraPoints[i]);
					}

					//attach a shape to it
					//TODO Determine size based on radius
					if(contour.shape.type == CURIO_SPHERE){
						if(contour.coordRadius < CurioShape::getRadiusForSize(contour.shape.type, CURIO_SMALL)){
							contour.shape.size = CURIO_SMALL;
						}
						else if(contour.coordRadius < CurioShape::getRadiusForSize(contour.shape.type,CURIO_MEDIUM)){
							contour.shape.size = CURIO_MEDIUM;						
						}
						else {
							contour.shape.size = CURIO_LARGE;
						}
						contour.shape.position = cameraPoints[2] + ofVec3f(0,0,contour.shape.getRadius() * .75);
						//contour.shape.position = cameraPoints[2];// + ofVec3f(0,0,contour.shape.getRadius());
					}
					else if(contour.shape.type == CURIO_CUBE){
						if(contour.coordRadius < CurioShape::getRadiusForSize(contour.shape.type, CURIO_SMALL)){
							contour.shape.size = CURIO_SMALL;
						}
						else if(contour.coordRadius < CurioShape::getRadiusForSize(contour.shape.type,CURIO_MEDIUM)){
							contour.shape.size = CURIO_MEDIUM;						
						}
						else {
							contour.shape.size = CURIO_LARGE;
						}					
						contour.shape.position = cameraPoints[2] + ofVec3f(0,0,contour.shape.getRadius() * .75);
					}
					else {
						// connector
						contour.shape.position = cameraPoints[2]; 
					}
				///}
			}
		}
	}
	*/
}


void ShapeDetector::mouseMoved(ofMouseEventArgs& args){}
void ShapeDetector::mouseDragged(ofMouseEventArgs& args){}

void ShapeDetector::mousePressed(ofMouseEventArgs& args){
	
	ofVec2f samplePoint(args.x,args.y);
	ofRectangle colorWindow(0, 0, depthColors.getWidth(), depthColors.getHeight());
	ofRectangle zoomWindow(depthColors.getWidth(), 0, depthColors.getWidth(), depthColors.getHeight());

	if(colorWindow.inside(samplePoint)){
		zoomPoint = samplePoint - ofVec2f( (zoomFbo.getWidth() / 5.0)  / 2.0, (zoomFbo.getHeight() / 5.0) / 2.0);
	}
}

void ShapeDetector::mouseReleased(ofMouseEventArgs& args){}

void ShapeDetector::keyPressed(ofKeyEventArgs& args){
	if(args.key == OF_KEY_LEFT){
	}
	if(args.key == OF_KEY_RIGHT){
	}
	if(args.key == OF_KEY_DOWN){
	}
	if(args.key == OF_KEY_UP){
	}

}

void ShapeDetector::keyReleased(ofKeyEventArgs& args){
}

void ShapeDetector::exit(){
	gui->saveSettings(getSettingsFilename());
}

string ShapeDetector::getSettingsFilename(){
	return "settings/settings.xml";
}
