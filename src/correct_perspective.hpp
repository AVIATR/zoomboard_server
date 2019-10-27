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
#include <thread>

/// Launches a thread that creates a warped matrix of the input frame according to the given transform matrix
/// @param[in] pInFrame input frame
/// @param[in, out] pWarpedFrame transformed output frame
/// @param[in] trfMatrix transform matrix
/// @return a new thread that runs in the background, updates the warpedFrame when a new inFrame is available.
std::thread threadedWarp(std::weak_ptr<const avtools::ThreadsafeFrame> pInFrame, std::weak_ptr<avtools::ThreadsafeFrame> pWarpedFrame, const std::string& calibrationFile);

#endif /* correct_perspective_hpp */
