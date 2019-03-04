//
//  StreamReader.hpp
//
//  Also see http://roxlu.com/2014/039/decoding-h264-and-yuv420p-playback
//  Created by Ender Tekin on 1/22/19.
//  Copyright (c) 2014 Smith-Kettlewell. All rights reserved.
//

#ifndef __MediaReader_hpp__
#define __MediaReader_hpp__

#include <memory>
#include <string>
#include "LibAVWrappers.hpp"

struct AVFrame;
struct AVStream;
struct AVDictionary;

namespace avtools
{
    /// @class opens a multimedia file for input
    /// See https://ffmpeg.org/doxygen/2.4/demuxing_decoding_8c-example.html#_a19
    class MediaReader
    {
    public:
        /// Input type
        enum InputType
        {
            CAPTURE_DEVICE = 0,
            FILE = 1
        };

        /// Ctor that opens a file
        /// @param[in] url url of media file to open
        /// @param[in] type whether the input is a file or a capture device
        /// @throw std::runtime_exception if there was an error opening the stream.
        MediaReader(const std::string& url, InputType type=FILE);

        /// More general ctor
        /// @param[in] url url of media file to open
        /// @param[in, out] opts stream options to use, such as resolution & frame rate.
        /// @param[in] type whether the input is a file or a capture device
        /// On return, this dictionary should contain the actual values used in opening.
        /// @throw std::runtime_exception if there was an error opening the stream.
        MediaReader(const std::string& url, Dictionary& opts, InputType type=FILE);
        
        /// Dtor
        ~MediaReader();
        
        /// @return the first opened video stream
        const AVStream* getVideoStream() const;
        

        /// Reads a frame
        /// @param[out] pFrame pointer to frame. Will contain new frame upon return
        /// @return pointer to the stream that the frame is from. Will be nullptr when finished reading without errors.
        /// @throw std::exception if there was a problem reading frames.
        const AVStream* read(Frame &frame);

    private:
        class Implementation;
        std::unique_ptr<Implementation> pImpl_;
    };  //avtools::MediaReader
    
} //::avtools

#endif /* defined(__MediaReader_hpp__) */
