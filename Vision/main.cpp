#include <iostream>

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>

#include "VisionWrapper/visioncontrolwrapperpc.h"

using namespace std;
using namespace cv;

int main(void)
{
    VisionControlWrapper* vision = VisionControlWrapper::getInstance();
    //DataWrapper* visiondata = DataWrapper::getInstance();
    char c=0;
    int error=0;
    bool look_for_ball = false;
    while(c!=27 && error==0) {
        //visiondata->updateFrame();
//        error = vision->runFrame();
//        c = waitKey(1);
        error = vision->runFrame(look_for_ball);
        c = waitKey(1);
        if(c == 32) {
            c = 0;
            while(c!=27 && c!=32) {
                c = waitKey(1);
            }
        }
    }
    if(error != 0)
        cout << "Error: " << error << endl;
}
