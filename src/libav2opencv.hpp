//
//  libav2opencv.hpp
//  zoomboard_server
//
//  Created by Ender Tekin on 8/7/19.
//

#ifndef libav2opencv_h
#define libav2opencv_h
#include <opencv2/core.hpp>
#include "LibAVWrappers.hpp"

/// Wraps a cv::mat around libav frames. Note that the matrix is just wrapped around the
/// existing data, so data is not cloned. Make sure that the matrix is done being used before reading new
/// data into the frame.
/// @param[in] frame a decoded video frame
/// @return a cv::mat wrapper around the frame data
inline const cv::Mat getImage(const avtools::Frame& frame)
{
    return cv::Mat(frame->height, frame->width, CV_8UC3, frame->data[0], frame->linesize[0]);
}

/// Wraps a cv::mat around libav frames. Note that the matrix is just wrapped around the
/// existing data, so data is not cloned. Make sure that the matrix is done being used before reading new
/// data into the frame.
/// @param[in] frame a decoded video frame
/// @return a cv::mat wrapper around the frame data
inline cv::Mat getImage(avtools::Frame& frame)
{
    return cv::Mat(frame->height, frame->width, CV_8UC3, frame->data[0], frame->linesize[0]);
}

/// Pixel format to process images in OpenCV
static const AVPixelFormat PIX_FMT = AVPixelFormat::AV_PIX_FMT_BGR24;

#endif /* libav2opencv_h */
