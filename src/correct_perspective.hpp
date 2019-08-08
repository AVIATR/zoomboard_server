//
//  correct_perspective.hpp
//  zoomboard_server
//
//  Created by Ender Tekin on 8/7/19.
//

#ifndef correct_perspective_hpp
#define correct_perspective_hpp

#include "ThreadsafeFrame.hpp"
#include <memory>
#include <string>
#include <opencv2/core.hpp>

/// Uses Aruco markers on the four corners of the board to undo perspective transform.
/// This can then be used with cv::warpPerspective to correct the perspective of the video.
/// also @see https://docs.opencv.org/3.1.0/da/d54/group__imgproc__transform.html
/// @param[in] pFrame input frame that will be updated by the reader thread
/// @param[in] calibrationFile calibration file that contains info re: camera calibration and aruco markers
/// @return a perspective transformation matrix
/// TODO: Should also return the roi so we send a smaller framesize if need be (no need to send extra data)
cv::Mat_<double> getPerspectiveTransformationMatrixFromMarkers(std::weak_ptr<const avtools::ThreadsafeFrame> pFrame, const std::string& calibrationFile);

/// Asks the user to choose the corners of the board and undoes the
/// perspective transform. This can then be used with
/// cv::warpPerspective to correct the perspective of the video.
/// also @see https://docs.opencv.org/3.1.0/da/d54/group__imgproc__transform.html
/// @param[in] pFrame input frame that will be updated by the reader thread
/// @return a perspective transformation matrix
/// TODO: Should also return the roi so we send a smaller framesize if need be (no need to send extra data)
cv::Mat_<double> getPerspectiveTransformationMatrixFromUser(std::weak_ptr<const avtools::ThreadsafeFrame> pFrame);


#endif /* correct_perspective_hpp */
