//
//  Transcoder.hpp
//
//  Also see http://roxlu.com/2014/039/decoding-h264-and-yuv420p-playback
//  Created by Ender Tekin on 1/22/19.
//  Copyright (c) 2019 UW-Madison. All rights reserved.
//

#ifndef __Transcoder_hpp__
#define __Transcoder_hpp__

#include <memory>
#include "LibAVWrappers.hpp"

struct AVFrame;
struct AVCodecParameters;

namespace avtools
{
    /// @class Converts the video frames from one format to another
    class Transcoder
    {
    public:
        
        /// Ctor
        /// @param[in] inParam input codec parameters
        /// @param[in] outParam output codec parameters
        Transcoder(const AVCodecParameters& inParam, const AVCodecParameters& outParam);
        
        ///Dtor
        ~Transcoder();
        
        /// Transcodes a video frame.
        /// @param[in] pFrame input frame
        /// @return transcoded frame
        /// @throw StreamError if there are issues with transcoding
        const AVFrame* convert(const AVFrame* pFrame);

        
    private:
        class Implementation;                           ///< Implementation class
        std::unique_ptr<Implementation> pImpl_;         ///< Ptr to implementation
    };  //avtools::Transcoder
    
} //::avtools

#endif /* defined(__Transcoder_hpp__) */
