//
//  MediaReader.cpp
//  MediaReader
//
//  Created by Ender Tekin on 10/8/14.
//  Copyright (c) 2014 Smith-Kettlewell. All rights reserved.
//

#include "MediaReader.hpp"
#include "Media.hpp"
#include "LibAVWrappers.hpp"
#include "log4cxx/logger.h"
#include <memory>
#include <stdexcept>
//#include <iostream>

extern "C" {
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>
#include <libavutil/frame.h>
#include <libavdevice/avdevice.h>
}

using avtools::MediaError;

namespace
{
#ifdef __APPLE__ // MacOS
    static const std::string INPUT_DRIVER = "avfoundation";
#elif defined __gnu_linux__ // GNU/Linux
    static const std::string INPUT_DRIVER = "v4l2";
#elif defined _WIN32 //Windows
    #error "Windows inputs not yet implemented."
#else
    #error "Unknown operating system"
#endif

    log4cxx::LoggerPtr logger(log4cxx::Logger::getLogger("zoombrd.MediaReader"));

}

namespace avtools
{
    //==========================================
    //
    // MediaReader Implementation
    //
    //==========================================
    
    /// @class MediaReader Implementation
    /// The code is based on https://github.com/chelyaev/ffmpeg-tutorial
    /// which is an updated (as of 02/13/2014) version of Stephen Dranger's original tutorials
    /// The originals can be found at http://dranger.com/ffmpeg/
    /// Also see https://stackoverflow.com/questions/35340437/how-can-i-use-avformat-open-input-function-ffmpeg for opening input devices
    class MediaReader::Implementation
    {
    private:
        FormatContext               formatCtx_;        ///< Format I/O context
        CodecContext                codecCtx_;         ///< codec context for the video codec context of opened stream
        Packet                      pkt_;              ///< packet to be used for reading from file
        int                         stream_;           ///< Index of the opened video stream

    public:
        /// Ctor
        /// @param[in] url url or filename to open
        /// @param[in] pFmt ptr to input format. If none is provided, it is guessed from the url
        /// @param[in] opts a ddictionary that has entries used for opening the url, such as framerate.
        Implementation(const std::string& url, avtools::Dictionary& muxerOpts):
        formatCtx_(FormatContext::INPUT),
        codecCtx_((AVCodec*) nullptr),
        pkt_(),
        stream_(-1)
        {
            avdevice_register_all();
            AVInputFormat* pFormat = nullptr;
            if (muxerOpts.has("name"))
            {
                const std::string demuxer = muxerOpts["name"];
                pFormat = av_find_input_format(demuxer.c_str());
                if (!pFormat)
                {
                    throw MediaError("Cannot determine input format for " + demuxer);
                }
            }
            LOG4CXX_DEBUG(logger, "Opening " << pFormat->long_name );

            int ret = avformat_open_input( &formatCtx_.get(), url.c_str(), pFormat, &muxerOpts.get() );
            if( ret < 0 )  // Couldn't open file
            {
                throw MediaError("Could not open " + url, ret);
            }
#ifndef NDEBUG
            LOG4CXX_DEBUG(logger, "Unused demuxer options:\n" << muxerOpts.as_string());
            {
                avtools::CharBuf buf;
                ret = av_opt_serialize(formatCtx_.get(), AV_OPT_FLAG_DECODING_PARAM, 0, &buf.get(), ':', '\n');
                if (ret < 0)
                {
                    throw avtools::MediaError("Unable to serialize demuxer options", ret);
                }
                LOG4CXX_DEBUG(logger, "Available demuxer options:\n" << buf.get());
            }
            {
                avtools::CharBuf buf;
                ret = av_opt_serialize(formatCtx_->priv_data, AV_OPT_FLAG_DECODING_PARAM, 0, &buf.get(), ':', '\n');
                if (ret < 0)
                {
                    throw avtools::MediaError("Unable to serialize private demuxer options", ret);
                }
                LOG4CXX_DEBUG(logger, "Available private demuxer options:\n" << buf.get());
            }
#endif

            // Retrieve stream information
            ret = avformat_find_stream_info(formatCtx_.get(), nullptr);
            if( ret < 0 ) // Couldn't find stream information
            {
                throw MediaError("Could not find stream information", ret);
            }
            const int nStreams = formatCtx_->nb_streams;
            LOG4CXX_DEBUG(logger, "MediaReader: Format context found.\n\tStart time = " << formatCtx_->start_time << "\n\t#streams = " << nStreams);

            // Open the first found video stream
            for (int i = 0; i < nStreams; ++i)
            {
                AVStream* pStream = formatCtx_->streams[i];
                assert(pStream && pStream->codecpar);
                const AVMediaType type = pStream->codecpar->codec_type;
                if ( type != AVMEDIA_TYPE_VIDEO )
                {
                    LOG4CXX_DEBUG(logger, "MediaReader: Skipping unprocessed stream " << *pStream);
                    continue;
                }
                // Open codec
                AVCodecID id = pStream->codecpar->codec_id;
                AVCodec* pCodec = avcodec_find_decoder(id);
                if (!pCodec)
                {
                    throw MediaError("Unable to find decoder for " + std::to_string(id));
                }
                int ret = avcodec_parameters_to_context(codecCtx_.get(), pStream->codecpar);
                if (ret < 0)
                {
                    throw MediaError("Unable to initialize decoder context " , ret);
                }

                Dictionary codecOpts;
                codecOpts.add("refcounted_frames", 1);
                ret = avcodec_open2(codecCtx_.get(), pCodec, &codecOpts.get());
                if (ret < 0)
                {
                    throw MediaError("Unable to open decoder" , ret);
                }
                codecCtx_->time_base = pStream->time_base;
#ifndef NDEBUG
                LOG4CXX_DEBUG(logger, "Unused decoder options: " << codecOpts.as_string());
                {
                    avtools::CharBuf buf;
                    ret = av_opt_serialize(codecCtx_.get(), AV_OPT_FLAG_DECODING_PARAM, 0, &buf.get(), ':', '\n');
                    if (ret < 0)
                    {
                        throw avtools::MediaError("Unable to serialize decoder options", ret);
                    }
                    LOG4CXX_DEBUG(logger, "Available decoder options:\n" << buf.get());
                }
                LOG4CXX_DEBUG(logger, "Unused decoder private options: " << codecOpts.as_string());
                {
                    avtools::CharBuf buf;
                    ret = av_opt_serialize(codecCtx_->priv_data, AV_OPT_FLAG_DECODING_PARAM, 0, &buf.get(), ':', '\n');
                    if (ret < 0)
                    {
                        throw avtools::MediaError("Unable to serialize decoder private options", ret);
                    }
                    LOG4CXX_DEBUG(logger, "Available decoder private options:\n" << buf.get());
                }
#endif

                LOG4CXX_DEBUG(logger, "MediaReader: Opened decoder for stream:\n" << *pStream);
                stream_ = i;
                break;
            }
            if (stream_ < 0)
            {
                throw MediaError("Unable to open any video streams");
            }
        }

        /// @return a list of opened streams
        const AVStream* stream() const
        {
            assert(formatCtx_);
            assert( (stream_ >= 0) && (stream_ < formatCtx_->nb_streams) );
            return formatCtx_->streams[stream_];
        }

        /// Dtor
        ~Implementation() = default;

        /// Reads a frame
        /// @param[out] pFrame pointer to frame. Will contain new frame upon return
        /// @return pointer to the stream that the frame is from. Will be nullptr when finished reading without errors.
        /// @throw MMError if there was a problem reading frames.
        const AVStream* read(Frame& frame)
        {
            assert(stream_ >= 0);    //Otherwise stream is closed and we shouldn't be calling this
            int ret;
            int stream = pkt_->stream_index;
            if (stream < 0)    //read more packets
            {
                ret = av_read_frame(formatCtx_.get(), pkt_.get()); //read a new packet
                if (AVERROR_EOF == ret)
                {
                    LOG4CXX_DEBUG(logger, "Reached end of file. Closing.");
                    return nullptr;
                }
                else if (ret < 0)
                {
                    throw MediaError("Error reading packets", ret);
                }
                stream = pkt_->stream_index;
                if ( stream != stream_ )   //no decoder open for this stream. Skip & read more packets
                {
                    LOG4CXX_DEBUG(logger, "Skipping packet from undecoded stream.");
                    pkt_.unref();
                    return read(frame);
                }
                //Valid packet & decoder - send packet to decoder
                ret = avcodec_send_packet(codecCtx_.get(), pkt_.get());
                if (ret < 0)
                {
                    throw MediaError("Unable to decode packet", ret);
                }
            }
            // Receive decoded frame if available
            assert (stream == stream_);
            ret = avcodec_receive_frame(codecCtx_.get(), frame.get());
            switch (ret)
            {
                case 0:
                    if (frame->pts == AV_NOPTS_VALUE)
                    {
                        frame->pts = frame->best_effort_timestamp;
                    }
                    frame.type = AVMEDIA_TYPE_VIDEO;
                    return formatCtx_->streams[stream_];
                case AVERROR(EAGAIN):   //more packets need to be read & sent to the decoder before decoding
                    pkt_.unref();
                    return read(frame);
                case AVERROR(EOF):
                    LOG4CXX_INFO(logger, "End of file or stream.");
                    return nullptr;
                case AVERROR(EINVAL):
                    throw MediaError("Codec not opened, or it is an encoder");
                default:
                    throw MediaError("Decoding error.", ret);
            }
        }
    };  // avtools::MediaReader::Implementation

    //==========================================
    //
    // MediaReader Definitions
    //
    //==========================================

    MediaReader::MediaReader(const std::string& url, Dictionary& opts):
    pImpl_( new Implementation(url, opts) )
    {
        assert( pImpl_ );
    }

    MediaReader::~MediaReader() = default;
    
    const AVStream* MediaReader::read(Frame & frame)
    {
        assert(pImpl_);
        try
        {
            const AVStream* pStr = pImpl_->read(frame);
            if (!pStr) //eof
            {
                LOG4CXX_DEBUG(logger, "End of stream reached. closing MediaReader.");
                pImpl_.reset(nullptr);
            }
            return pStr;
        }
        catch (std::exception& err)
        {
            pImpl_.reset(nullptr);
            std::throw_with_nested( MediaError("MediaReader: Unable to read frames.") );
        }
    }

    const AVStream* MediaReader::getVideoStream() const
    {
        assert(pImpl_);
        return pImpl_->stream();
    }

}   //::avtools
