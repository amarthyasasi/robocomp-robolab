/*
 *    Copyright (C) 2020 by jvallero & mtorocom
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
#include <Eigen/Dense>
#include <iostream>
#include <math.h>
#include "grid.h"
#include <QGraphicsSceneMouseEvent>


/**
* \brief Default constructor
*/
SpecificWorker::SpecificWorker(TuplePrx tprx, bool startup_check) : GenericWorker(tprx) {
    this->startup_check_flag = startup_check;
}

/**
* \brief Default destructor
*/
SpecificWorker::~SpecificWorker()
{
    std::cout << "Destroying SpecificWorker" << std::endl;
}

bool SpecificWorker::setParams(RoboCompCommonBehavior::ParameterList params)
{
    try
    {
        RoboCompCommonBehavior::Parameter par = params.at("InnerModel");
        std::string innermodel_path = par.value;
        innerModel = std::make_shared<InnerModel>(innermodel_path);
        dim.TILE_SIZE = int(100);  // Get from config
        dim.HMIN = std::min(std::stof(params.at("OuterRegionLeft").value), std::stof(params.at("OuterRegionRight").value));
        dim.WIDTH = std::max(std::stof(params.at("OuterRegionLeft").value), std::stof(params.at("OuterRegionRight").value)) - dim.HMIN;
        dim.VMIN = std::min(std::stof(params.at("OuterRegionTop").value), std::stof(params.at("OuterRegionBottom").value));
        dim.HEIGHT = std::max(std::stof(params.at("OuterRegionTop").value), std::stof(params.at("OuterRegionBottom").value)) - dim.VMIN;
    }
    catch (const std::exception &e) { std::cout << e.what() << std::endl; qFatal("Error reading config params"); }
    confParams  = std::make_shared<RoboCompCommonBehavior::ParameterList>(params);
    return true;
}

void SpecificWorker::initialize(int period)
{
    std::cout << "Initialize worker" << std::endl;

    // graphics
    init_drawing(dim);
    initializeWorld();
    navigation.initialize(innerModel, confParams, omnirobot_proxy, &scene, robot_polygon);

    this->Period = 100;
    if (this->startup_check_flag)
        this->startup_check();
    else
        timer.start(Period);
}

void SpecificWorker::compute()
{
    auto robot_pose = read_base();
    auto [laser_data, ldata] = read_laser(); //QPolygon (robot) and RoboComp (robot)
    draw_laser(laser_data);

    // check for new target
    if(auto t = target_buffer.try_get(); t.has_value())
    {
        //set target
        target.pos = t.value().pos;
        draw_target(robot_pose, t.value().pos);
        if( not navigation.plan_new_path(target.pos))
        { qWarning() << __FUNCTION__ << "No plan found. Try a new target"; return; }
        atTarget = false;
    }
    if(not atTarget)
    {
        QVec rtarget =  innerModel->transform("robot", QVec::vec3(target.pos.x(), 0., target.pos.y()), "world");
        //double target_ang = -atan2(rtarget[0], rtarget[2]);
        //double rot_error = -atan2(target.pos.x() - bState.x, target.pos.y() - bState.z) - bState.alpha;
        double pos_error = rtarget.norm2();
        if (pos_error < MIN_DISTANCE_TO_GOAL)
        {
            stop_robot();
            atTarget = true;
        }
        else
        {
            navigation.update(robot_pose, laser_data, target);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////// From Coppelia bridge //////////////////////////////////////////////////////////////////////////

Navigation<SpecificWorker::MyGrid, Controller>::RobotPose SpecificWorker::read_base()
{
    try
    {
        RoboCompGenericBase::TBaseState bState;
        omnirobot_proxy->getBaseState(bState);
        robot_polygon_draw->setRotation(qRadiansToDegrees(bState.alpha));
        robot_polygon_draw->setPos(bState.x, bState.z);
        innerModel->updateTransformValues("robot", bState.x, 0, bState.z, 0, -bState.alpha, 0);  // WATCH - sign
        return RobotPose{bState.x, bState.z, bState.alpha, bState.advVx, bState.advVz, bState.rotV, robot_polygon_draw};
    }
    catch(const Ice::Exception &e){};
    return RobotPose();
}

std::tuple<QPolygonF,RoboCompLaser::TLaserData> SpecificWorker::read_laser() // in robot coordinates
{
    QPolygonF laser_poly;
    RoboCompLaser::TLaserData ldata;
    try
    {
        ldata = laser_proxy->getLaserData();

        // Simplify laser contour with Ramer-Douglas-Peucker
        std::vector<Point> plist(ldata.size());
        std::generate(plist.begin(), plist.end(), [ldata, k=0]() mutable
        { auto &l = ldata[k++]; return std::make_pair(l.dist * sin(l.angle), l.dist * cos(l.angle));});
        vector<Point> pointListOut;
        ramer_douglas_peucker(plist, MAX_RDP_DEVIATION_mm, pointListOut);
        laser_poly.resize(pointListOut.size());
        std::generate(laser_poly.begin(), laser_poly.end(), [pointListOut, this, k=0]() mutable
        { auto &p = pointListOut[k++]; return QPointF(p.first, p.second);});

        // Filter out spikes. If the angle between two line segments is less than to the specified maximum angle
        std::vector<QPointF> removed;
        for(auto &&[k, ps] : iter::sliding_window(laser_poly,3) | iter::enumerate)
            if( MAX_SPIKING_ANGLE_rads > acos(QVector2D::dotProduct( QVector2D(ps[0] - ps[1]).normalized(), QVector2D(ps[2] - ps[1]).normalized())))
                removed.push_back(ps[1]);
        for(auto &&r : removed)
            laser_poly.erase(std::remove_if(laser_poly.begin(), laser_poly.end(), [r](auto &p) { return p == r; }), laser_poly.end());
    }
    catch(const Ice::Exception &e)
    { std::cout << "Error reading from Laser" << e << std::endl;}
    laser_poly.pop_back();
    return std::make_tuple(laser_poly, ldata);  //robot coordinates
}

void SpecificWorker::stop_robot()
{
    try
    {
        omnirobot_proxy->setSpeedBase(0, 0, 0);
        std::cout << "FINISH" << std::endl;
        atTarget = true;
    }
    catch(const Ice::Exception &e)
    { std::cout << e.what() << std::endl;}
}

//load world model from file
void SpecificWorker::initializeWorld()
{
    QString val;
    QFile file(QString::fromStdString(confParams->at("World").value));
    if (not file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        qDebug() << "Error reading world file, check config params:" << QString::fromStdString(confParams->at("World").value);
        exit(-1);
    }
    val = file.readAll();
    file.close();
    QJsonDocument doc = QJsonDocument::fromJson(val.toUtf8());
    QJsonObject jObject = doc.object();
    QVariantMap mainMap = jObject.toVariantMap();
    //load dimensions
//	QVariantMap dim = mainMap[QString("dimensions")].toMap();
//	dimensions = TDim{dim["TILESIZE"].toInt(), dim["LEFT"].toFloat(), dim["BOTTOM"].toFloat(), dim["WIDTH"].toFloat(), dim["HEIGHT"].toFloat()};

    //load tables
    QVariantMap tables = mainMap[QString("tables")].toMap();
    for (auto &t : tables)
    {
        QVariantList object = t.toList();
        auto box = scene.addRect(QRectF(-object[2].toFloat() / 2, -object[3].toFloat() / 2, object[2].toFloat(), object[3].toFloat()), QPen(QColor("SandyBrown")), QBrush(QColor("SandyBrown")));
        box->setPos(object[4].toFloat(), object[5].toFloat());
        box->setRotation(object[6].toFloat());
        boxes.push_back(box);
    }
    //load roundtables
    QVariantMap rtables = mainMap[QString("roundTables")].toMap();
    for (auto &t : rtables)
    {
        QVariantList object = t.toList();
        auto box = scene.addEllipse(QRectF(-object[2].toFloat() / 2, -object[3].toFloat() / 2, object[2].toFloat(), object[3].toFloat()), QPen(QColor("Khaki")), QBrush(QColor("Khaki")));
        box->setPos(object[4].toFloat(), object[5].toFloat());
        boxes.push_back(box);
    }
    //load walls
    QVariantMap walls = mainMap[QString("walls")].toMap();
    for (auto &t : walls)
    {
        QVariantList object = t.toList();
        auto box = scene.addRect(QRectF(-object[2].toFloat() / 2, -object[3].toFloat() / 2, object[2].toFloat(), object[3].toFloat()), QPen(QColor("Brown")), QBrush(QColor("Brown")));
        box->setPos(object[4].toFloat(), object[5].toFloat());
        box->setRotation(object[6].toFloat()*180/M_PI);
        boxes.push_back(box);
    }

    //load points
    QVariantMap points = mainMap[QString("points")].toMap();
    for (auto &t : points)
    {
        QVariantList object = t.toList();
        auto box = scene.addRect(QRectF(-object[2].toFloat() / 2, -object[3].toFloat() / 2, object[2].toFloat(), object[3].toFloat()), QPen(QColor("Brown")), QBrush(QColor("Brown")));
        box->setPos(object[4].toFloat(), object[5].toFloat());
        box->setRotation(object[6].toFloat()*180/M_PI);
        boxes.push_back(box);
    }
    //load boxes
    QVariantMap cajas = mainMap[QString("boxes")].toMap();
    for (auto &t : cajas)
    {
        QVariantList object = t.toList();
        auto box = scene.addRect(QRectF(-object[2].toFloat() / 2, -object[3].toFloat() / 2, object[2].toFloat(), object[3].toFloat()), QPen(QColor("Brown")), QBrush(QColor("Orange")));
        box->setPos(object[4].toFloat(), object[5].toFloat());
        box->setRotation(object[6].toFloat()*180/M_PI);
        box->setFlag(QGraphicsItem::ItemIsMovable);
        boxes.push_back(box);
    }
}

void SpecificWorker::ramer_douglas_peucker(const vector<Point> &pointList, double epsilon, vector<Point> &out)
{
    if(pointList.size()<2)
    {
        qWarning() << "Not enough points to simplify";
        return;
    }

    // Find the point with the maximum distance from line between start and end
    auto line = Eigen::ParametrizedLine<float, 2>::Through(Eigen::Vector2f(pointList.front().first, pointList.front().second),
                                                           Eigen::Vector2f(pointList.back().first, pointList.back().second));
    auto max = std::max_element(pointList.begin()+1, pointList.end(), [line](auto &a, auto &b)
    { return line.distance(Eigen::Vector2f(a.first, a.second)) < line.distance(Eigen::Vector2f(b.first, b.second));});
    float dmax =  line.distance(Eigen::Vector2f((*max).first, (*max).second));

    // If max distance is greater than epsilon, recursively simplify
    if(dmax > epsilon)
    {
        // Recursive call
        vector<Point> recResults1;
        vector<Point> recResults2;
        vector<Point> firstLine(pointList.begin(), max + 1);
        vector<Point> lastLine(max, pointList.end());

        ramer_douglas_peucker(firstLine, epsilon, recResults1);
        ramer_douglas_peucker(lastLine, epsilon, recResults2);

        // Build the result list
        out.assign(recResults1.begin(), recResults1.end() - 1);
        out.insert(out.end(), recResults2.begin(), recResults2.end());
        if (out.size() < 2)
        {
            qWarning() << "Problem assembling output";
            return;
        }
    }
    else
    {
        //Just return start and end points
        out.clear();
        out.push_back(pointList.front());
        out.push_back(pointList.back());
    }
}

///////////////////////////////////// DRAWING /////////////////////////////////////////////////////////////////
void SpecificWorker::init_drawing( Dimensions dim)
{
    graphicsView->setScene(&scene);
    graphicsView->setMinimumSize(400,400);
    scene.setSceneRect(dim.HMIN, dim.VMIN, dim.WIDTH, dim.HEIGHT);
    graphicsView->scale(1, -1);
    graphicsView->fitInView(scene.sceneRect(), Qt::KeepAspectRatio );
    graphicsView->show();
    connect(&scene, &MyScene::new_target, this, [this](QGraphicsSceneMouseEvent *e)
    {
        qDebug() << "Lambda SLOT: " << e->scenePos();
        target_buffer.put(std::move(e->scenePos()), [r=robot_polygon_draw->pos()](auto &&t, auto &out)
        {
            out.pos = t;
            out.ang  = -atan2(t.x() - r.x(), t.y() - r.y()); //target ang in the direction or line joining robot-target
        });
    });

    //robot
    auto robotXWidth = std::stof(confParams->at("RobotXWidth").value);
    auto robotZLong = std::stof(confParams->at("RobotZLong").value);
    robot_polygon  << QPointF(-robotXWidth/2, -robotZLong/2)
                   << QPointF(-robotXWidth/2, 0.)
                   << QPointF(-robotXWidth/2, robotZLong/2)
                   << QPointF(0, robotZLong/2)
                   << QPointF(robotXWidth/2, robotZLong/2)
                   << QPointF(robotXWidth/2, 0.)
                   << QPointF(robotXWidth/2, -robotZLong/2)
                   << QPointF(0, -robotZLong/2);

    QPolygonF marca(QRectF(-25,-25, 50, 50));

    QColor rc("DarkRed"); rc.setAlpha(80);
    robot_polygon_draw = scene.addPolygon(robot_polygon, QPen(QColor("DarkRed")), QBrush(rc));
    auto marca_polygon = scene.addPolygon(marca, QPen(QColor("White")), QBrush("White"));
    marca_polygon->setParentItem(robot_polygon_draw);
    marca_polygon->setPos(QPointF(0, robotZLong/2 - 40));
    robot_polygon_draw->setZValue(5);
    try
    {
        RoboCompGenericBase::TBaseState bState;
        omnirobot_proxy->getBaseState(bState);
        robot_polygon_draw->setRotation(qRadiansToDegrees(bState.alpha));
        robot_polygon_draw->setPos(bState.x, bState.z);
    }
    catch(const Ice::Exception &e){};;
}

void SpecificWorker::draw_laser(const QPolygonF &poly) // robot coordinates
{
    static QGraphicsItem *laser_polygon = nullptr;
    if (laser_polygon != nullptr)
        scene.removeItem(laser_polygon);

    QColor color("LightGreen");
    color.setAlpha(40);
    laser_polygon = scene.addPolygon(robot_polygon_draw->mapToScene(poly), QPen(QColor("DarkGreen"), 30), QBrush(color));
    laser_polygon->setZValue(3);
}

void SpecificWorker::draw_target(const RobotPose &robot_pose, QPointF t)
{
    if (target_draw) scene.removeItem(target_draw);
    target_draw = scene.addEllipse(t.x() - 50, t.y() - 50, 100, 100, QPen(QColor("green")), QBrush(QColor("green")));
    // angular reference obtained from line joinning robot an target when  clicking
    float tr_x = t.x() - robot_pose.x;
    float tr_y = t.y() - robot_pose.y;
    float ref_ang = -atan2(tr_x, tr_y);   // signo menos para tener ángulos respecto a Y CCW
    auto ex = t.x() + 350 * sin(-ref_ang);
    auto ey = t.y() + 350 * cos(-ref_ang);  //OJO signos porque el ang está respecto a Y CCW
    auto line = scene.addLine(t.x(), t.y(), ex, ey, QPen(QBrush(QColor("green")), 20));
    line->setParentItem(target_draw);
    auto ball = scene.addEllipse(ex - 25, ey - 25, 50, 50, QPen(QColor("green")), QBrush(QColor("green")));
    ball->setParentItem(target_draw);
}

////////////////////////////////////////////////////////////////
    int SpecificWorker::startup_check() {
        std::cout << "Startup check" << std::endl;
        QTimer::singleShot(200, qApp, SLOT(quit()));
        return 0;
    }

////////////////////////// SERVANT ////////////////////////////////////////7
void SpecificWorker::NavigationOptimizer_abort()
{
    stop_robot();
}

RoboCompNavigationOptimizer::Params SpecificWorker::NavigationOptimizer_getParams()
{
    RoboCompNavigationOptimizer::Params params;
    return params;
}

RoboCompNavigationOptimizer::State SpecificWorker::NavigationOptimizer_getState()
{
    RoboCompNavigationOptimizer::State state;
    return( state );
}

bool SpecificWorker::NavigationOptimizer_gotoNewRandomPoint( RoboCompNavigationOptimizer::Params params)

{
    // get random safe target

    target_buffer.put(std::move(QPointF()), [r=robot_polygon_draw->pos()](auto &&t, auto &out)
    {
        out.pos = t;
        out.ang  = -atan2(t.x() - r.x(), t.y() - r.y()); //target ang in the direction or line joining robot-target
    });
}

