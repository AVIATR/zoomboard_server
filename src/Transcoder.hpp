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
        
        /// Pushes a video frame to be transcoded. After pushing, use pop() to see if
        /// there are transcoded frames available.
        /// There may be buffered frames. Push nullptr to return these with pop().
        /// @param[in] pFrame input frame
        /// @throw StreamError if there are issues with transcoding
        void push(const AVFrame* pFrame);
        
        /// Pops a video frame from the transcoder if one is available.
        /// If no frames are yet available, returns nullptr
        /// @return pointer to transcoded video frame
        /// @throw StreamError if there are issues with transcoding.
        const AVFrame* pop();
    private:
        class Implementation;                           ///< Implementation class
        std::unique_ptr<Implementation> pImpl_;         ///< Ptr to implementation
    };  //avtools::Transcoder
    
} //::avtools

#endif /* defined(__Transcoder_hpp__) */
