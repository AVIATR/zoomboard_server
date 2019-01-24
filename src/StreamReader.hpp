//
//  StreamReader.hpp
//
//  Also see http://roxlu.com/2014/039/decoding-h264-and-yuv420p-playback
//  Created by Ender Tekin on 1/22/19.
//  Copyright (c) 2014 Smith-Kettlewell. All rights reserved.
//

#ifndef __StreamReader_hpp__
#define __StreamReader_hpp__

#include <memory>
#include <string>

struct AVFrame;
struct AVStream;

namespace avtools
{
    /// @class opens a multimedia file for input
    /// See https://ffmpeg.org/doxygen/2.4/demuxing_decoding_8c-example.html#_a19
    class StreamReader
    {
    public:
        typedef std::unique_ptr<StreamReader> Handle;   ///< handle to mmreader
                
        /// Ctor that opens a file
        /// @param[in] streamName name of media file to open
        /// @throw std::runtime_exception if there was an error opening the stream.
        StreamReader(const std::string& streamName);
        
        /// Dtor
        ~StreamReader();
        
        /// Opens a new file for reading
        /// @param[in] streamName name of stream to open
        /// @return a new handle to the multimedia reader for the opened file, nullptr if a file could not be opened.
        static Handle Open(const std::string& streamName) noexcept;
        
        /// @return the decodable media streams found in the opened file. If no file is open, or no decodable streams are found, an empty vector is returned.
        // std::vector<const AVStream*> getOpenedStreams() const;
        
        /// @return the first opened audio stream
        // const AVStream* getFirstAudioStream() const;
        
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
    };  //avtools::StreamReader
    
} //::avtools

#endif /* defined(__StreamReader_hpp__) */
