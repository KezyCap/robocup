#include <boost/foreach.hpp>
#include "datawrapperpc.h"
#include "Infrastructure/NUImage/ColorModelConversions.h"
#include "debug.h"
#include "nubotdataconfig.h"

#include "Vision/VisionTypes/coloursegment.h"

DataWrapper* DataWrapper::instance = 0;

string DataWrapper::getIDName(DATA_ID id) {
    switch(id) {
    case DID_IMAGE:
        return "DID_IMAGE";
    case DID_CLASSED_IMAGE:
        return "DID_CLASSED_IMAGE";
    default:
        return "NOT VALID";
    }
}

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
    case DBID_TRANSITIONS:
        return "DBID_TRANSITIONS";
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

void getPointsAndColoursFromSegments(const vector< vector<ColourSegment> >& segments, vector<Scalar>& colours, vector<PointType>& pts)
{
    unsigned char r, g, b;
    
    BOOST_FOREACH(const vector<ColourSegment>& line, segments) {
        BOOST_FOREACH(const ColourSegment& seg, line) {
            ClassIndex::getColourAsRGB(seg.getColour(), r, g, b);
            pts.push_back(seg.getStart());
            pts.push_back(seg.getEnd());
            colours.push_back(Scalar(b,g,r));
        }
    }
}

DataWrapper::DataWrapper()
{
    //slider window
    saving_images_slider_val = 0;
    cv::namedWindow("Save Images");
    cv::createTrackbar("Save Images", "Save Images", &saving_images_slider_val, 1);

    //frame grab methods
    switch(METHOD) {
    case CAMERA:
        m_camera = new PCCamera();
        m_current_image = m_camera->grabNewImage();
        LUTname = string(getenv("HOME")) +  string("/nubot/default.lut");
        break;
    case STREAM:
        streamname = string(getenv("HOME")) +  string("/nubot/image.strm");
        imagestrm.open(streamname.c_str());
        m_current_image = new NUImage();
        if(imagestrm.is_open()) {
            imagestrm >> *m_current_image;
        }
        else {
            errorlog << "DataWrapper::DataWrapper() - failed to load stream: " << streamname << endl;
        }
        LUTname = string(getenv("HOME")) +  string("/nubot/default.lut");
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
    debug_window_num = 2;
    debug_windows = new pair<string, Mat>[debug_window_num];
    debug_windows[0].first = "Debug1";
    debug_windows[1].first = "Debug2";
    for(int i=0; i<debug_window_num; i++) {
        namedWindow(debug_windows[i].first, CV_WINDOW_KEEPRATIO);
        debug_windows[i].second.create(m_current_image->getHeight(), m_current_image->getWidth(), CV_8UC3);
    }

    //create map into images
    debug_map[DBID_IMAGE]               = &debug_windows[0];
    debug_map[DBID_H_SCANS]             = &debug_windows[0];
    debug_map[DBID_V_SCANS]             = &debug_windows[0];
    debug_map[DBID_SEGMENTS]            = &debug_windows[0];
    debug_map[DBID_TRANSITIONS]         = &debug_windows[0];
    debug_map[DBID_HORIZON]             = &debug_windows[0];
    debug_map[DBID_GREENHORIZON_SCANS]  = &debug_windows[0];
    debug_map[DBID_GREENHORIZON_FINAL]  = &debug_windows[0];
    debug_map[DBID_OBJECT_POINTS]       = &debug_windows[0];
    debug_map[DBID_FILTERED_SEGMENTS]   = &debug_windows[1];
    debug_map[DBID_GOALS]               = &debug_windows[0];
    debug_map[DBID_BEACONS]             = &debug_windows[0];
    debug_map[DBID_BALLS]               = &debug_windows[0];
    debug_map[DBID_OBSTACLES]           = &debug_windows[0];
    
    results_window_name = "Results";
    namedWindow(results_window_name, CV_WINDOW_KEEPRATIO);
    results_img.create(m_current_image->getHeight(), m_current_image->getWidth(), CV_8UC3);
    
    numFramesDropped = numFramesProcessed = 0;

    numSavedImages = 0;
    isSavingImages = saving_images_slider_val == 1;

}

DataWrapper::~DataWrapper()
{
}

void DataWrapper::generateImageFromMat(Mat& frame)
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

//! @brief Returns spoofed kinematics horizon.
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

//! Outputs supply data to the appropriate external interface
void DataWrapper::publish(DATA_ID id, const Mat &img)
{
    switch(id) {
    case DID_IMAGE:
        //NOT IMPLEMENTED YET
        break;
    case DID_CLASSED_IMAGE:
        results_img = img;
        imshow(results_window_name, results_img);
        break;
    }
}

void DataWrapper::publish(const vector<const VisionFieldObject*> &visual_objects)
{

}

void DataWrapper::publish(const VisionFieldObject* visual_object)
{
    
}

//! Outputs debug data to the appropriate external interface
void DataWrapper::debugRefresh()
{
    for(int i=0; i<debug_window_num; i++) {
        debug_windows[i].second = Scalar(0,0,0);
    }
    
    LUT.classifyImage(*m_current_image, results_img);
    imshow(results_window_name, results_img);
}

bool DataWrapper::debugPublish(vector<Ball> data) {
    #if VISION_WRAPPER_VERBOSITY > 1
        if(data.empty()) {
            debug << "DataWrapper::debugPublish - empty vector DEBUG_ID = " << getIDName(DBID_BALLS) << endl;
            return false;
        }
    #endif
    
    Mat& img = results_img;    //get image from pair
    string& window = results_window_name; //get window name from pair
    
    BOOST_FOREACH(Ball b, data) {
        circle(img, cv::Point2i(b.getLocationPixels().x, b.getLocationPixels().y), b.getRadius(), Scalar(255,255,0), 2);
    }

    imshow(window, img);    //refresh this particular debug window
    return true;
}

bool DataWrapper::debugPublish(vector<Beacon> data) {
    
#if VISION_WRAPPER_VERBOSITY > 1
    if(data.empty()) {
        debug << "DataWrapper::debugPublish - empty vector DEBUG_ID = " << getIDName(DBID_GOALS) << endl;
        return false;
    }
#endif
    
    Mat& img = results_img;    //get image from pair
    string& window = results_window_name; //get window name from pair
    PointType bl, tr;

    BOOST_FOREACH(Beacon b, data) {
        bl = b.getQuad().getBottomLeft();
        tr = b.getQuad().getTopRight();
        if(b.getID() == Beacon::BlueBeacon)
            rectangle(img, cv::Point2i(bl.x, bl.y), cv::Point2i(tr.x, tr.y), Scalar(255,0,255), 2, 8, 0);
        else if(b.getID() == Beacon::YellowBeacon)
            rectangle(img, cv::Point2i(bl.x, bl.y), cv::Point2i(tr.x, tr.y), Scalar(255,255,0), 2, 8, 0);
        else
            rectangle(img, cv::Point2i(bl.x, bl.y), cv::Point2i(tr.x, tr.y), Scalar(255,255,255), 2, 8, 0);
    }
    
    imshow(window, img);    //refresh this particular debug window
    return true;
}

bool DataWrapper::debugPublish(vector<Goal> data) {

#if VISION_WRAPPER_VERBOSITY > 1
    if(data.empty()) {
        debug << "DataWrapper::debugPublish - empty vector DEBUG_ID = " << getIDName(DBID_GOALS) << endl;
        return false;
    }
#endif
    
    Mat& img = results_img;    //get image from pair
    string& window = results_window_name; //get window name from pair
    
    PointType bl, tr;

    BOOST_FOREACH(Goal post, data) {
        bl = post.getQuad().getBottomLeft();
        tr = post.getQuad().getTopRight();
        if(post.getID() == Goal::BlueLeftGoal || post.getID() == Goal::BlueRightGoal || post.getID() == Goal::BlueUnknownGoal)
            rectangle(img, cv::Point2i(bl.x, bl.y), cv::Point2i(tr.x, tr.y), Scalar(0,255,255), 2, 8, 0);
        else
            rectangle(img, cv::Point2i(bl.x, bl.y), cv::Point2i(tr.x, tr.y), Scalar(255,0,0), 2, 8, 0);
    }
    
    imshow(window, img);    //refresh this particular debug window
    return true;
}

bool DataWrapper::debugPublish(vector<Obstacle> data) {
    
    return false;
}

bool DataWrapper::debugPublish(DEBUG_ID id, const vector<PointType>& data_points)
{
    map<DEBUG_ID, pair<string, Mat>* >::iterator map_it;
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

    Mat& img = map_it->second->second;    //get image from pair
    string& window = map_it->second->first; //get window name from pair


#if VISION_WRAPPER_VERBOSITY > 2
    debug << id << endl;
    debug << colour[0] << "," << colour[1] << "," << colour[2] << "," << colour[3] << "\t";
    debug << data_points << endl;
#endif

    switch(id) {
    case DBID_H_SCANS:
        BOOST_FOREACH(const PointType& pt, data_points) {
            line(img, cv::Point2i(0, pt.y), cv::Point2i(img.cols, pt.y), Scalar(127,127,127), 1);
        }
        break;
    case DBID_V_SCANS:
        BOOST_FOREACH(const PointType& pt, data_points) {
            line(img, cv::Point2i(pt.x, pt.y), cv::Point2i(pt.x, img.rows), Scalar(127,127,127), 1);
        }
        break;
    case DBID_TRANSITIONS:
        BOOST_FOREACH(const PointType& pt, data_points) {
            circle(img, cv::Point2i(pt.x, pt.y), 1, Scalar(255,255,0), 4);
        }
        break;
    case DBID_HORIZON:
        line(img, cv::Point2i(data_points.front().x, data_points.front().y), cv::Point2i(data_points.back().x, data_points.back().y), Scalar(0,255,255), 1);
        break;
    case DBID_GREENHORIZON_SCANS:
        BOOST_FOREACH(const PointType& pt, data_points) {
            line(img, cv::Point2i(pt.x, 0), cv::Point2i(pt.x, img.rows), Scalar(127,127,127), 1);
            circle(img, cv::Point2i(pt.x, pt.y), 1, Scalar(127,127,127), 2);
        }
        break;
    case DBID_GREENHORIZON_FINAL:
        for(it=data_points.begin(); it<data_points.end(); it++) {
            if (it > data_points.begin()) {
                line(img, cv::Point2i((it-1)->x, (it-1)->y), cv::Point2i(it->x, it->y), Scalar(255,0,255), 1);
            }
            circle(img, cv::Point2i(it->x, it->y), 1, Scalar(255,0,255), 2);
        }
        break;
    case DBID_OBJECT_POINTS:
        BOOST_FOREACH(const PointType& pt, data_points) {
            circle(img, cv::Point2i(pt.x, pt.y), 1, Scalar(0,0,255), 4);
        }
        break;
    default:
        errorlog << "DataWrapper::debugPublish - Called with invalid id" << endl;
        return false;
    }

    imshow(window, img);    //refresh this particular debug window

    return true;
}

//! Outputs debug data to the appropriate external interface
bool DataWrapper::debugPublish(DEBUG_ID id, const SegmentedRegion& region)
{
    vector<PointType> data_points;
    vector<Scalar> colours;
    map<DEBUG_ID, pair<string, Mat>* >::iterator map_it;
    vector<PointType>::const_iterator it;
    vector<Scalar>::const_iterator c_it;

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

    Mat& img = map_it->second->second;    //get image from pair
    string& window = map_it->second->first; //get window name from pair


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

    return true;
}

bool DataWrapper::debugPublish(DEBUG_ID id, const Mat &img)
{
    map<DEBUG_ID, pair<string, Mat>* >::iterator map_it;

    //Get window assigned to id
    //map_it = debug_map.find(id);
    if(map_it == debug_map.end()) {
        errorlog << "DataWrapper::debugPublish - Missing window definition DEBUG_ID = " << getIDName(id) << endl;
        return false;
    }

    Mat& img_map = map_it->second->second;    //get image from pair
    string& window = map_it->second->first; //get window name from pair

    switch(id) {
    case DBID_IMAGE:
        img_map = img;
        imshow(window, img_map);
        return true;
    default:
        errorlog << "DataWrapper::debugPublish - Called with invalid id" << endl;
        return false;
    }

}

bool DataWrapper::updateFrame()
{
    numFramesProcessed++;
    bool newIsSavingImages = saving_images_slider_val;
    
    switch(METHOD) {
    case CAMERA:
        m_current_image = m_camera->grabNewImage();   //force get new frame
        if(isSavingImages != newIsSavingImages) {
            //if the job changes the saving images state
            if(newIsSavingImages) {
                //we weren't saving and now we've started
                if (!imagefile.is_open()) {
                    cout << "Openning " << string(DATA_DIR) + string("image.strm") << endl;
                    imagefile.open((string(DATA_DIR) + string("image.strm")).c_str());
                }
            }
            else {
                //we were saving and now we've finished
                cout << "Flushing " << string(DATA_DIR) + string("image.strm") << endl;
                imagefile.flush();
            }
        }
        isSavingImages = newIsSavingImages;

        if(isSavingImages)
            saveAnImage();
        break;
    case STREAM:
        try {
            imagestrm >> *m_current_image;
        }
        catch(std::exception& e){
            cout << "Stream error - resetting: " << e.what() << endl;
            imagestrm.clear() ;
            imagestrm.seekg(0, ios::beg) ;
            imagestrm >> *m_current_image;
        }
        break;
    case FILE:
        char c = cv::waitKey(10);
        if(c > 0) {
            cur_image = (cur_image+1)%num_images;
            stringstream strm;
            strm << GROUP_NAME << cur_image << GROUP_EXT;
            capture->open(strm.str());
            *capture >> m_current_image_cv;

            imshow("test - DataWrapperPC::line28", m_current_image_cv);

            generateImageFromMat(m_current_image_cv);
        }
        break;
    }
    
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

/**
*   @brief Saves an image and the current sensor data to the associated streams.
*   @note Taken from original vision system
*/
void DataWrapper::saveAnImage()
{
    #if VISION_WRAPPER_VERBOSITY > 1
        debug << "DataWrapper::SaveAnImage(). Starting..." << endl;
    #endif

    if (!imagefile.is_open())
        imagefile.open((string(DATA_DIR) + string("image.strm")).c_str());

    if (imagefile.is_open() and numSavedImages < 2500)
    {
        NUImage buffer;
        buffer.cloneExisting(*m_current_image);
        imagefile << buffer;
        numSavedImages++;

    }
    #if VISION_WRAPPER_VERBOSITY > 1
        debug << "DataWrapper::SaveAnImage(). Finished" << endl;
    #endif
}

void DataWrapper::ycrcb2ycbcr(Mat *img_ycrcb)
{
    vector<Mat> planes;
    split(*img_ycrcb, planes);
    Mat temp = planes[0];
    planes[0] = planes[2];
    planes[2] = temp;
    merge(planes, *img_ycrcb);
}
