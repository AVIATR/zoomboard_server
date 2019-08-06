//
//  common.cpp
//  zoomboard_server
//
//  Created by Ender Tekin on 8/6/19.
//

#include "common.hpp"
#include "opencv2/core.hpp"
#include <iostream>
#include <stdexcept>

std::string type2str(int type)
{
    std::string r = "CV_";

//        uchar depth = type & CV_MAT_DEPTH_MASK;
//        uchar chans = 1 + (type >> CV_CN_SHIFT);
    int depth = CV_MAT_DEPTH(type);
    int chans = CV_MAT_CN(type);
    switch ( depth ) {
        case CV_8U:  r += "8UC"; break;
        case CV_8S:  r += "8SC"; break;
        case CV_16U: r += "16UC"; break;
        case CV_16S: r += "16SC"; break;
        case CV_32S: r += "32SC"; break;
        case CV_32F: r += "32FC"; break;
        case CV_64F: r += "64FC"; break;
        default:     r = "UserC"; break;
    }

//        r += (chans+'0');
    r += std::to_string(chans);
    return r;
}

bool promptYesNo(const std::string& prompt)
{
    char answer;
    do
    {
        std::cout << prompt << " (Y/N)\n";
        std::cin >> answer;
    }
    while( !std::cin.fail() && (answer != 'y') && (answer != 'Y')&& (answer != 'n') && (answer != 'N') );

    if ((answer == 'y') || (answer == 'Y'))
    {
        return true;
    }
    else if ((answer == 'n') || (answer == 'N'))
    {
        return false;
    }
    else
    {
        throw std::runtime_error("Failed reading prompt response.");
    }
}
