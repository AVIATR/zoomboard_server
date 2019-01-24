//
//  StreamWriter.hpp
//
//  Created by Ender Tekin on 1/22/19.
//
//

#ifndef __StreamWriter__
#define __StreamWriter__

#include <stdio.h>
#include <memory>
#include <vector>
#include "Stream.hpp"

struct AVCodecParameters;
struct AVFrame;
struct AVStream;

namespace avtools
{
    /// @class StreamWriter initializes and writes to an output file
    class StreamWriter
    {
    public:
        
        typedef std::unique_ptr<MtreamWriter> Handle;   ///< handle to this class
        
        /// Ctor that opens a stream
        /// @param[in] url stream URL
        /// @param[in] codecParam video codec parameters
        /// @param[in] timebase timebase for the video stream
        /// @param[in] allowExperimentalCodecs whether to allow experimental codecs
        /// @throw MMError if stream cannot be opened for writing
        StreamWriter(
            const std::string& url, 
            const AVCodecParameters& codecParam, 
            const TimeBaseType& timebase, 
            bool allowExperimentalCodecs=false
        );

        ///Dtor
        ~StreamWriter();
        
        /// Open a file for writing.
        /// @param[in] filename name of file to open
        /// @return a handle to the media writer, nullptr if file cannot be opened for writing
        static Handle Open(
            const std::string& url,
            const AVCodecParameters& codecParam, 
            const TimeBaseType& timebase, 
            bool allowExperimentalCodecs=false
        ) noexcept;

        /// @return opened video stream
        const AVStream* getStream() const;
        
        /// Writes a video frame to the stream. Write nullptr to close the stream
        /// @param[in] pFrame frame data to write
        void write( const AVFrame* pFrame);

    private:
        class Implementation;                       ///< implementation class
        std::unique_ptr<Implementation> pImpl_;     ///< ptr to implementation

    };  //::avtools::StreamWriter

}   //::avtools

#endif /* defined(__StreamWriter__) */
