//
//  MediaWriter.hpp
//
//  Created by Ender Tekin on 1/22/19.
//
//

#ifndef __MediaWriter__
#define __MediaWriter__

#include <stdio.h>
#include <memory>
#include <vector>
#include "Media.hpp"
#include "LibAVWrappers.hpp"

struct AVCodecParameters;
struct AVFrame;
struct AVStream;

namespace avtools
{
    /// @class MediaWriter initializes and writes to an output file
    class MediaWriter
    {
    public:

        /// Ctor that opens a stream
        /// @param[in] url stream URL
        /// @param[in] codecParam video codec parameters
        /// @param[in] timebase timebase for the video stream
        /// @param[in] opts a dictionary with optional stream & codec
        /// @throw StreamError if stream cannot be opened for writing
        [[deprecated]]
        MediaWriter(
            const std::string& url,
            const CodecParameters& codecParam,
            const TimeBaseType& timebase,
            Dictionary& opts
        );

        MediaWriter(
            const std::string& url,
            Dictionary& codecOpts,
            Dictionary& muxerOpts
        );

        ///Dtor
        ~MediaWriter();
        
        /// @return opened video stream
        const AVStream* getStream() const;
        
        /// Writes a video frame to the stream. Write nullptr to close the stream
        /// @param[in] pFrame frame data to write
        /// @param[in] timebase for the incoming frames
        void write(const AVFrame* pFrame, TimeBaseType timebase);

        /// Writes a video frame to the stream. Write nullptr to close the stream
        /// @param[in] Frame frame data to write
        void write(const Frame& frame);

    private:
        class Implementation;                       ///< implementation class
        std::unique_ptr<Implementation> pImpl_;     ///< ptr to implementation

    };  //::avtools::MediaWriter

}   //::avtools

#endif /* defined(__MediaWriter__) */
