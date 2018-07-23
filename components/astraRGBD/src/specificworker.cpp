/*
 *    Copyright (C)2018 by YOUR NAME HERE
 *
 *    This file is part of RoboComp
 *
 *    RoboComp is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    RoboComp is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with RoboComp.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "specificworker.h"

/**
* \brief Default constructor
*/
SpecificWorker::SpecificWorker(MapPrx& mprx) : GenericWorker(mprx)
{
}

/**
* \brief Default destructor
*/
SpecificWorker::~SpecificWorker()
{
       astra::terminate();
}

bool SpecificWorker::setParams(RoboCompCommonBehavior::ParameterList params)
{
//       THE FOLLOWING IS JUST AN EXAMPLE
//
//	try
//	{
//		RoboCompCommonBehavior::Parameter par = params.at("InnerModelPath");
//		innermodel_path = par.value;
//		innermodel = new InnerModel(innermodel_path);
//	}
//	catch(std::exception e) { qFatal("Error reading config params"); }

    depthB = QString::fromStdString(params["depth"].value).contains("true");
	colorB = QString::fromStdString(params["color"].value).contains("true");



	timer.start(Period);
    astra::initialize();
    initializeStreams();



	return true;
}

void SpecificWorker::compute()
{
	QMutexLocker locker(mutex);
	//computeCODE
// 	try
// 	{
// 		camera_proxy->getYImage(0,img, cState, bState);
// 		memcpy(image_gray.data, &img[0], m_width*m_height*sizeof(uchar));
// 		searchTags(image_gray);
// 	}
// 	catch(const Ice::Exception &e)
// 	{
// 		std::cout << "Error reading from Camera" << e << std::endl;
// 	}

    readFrame();

}


Registration SpecificWorker::getRegistration()
{
//implementCODE

}

void SpecificWorker::getData(imgType &rgbMatrix, depthType &distanceMatrix, RoboCompJointMotor::MotorStateMap &hState, RoboCompGenericBase::TBaseState &bState)
{
//implementCODE
	RGBMutex->lock();
	rgbMatrix=*colorImage;
	RGBMutex->unlock();
	depthMutex->lock();
	distanceMatrix=*depthImage;
	depthMutex->unlock();

}

void SpecificWorker::getXYZ(PointSeq &points, RoboCompJointMotor::MotorStateMap &hState, RoboCompGenericBase::TBaseState &bState)
{
//implementCODE

}

void SpecificWorker::getRGB(ColorSeq &color, RoboCompJointMotor::MotorStateMap &hState, RoboCompGenericBase::TBaseState &bState)
{
//implementCODE

}

TRGBDParams SpecificWorker::getRGBDParams()
{
//implementCODE

}

void SpecificWorker::getDepth(DepthSeq &depth, RoboCompJointMotor::MotorStateMap &hState, RoboCompGenericBase::TBaseState &bState)
{
//implementCODE

}

void SpecificWorker::setRegistration(const Registration &value)
{
//implementCODE

}

void SpecificWorker::getImage(ColorSeq &color, DepthSeq &depth, PointSeq &points, RoboCompJointMotor::MotorStateMap &hState, RoboCompGenericBase::TBaseState &bState)
{
//implementCODE

}

void SpecificWorker::getDepthInIR(depthType &distanceMatrix, RoboCompJointMotor::MotorStateMap &hState, RoboCompGenericBase::TBaseState &bState)
{
//implementCODE

}


void SpecificWorker::initializeStreams()
{
    qDebug()<<"\tInitializing streams"<<endl;
	if (depthB) {
        depthReader = new(astra::StreamReader);
        qDebug()<<"\t\tDepth reader created"<<endl;
        *depthReader = streamSet.create_reader();

        depthReader->stream<astra::DepthStream>().start();
        astra::Frame frame = depthReader->get_latest_frame();
        const auto depthFrame = frame.get<astra::DepthFrame>();
        int width = depthFrame.width();
        int height = depthFrame.height();
        depthImage = new vector<float>(width*height);
        qDebug()<<"\t\tDepth stream initalized";
	}

	if (colorB){
	    qDebug()<<"\t\tColor reader created"<<endl;
	    *colorReader = streamSet.create_reader();
        colorReader->stream<astra::ColorStream>().start();
        astra::Frame frame = depthReader->get_latest_frame();
        const auto colorFrame = frame.get<astra::ColorFrame>();
	    int width = colorFrame.width();
        int height = colorFrame.height();
        colorImage = new vector<Ice::Byte>(width*height*3);
	}
}

void SpecificWorker::readFrame()
{
    astra::Frame frame = depthReader->get_latest_frame();
    fps++;
	if (depthB) {

        const auto depthFrame = frame.get<astra::DepthFrame>();
        int width = depthFrame.width();
        int height = depthFrame.height();
        const int16_t* depthPtr = depthFrame.data();
        RGBMutex->lock();
    	memcpy(&depthImage->operator[](0),depthPtr,width*height);
	    RGBMutex->unlock();
	}

	if (colorB){
	    const auto colorFrame = frame.get<astra::ColorFrame>();
	    int width = colorFrame.width();
        int height = colorFrame.height();
        const astra::RgbPixel* colorPtr = colorFrame.data();
        RGBMutex->lock();
    	memcpy(&colorImage->operator[](0),colorPtr,width*height*3);
	    RGBMutex->unlock();
	}

}