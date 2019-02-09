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
        typedef std::unique_ptr<MediaReader> Handle;   ///< handle to mediareader

        /// Ctor that opens a file
        /// @param[in] url url of media file to open
        /// @throw std::runtime_exception if there was an error opening the stream.
        MediaReader(const std::string& url);

        /// More general ctor
        /// @param[in, out] opts stream options to use, such as url, resolution & frame rate.
        /// On return, this dictionary should contain the actual values used in opening.
        /// @throw std::runtime_exception if there was an error opening the stream.
        [[deprecated]]
        MediaReader(const AVDictionary& opts);

        /// More general ctor
        /// @param[in, out] opts stream options to use, such as url, resolution & frame rate.
        /// On return, this dictionary should contain the actual values used in opening.
        /// @throw std::runtime_exception if there was an error opening the stream.
        MediaReader(const Dict& opts);
        
        /// Dtor
        ~MediaReader();
        
        /// Opens a new file for reading
        /// @param[in] fileName name of file to open
        /// @return a new handle to the multimedia reader for the opened file, nullptr if a file could not be opened.
        static Handle Open(const std::string& fileName) noexcept;
        
        /// Opens a stream
        /// @param[in] opts AVDictionary instance that contains entries for the url to open, desired
        /// width, height etc for an input device, etc. it should have the options:
        /// * url: name of file, or name of device
        /// * driver: for a capture device, name of the driver to use (e.g., v4l2, avfoundation, etc.)
        /// * width: for a capture device, desired width. If device doesn't support this, a different value may be subbed
        /// * height: for a capture device, desired height. If device doesn't support this, a different value may be subbed
        /// @return a new handle to the multimedia reader for the opened file, nullptr if a file could not be opened.
        static Handle Open(const AVDictionary& opts) noexcept;

        static Handle Open(Dict& opts) noexcept;

        /// @return the first opened video stream
        const AVStream* getVideoStream() const;
        
        /// Reads a frame
        /// @param[out] pFrame pointer to frame. Will contain new frame upon return
        /// @return pointer to the stream that the frame is from. Will be nullptr when finished reading without errors.
        /// @throw std::exception if there was a problem reading frames.
        const AVStream* read(AVFrame const*& pFrame);
                
    private:
        class Implementation;
        std::unique_ptr<Implementation> pImpl_;
    };  //avtools::MediaReader
    
} //::avtools

#endif /* defined(__MediaReader_hpp__) */
