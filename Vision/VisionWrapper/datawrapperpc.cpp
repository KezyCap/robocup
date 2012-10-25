#include <boost/foreach.hpp>
#include "datawrapperpc.h"
#include "Infrastructure/NUImage/ColorModelConversions.h"
#include "debug.h"
#include "nubotdataconfig.h"

#include "Vision/VisionTypes/coloursegment.h"
#include "Vision/visionconstants.h"

#include <QFileDialog>

DataWrapper* DataWrapper::instance = 0;

string DataWrapper::getIDName(DEBUG_ID id) {
    switch(id) {
    case DBID_IMAGE:
        return "DBID_IMAGE";
    case DBID_H_SCANS:
        return "DBID_H_SCANS";
    case DBID_V_SCANS:
        return "DBID_V_SCANS";
    case DBID_SEGMENTS:
        return "DBID_SEGMENTS";
    case DBID_MATCHED_SEGMENTS:
        return "DBID_MATCHED_SEGMENTS";
    case DBID_HORIZON:
        return "DBID_HORIZON";
    case DBID_GREENHORIZON_SCANS:
        return "DBID_GREENHORIZON_SCANS";
    case DBID_GREENHORIZON_FINAL:
        return "DBID_GREENHORIZON_FINAL";
    case DBID_OBJECT_POINTS:
        return "DBID_OBJECT_POINTS";
    case DBID_FILTERED_SEGMENTS:
        return "DBID_FILTERED_SEGMENTS";
    case DBID_GOALS:
        return "DBID_GOALS";
    case DBID_BEACONS:
        return "DBID_BEACONS";
    case DBID_BALLS:
        return "DBID_BALLS";
    case DBID_OBSTACLES:
        return "DBID_OBSTACLES";
    default:
        return "NOT VALID";
    }
}

void getPointsAndColoursFromSegments(const vector< vector<ColourSegment> >& segments, vector<cv::Scalar>& colours, vector<PointType>& pts)
{
    unsigned char r, g, b;
    
    BOOST_FOREACH(const vector<ColourSegment>& line, segments) {
        BOOST_FOREACH(const ColourSegment& seg, line) {
            ClassIndex::getColourAsRGB(seg.getColour(), r, g, b);
            pts.push_back(seg.getStart());
            pts.push_back(seg.getEnd());
            colours.push_back(cv::Scalar(b,g,r));
        }
    }
}

void doGaussian(NUImage* img) {
    unsigned char* ptr;
            //r, g, b,
            //Y, cb, cr;
    vector<const VisionFieldObject*>::const_iterator it;
    cv::Mat mat(img->getHeight(), img->getWidth(), CV_8UC3);

    for(int y=0; y<img->getHeight(); y++) {
        ptr = mat.ptr<unsigned char>(y);
        for(int x=0; x<img->getWidth(); x++) {
//            ColorModelConversions::fromYCbCrToRGB((*img)(x,y).y, (*img)(x,y).cb, (*img)(x,y).cr, r, g, b);
//            ptr[3*x]   = b;
//            ptr[3*x+1] = g;
//            ptr[3*x+2] = r;
            ptr[3*x]   = (*img)(x,y).y;
            ptr[3*x+1] = (*img)(x,y).cb;
            ptr[3*x+2] = (*img)(x,y).cr;
        }
    }

    cv::GaussianBlur(mat, mat, cv::Size (5, 5), 0);

    for(int y=0; y<img->getHeight(); y++) {
        ptr = mat.ptr<unsigned char>(y);
        for(int x=0; x<img->getWidth(); x++) {
//            ColorModelConversions::fromRGBToYCbCr(ptr[3*x+2], ptr[3*x+1], ptr[3*x], Y, cb, cr);
//            Pixel px;
//            px.yCbCrPadding = Y;
//            px.cb = cb;
//            px.y = Y;
//            px.cr = cr;
//            img->setPixel(x, y, px);
            Pixel px;
            px.yCbCrPadding = ptr[3*x];
            px.cb = ptr[3*x+1];
            px.y = ptr[3*x];
            px.cr = ptr[3*x+2];
            img->setPixel(x, y, px);
        }
    }
}

DataWrapper::DataWrapper()
{
    //frame grab methods
    switch(METHOD) {
    case CAMERA:
        m_camera = new PCCamera();
        m_current_image = m_camera->grabNewImage();
        LUTname = string(getenv("HOME")) +  string("/nubot/default.lut");
        break;
    case STREAM:
        if(false) {
            streamname = QFileDialog::getOpenFileName(NULL, "Select image stream", getenv("HOME"),  "Stream Files (*.strm)").toStdString();
            LUTname = QFileDialog::getOpenFileName(NULL, "Select Lookup Table", getenv("HOME"),  "LUT Files (*.lut)").toStdString();
            configname = QFileDialog::getOpenFileName(NULL, "Select Configuration File", getenv("HOME"), "config Files (*.cfg)").toStdString();
        }
        else {
            streamname = string(getenv("HOME")) + string("/nubot/image.strm");
            LUTname = string(getenv("HOME")) + string("/nubot/default.lut");
            configname = string(getenv("HOME")) + string("/nubot/") + string("VisionOptions.cfg");
        }
        imagestrm.open(streamname.c_str());
        m_current_image = new NUImage();
        if(imagestrm.is_open()) {
            imagestrm >> *m_current_image;
        }
        else {
            errorlog << "DataWrapper::DataWrapper() - failed to load stream: " << streamname << endl;
        }
        break;
    case FILE:
        m_camera = NULL;

        m_current_image = new NUImage();
        m_yuyv_buffer = 0;

        num_images = 2;
        cur_image = 0;
        stringstream strm;
        capture = new VideoCapture(strm.str());
        *capture >> m_current_image_cv;

        generateImageFromMat(m_current_image_cv);
        loadLUTFromFile(string(getenv("HOME")) +  string("/nubot/default.lut"));

        break;
    }

    //set up fake horizon
    kinematics_horizon.setLine(0, 1, 50);

    if(!loadLUTFromFile(LUTname)){
        errorlog << "DataWrapper::DataWrapper() - failed to load LUT: " << LUTname << endl;
    }

    //create debug windows
    debug_window_num = 4;
    debug_windows = new pair<string, cv::Mat>[debug_window_num];
    debug_windows[0].first = "Debug1";
    debug_windows[1].first = "Debug2";
    debug_windows[2].first = "Debug3";
    debug_windows[3].first = "Classified";
    for(int i=0; i<debug_window_num; i++) {
        namedWindow(debug_windows[i].first, CV_WINDOW_KEEPRATIO);
        debug_windows[i].second.create(m_current_image->getHeight(), m_current_image->getWidth(), CV_8UC3);
    }

    //create map into images
    debug_map[DBID_IMAGE].push_back(                &debug_windows[1]);
//    debug_map[DBID_IMAGE].push_back(                &debug_windows[2]);
    debug_map[DBID_CLASSED_IMAGE].push_back(        &debug_windows[3]);
    debug_map[DBID_H_SCANS].push_back(              &debug_windows[0]);
    debug_map[DBID_V_SCANS].push_back(              &debug_windows[0]);
    debug_map[DBID_SEGMENTS].push_back(             &debug_windows[0]);
    debug_map[DBID_MATCHED_SEGMENTS].push_back(     &debug_windows[1]);
    debug_map[DBID_MATCHED_SEGMENTS].push_back(     &debug_windows[3]);

    debug_map[DBID_HORIZON].push_back(              &debug_windows[0]);
    debug_map[DBID_GREENHORIZON_SCANS].push_back(   &debug_windows[0]);
    debug_map[DBID_GREENHORIZON_FINAL].push_back(   &debug_windows[1]);
    debug_map[DBID_GREENHORIZON_FINAL].push_back(   &debug_windows[3]);

    debug_map[DBID_OBJECT_POINTS].push_back(        &debug_windows[0]);
    debug_map[DBID_FILTERED_SEGMENTS] .push_back(   &debug_windows[0]);
    debug_map[DBID_GOALS].push_back(                &debug_windows[0]);
    debug_map[DBID_BEACONS].push_back(              &debug_windows[0]);
    debug_map[DBID_BALLS].push_back(                &debug_windows[0]);
    debug_map[DBID_OBSTACLES].push_back(            &debug_windows[0]);
    debug_map[DBID_LINES].push_back(                &debug_windows[0]);
    
    results_window_name = "Results";
    namedWindow(results_window_name, CV_WINDOW_KEEPRATIO);
    results_img.create(m_current_image->getHeight(), m_current_image->getWidth(), CV_8UC3);
    
    numFramesDropped = numFramesProcessed = 0;

}

DataWrapper::~DataWrapper()
{
}

void DataWrapper::generateImageFromMat(cv::Mat& frame)
{
    int size = frame.rows*frame.cols;
    if (frame.rows != m_current_image->getHeight() or frame.cols != m_current_image->getWidth())
    {
        if(m_yuyv_buffer)
            delete m_yuyv_buffer;
        m_yuyv_buffer = new unsigned char[size*2];
        m_current_image->MapYUV422BufferToImage(m_yuyv_buffer, frame.cols, frame.rows);
    }

    int i_YUV = 0;			// the index into the yuyv_buffer
    unsigned char y1,u1,v1,y2,u2,v2;
    for (int i=0; i<frame.rows; i++)
    {
        for (int j=frame.cols/2; j>0; j--)		// count down to flip the image around
        {
            ColorModelConversions::fromRGBToYCbCr(frame.at<Vec3b>(i,j<<1)[2], frame.at<Vec3b>(i,j<<1)[1], frame.at<Vec3b>(i,j<<1)[0], y1, u1, v1);
            ColorModelConversions::fromRGBToYCbCr(frame.at<Vec3b>(i,(j<<1)-1)[2], frame.at<Vec3b>(i,(j<<1)-1)[1], frame.at<Vec3b>(i,(j<<1)-1)[0], y2, u2, v2);
            m_yuyv_buffer[i_YUV++] = y1;
            m_yuyv_buffer[i_YUV++] = (u1+u2)>>1;
            m_yuyv_buffer[i_YUV++] = y2;
            m_yuyv_buffer[i_YUV++] = (v1+v2)>>1;
        }
    }
}

DataWrapper* DataWrapper::getInstance()
{
    if(!instance)
        instance = new DataWrapper();
    return instance;
}

/**
*   @brief Fetches the next frame from the webcam.
*/
NUImage* DataWrapper::getFrame()
{
    return m_current_image;
}

//! @brief Generates spoofed camera transform vector.
bool DataWrapper::getCTGVector(vector<float> &ctgvector)
{
    //bool isOK = getSensorsData()->get(NUSensorsData::CameraToGroundTransform, ctgvector);
    //return isOK;
    ctgvector.clear();
    ctgvector.push_back(0);
    ctgvector.push_back(0);
    ctgvector.push_back(0);
    ctgvector.push_back(0);
    return false;
}

//! @brief Generates spoofed camera transform vector.
bool DataWrapper::getCTVector(vector<float> &ctvector)
{
    //bool isOK = getSensorsData()->get(NUSensorsData::CameraToGroundTransform, ctgvector);
    //return isOK;
    ctvector.clear();
    ctvector.push_back(0);
    ctvector.push_back(0);
    ctvector.push_back(0);
    ctvector.push_back(0);
    return false;
}


//! @brief Generates spoofed camera height.
bool DataWrapper::getCameraHeight(float& height)
{
    height = 0;
    return false;
}


//! @brief Generates spoofed camera pitch.
bool DataWrapper::getCameraPitch(float& pitch)
{
    pitch = 0;
    return false;
}

//! @brief Generates spoofed body pitch.
bool DataWrapper::getBodyPitch(float& pitch)
{
    pitch = 0;
    return false;
}

//! @brief Returns spoofed kinecv::Matics horizon.
const Horizon& DataWrapper::getKinematicsHorizon()
{ 
    return kinematics_horizon;
}

////! @brief Returns image width.
//unsigned int DataWrapper::getImageWidth()
//{
//    return m_image_width;
//}

////! @brief Returns image height.
//unsigned int DataWrapper::getImageHeight()
//{
//    return m_image_height;
//}

//! @brief Returns camera settings.
CameraSettings DataWrapper::getCameraSettings()
{
    switch(METHOD) {
    case CAMERA:
        return m_camera->getSettings();
    case STREAM:
        return CameraSettings();
    case FILE:
        return CameraSettings();
    }
}

const LookUpTable& DataWrapper::getLUT() const
{
    return LUT;
}

void DataWrapper::publish(const vector<const VisionFieldObject*> &visual_objects)
{
    //cout << visual_objects.size() << " visual objects seen" << std::endl;
}

void DataWrapper::publish(const VisionFieldObject* visual_object)
{
    //cout << "Visual object seen at " << visual_object->getLocationPixels() << std::endl;
    visual_object->printLabel(debug);
    debug << endl;
}

//! Outputs debug data to the appropriate external interface
void DataWrapper::debugRefresh()
{
    for(int i=0; i<debug_window_num; i++) {
        debug_windows[i].second = cv::Scalar(0,0,0);
    }

    LUT.classifyImage(*m_current_image, results_img);
    imshow(results_window_name, results_img);
    debugPublish(DBID_CLASSED_IMAGE);
}

//! Outputs supply data to the appropriate external interface
bool DataWrapper::debugPublish(DEBUG_ID id)
{
    //warning if called after another publish method for the same window it will overwrite the window
    switch(id) {
    case DBID_CLASSED_IMAGE:
        //Get window assigned to id
        map<DEBUG_ID, vector< pair<string, cv::Mat>* > >::iterator map_it = debug_map.find(id);
        if(map_it == debug_map.end()) {
            errorlog << "DataWrapper::debugPublish - Missing window definition DEBUG_ID = " << getIDName(id) << endl;
            return false;
        }

        //for all images
        for(int i=0; i<map_it->second.size(); i++) {
            string& window = map_it->second[i]->first; //get window name from pair
            cv::Mat& img = map_it->second[i]->second; //get window name from pair

            LUT.classifyImage(*m_current_image, img);

            imshow(window, img);
        }
        break;
    }
    return true;
}

bool DataWrapper::debugPublish(const vector<Ball>& data) {
    #if VISION_WRAPPER_VERBOSITY > 1
        if(data.empty()) {
            debug << "DataWrapper::debugPublish - empty vector DEBUG_ID = " << getIDName(DBID_BALLS) << endl;
            return false;
        }
    #endif
    
    cv::Mat& img = results_img;    //get image from pair
    string& window = results_window_name; //get window name from pair
    
    BOOST_FOREACH(Ball b, data) {
        cv::circle(img, cv::Point2i(b.getLocationPixels().x, b.getLocationPixels().y), b.getRadius(), cv::Scalar(255,255,0), 2);
    }

    imshow(window, img);    //refresh this particular debug window
    return true;
}

bool DataWrapper::debugPublish(const vector<Beacon>& data) {
    
#if VISION_WRAPPER_VERBOSITY > 1
    if(data.empty()) {
        debug << "DataWrapper::debugPublish - empty vector DEBUG_ID = " << getIDName(DBID_GOALS) << endl;
        return false;
    }
#endif
    
    cv::Mat& img = results_img;    //get image from pair
    string& window = results_window_name; //get window name from pair
    PointType bl, tr;

    BOOST_FOREACH(Beacon b, data) {
        bl = b.getQuad().getBottomLeft();
        tr = b.getQuad().getTopRight();
        if(b.getID() == VisionFieldObject::BEACON_B)
            rectangle(img, cv::Point2i(bl.x, bl.y), cv::Point2i(tr.x, tr.y), cv::Scalar(255,0,255), 2, 8, 0);
        else if(b.getID() == VisionFieldObject::BEACON_Y)
            rectangle(img, cv::Point2i(bl.x, bl.y), cv::Point2i(tr.x, tr.y), cv::Scalar(255,255,0), 2, 8, 0);
        else
            rectangle(img, cv::Point2i(bl.x, bl.y), cv::Point2i(tr.x, tr.y), cv::Scalar(255,255,255), 2, 8, 0);
    }
    
    imshow(window, img);    //refresh this particular debug window
    return true;
}

bool DataWrapper::debugPublish(const vector<Goal>& data) {

#if VISION_WRAPPER_VERBOSITY > 1
    if(data.empty()) {
        debug << "DataWrapper::debugPublish - empty vector DEBUG_ID = " << getIDName(DBID_GOALS) << endl;
        return false;
    }
#endif
    
    cv::Mat& img = results_img;    //get image from pair
    string& window = results_window_name; //get window name from pair

    PointType bl, tr;

    BOOST_FOREACH(Goal post, data) {
        bl = post.getQuad().getBottomLeft();
        tr = post.getQuad().getTopRight();
        cv::Scalar colour;
        if(VisionFieldObject::isBlueGoal(post.getID()))
            colour = cv::Scalar(0,255,255);
        else
            colour = cv::Scalar(255,0,0);
        rectangle(img, cv::Point2i(bl.x, bl.y), cv::Point2i(tr.x, tr.y), colour, 2, 8, 0);
    }
    
    imshow(window, img);    //refresh this particular debug window
    return true;
}

bool DataWrapper::debugPublish(const vector<Obstacle>& data)
{
    cout << data.size() << " Obstacles seen" << std::endl;
    return false;
}

bool DataWrapper::debugPublish(const vector<FieldLine> &data)
{
#if VISION_WRAPPER_VERBOSITY > 1
    if(data.empty()) {
        debug << "DataWrapper::debugPublish - empty vector DEBUG_ID = " << getIDName(DBID_LINES) << endl;
        return false;
    }
#endif

    //Get window assigned to id
    map<DEBUG_ID, vector< pair<string, cv::Mat>* > >::iterator map_it = debug_map.find(DBID_LINES);
    if(map_it == debug_map.end()) {
        errorlog << "DataWrapper::debugPublish - Missing window definition DEBUG_ID = " << getIDName(DBID_LINES) << endl;
        return false;
    }

    //for all images
    for(int i=0; i<map_it->second.size(); i++) {
        cv::Mat& img = map_it->second[i]->second;    //get image from pair
        string& window = map_it->second[i]->first; //get window name from pair

        BOOST_FOREACH(FieldLine l, data) {
            l.render(img);
        }

        imshow(window, img);    //refresh this particular debug window
    }
    return true;
}

bool DataWrapper::debugPublish(DEBUG_ID id, const vector<PointType>& data_points)
{
    map<DEBUG_ID, vector<pair<string, cv::Mat>* > >::iterator map_it;
    vector<PointType>::const_iterator it;

#if VISION_WRAPPER_VERBOSITY > 1
    if(data_points.empty()) {
        debug << "DataWrapper::debugPublish - empty vector DEBUG_ID = " << getIDName(id) << endl;
        return false;
    }
#endif

    //Get window assigned to id
    map_it = debug_map.find(id);
    if(map_it == debug_map.end()) {
        errorlog << "DataWrapper::debugPublish - Missing window definition DEBUG_ID = " << getIDName(id) << endl;
        return false;
    }


    //for all images
    for(int i=0; i<map_it->second.size(); i++) {
        cv::Mat& img = map_it->second[i]->second;    //get image from pair
        string& window = map_it->second[i]->first; //get window name from pair


    #if VISION_WRAPPER_VERBOSITY > 2
        debug << id << endl;
        debug << colour[0] << "," << colour[1] << "," << colour[2] << "," << colour[3] << "\t";
        debug << data_points << endl;
    #endif

        switch(id) {
        case DBID_H_SCANS:
            BOOST_FOREACH(const PointType& pt, data_points) {
                cv::line(img, cv::Point2i(0, pt.y), cv::Point2i(img.cols, pt.y), cv::Scalar(127,127,127), 1);
            }
            break;
        case DBID_V_SCANS:
            BOOST_FOREACH(const PointType& pt, data_points) {
                cv::line(img, cv::Point2i(pt.x, pt.y), cv::Point2i(pt.x, img.rows), cv::Scalar(127,127,127), 1);
            }
            break;
        case DBID_MATCHED_SEGMENTS:
            BOOST_FOREACH(const PointType& pt, data_points) {
                cv::circle(img, cv::Point2i(pt.x, pt.y), 1, cv::Scalar(255,255,0), 4);
            }
            break;
        case DBID_HORIZON:
            line(img, cv::Point2i(data_points.front().x, data_points.front().y), cv::Point2i(data_points.back().x, data_points.back().y), cv::Scalar(255,255,0), 1);
            break;
        case DBID_GREENHORIZON_SCANS:
            BOOST_FOREACH(const PointType& pt, data_points) {
                line(img, cv::Point2i(pt.x, kinematics_horizon.findYFromX(pt.x)), cv::Point2i(pt.x, img.rows), cv::Scalar(127,127,127), 1);
                //cv::circle(img, cv::Point2i(pt.x, pt.y), 1, cv::Scalar(127,127,127), 2);
                cv::circle(img, cv::Point2i(pt.x, pt.y), 1, cv::Scalar(255,0,255), 2);
            }
            break;
        case DBID_GREENHORIZON_FINAL:
            for(it=data_points.begin(); it<data_points.end(); it++) {
                if (it > data_points.begin()) {
                    line(img, cv::Point2i((it-1)->x, (it-1)->y), cv::Point2i(it->x, it->y), cv::Scalar(255,0,255), 2);
                }
                //cv::circle(img, cv::Point2i(it->x, it->y), 1, cv::Scalar(255,0,255), 2);
            }
            break;
        case DBID_OBJECT_POINTS:
            BOOST_FOREACH(const PointType& pt, data_points) {
                cv::circle(img, cv::Point2i(pt.x, pt.y), 1, cv::Scalar(0,0,255), 4);
            }
            break;
        default:
            errorlog << "DataWrapper::debugPublish - Called with invalid id" << endl;
            return false;
        }

        imshow(window, img);    //refresh this particular debug window
    }

    return true;
}

//! Outputs debug data to the appropriate external interface
bool DataWrapper::debugPublish(DEBUG_ID id, const SegmentedRegion& region)
{
    vector<PointType> data_points;
    vector<cv::Scalar> colours;
    map<DEBUG_ID, vector<pair<string, cv::Mat>* > >::iterator map_it;
    vector<PointType>::const_iterator it;
    vector<cv::Scalar>::const_iterator c_it;

    getPointsAndColoursFromSegments(region.getSegments(), colours, data_points);
    
    if(data_points.empty() || colours.empty()) {
        errorlog << "DataWrapper::debugPublish - empty vector DEBUG_ID = " << getIDName(id) << endl;
        return false;
    }

    //Get window assigned to id
    map_it = debug_map.find(id);
    if(map_it == debug_map.end()) {
        errorlog << "DataWrapper::debugPublish - Missing window definition DEBUG_ID = " << getIDName(id) << endl;
        return false;
    }

    //for all images
    for(int i=0; i<map_it->second.size(); i++) {
        cv::Mat& img = map_it->second[i]->second;    //get image from pair
        string& window = map_it->second[i]->first; //get window name from pair


    #if VISION_WRAPPER_VERBOSITY > 2
        debug << id << endl;
        debug << colours.front()[0] << "," << colours.front()[1] << "," << colours.front()[2] << "," << colours.front()[3] << "\t";
        debug << data_points << endl;
    #endif

        switch(id) {
        case DBID_SEGMENTS:
            c_it = colours.begin();
            for (it = data_points.begin(); it < data_points.end()-1; it+=2) {
                //draws a line between each consecutive pair of points of the corresponding colour
                line(img, cv::Point2i(it->x, it->y), cv::Point2i((it+1)->x, (it+1)->y), *c_it, 1);
                c_it++;
            }
            break;
        case DBID_FILTERED_SEGMENTS:
            c_it = colours.begin();
            for (it = data_points.begin(); it < data_points.end()-1; it+=2) {
                //draws a line between each consecutive pair of points of the corresponding colour
                line(img, cv::Point2i(it->x, it->y), cv::Point2i((it+1)->x, (it+1)->y), *c_it, 1);
                c_it++;
            }
            break;
        default:
            errorlog << "DataWrapper::debugPublish - Called with invalid id" << endl;
            return false;
        }

        imshow(window, img);    //refresh this particular debug window
    }

    return true;
}

bool DataWrapper::debugPublish(DEBUG_ID id, NUImage const* const img)
{
    map<DEBUG_ID, vector<pair<string, cv::Mat>* > >::iterator map_it;
    if(id != DBID_IMAGE) {
        errorlog << "DataWrapper::debugPublish - Called with invalid id" << endl;
        return false;
    }
    //Get window assigned to id
    map_it = debug_map.find(id);
    if(map_it == debug_map.end()) {
        errorlog << "DataWrapper::debugPublish - Missing window definition DEBUG_ID = " << getIDName(id) << endl;
        return false;
    }

    //for all images
    for(int i=0; i<map_it->second.size(); i++) {
        cv::Mat& img_map = map_it->second[i]->second;    //get image from pair
        string& window = map_it->second[i]->first; //get window name from pair

        unsigned char* ptr,
                r, g, b;

        for(int y=0; y<m_current_image->getHeight(); y++) {
            ptr = img_map.ptr<unsigned char>(y);
            for(int x=0; x<m_current_image->getWidth(); x++) {
                ColorModelConversions::fromYCbCrToRGB((*img)(x,y).y, (*img)(x,y).cb, (*img)(x,y).cr, r, g, b);
                ptr[3*x]   = b;
                ptr[3*x+1] = g;
                ptr[3*x+2] = r;
            }
        }
        imshow(window, img_map);
    }
    return true;
}

bool DataWrapper::updateFrame()
{
    switch(METHOD) {
    case CAMERA:
        m_current_image = m_camera->grabNewImage();   //force get new frame
        break;
    case STREAM:
        VisionConstants::loadFromFile(configname);
        if(!imagestrm.is_open()) {
            errorlog << "No image stream" << endl;
            return false;
        }
        try {
            imagestrm >> *m_current_image;
            //doGaussian(m_current_image);
        }
        catch(std::exception& e){
            cout << "Stream error - resetting: " << e.what() << endl;
            imagestrm.clear() ;
            imagestrm.seekg(0, ios::beg);
            imagestrm >> *m_current_image;
        }
        break;
    case FILE:
        cur_image = (cur_image+1)%num_images;
        stringstream strm;
        strm << GROUP_NAME << cur_image << GROUP_EXT;
        capture->open(strm.str());
        *capture >> m_current_image_cv;

        imshow("test - DataWrapperPC::line28", m_current_image_cv);

        generateImageFromMat(m_current_image_cv);
        break;
    }
    numFramesProcessed++;
    
    return true;
}

/**
*   @brief loads the colour look up table
*   @param filename The filename for the LUT stored on disk
*   @note Taken from original vision system
*/
bool DataWrapper::loadLUTFromFile(const string& fileName)
{
    return LUT.loadLUTFromFile(fileName);
}

void DataWrapper::ycrcb2ycbcr(cv::Mat *img_ycrcb)
{
    vector<cv::Mat> planes;
    split(*img_ycrcb, planes);
    cv::Mat temp = planes[0];
    planes[0] = planes[2];
    planes[2] = temp;
    merge(planes, *img_ycrcb);
}
