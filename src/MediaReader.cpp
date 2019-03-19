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
#include "log.hpp"
#include <memory>
#include <stdexcept>
//#include <iostream>

extern "C" {
#include <libavformat/avformat.h>
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
        CodecContext                codexCtx_;         ///< codec context for the video codec context of opened stream
        Packet                      pkt_;              ///< packet to be used for reading from file
        int                         stream_;           ///< Index of the opened video stream

    public:
        /// Ctor
        /// @param[in] url url or filename to open
        /// @param[in] pFmt ptr to input format. If none is provided, it is guessed from the url
        /// @param[in] pDict pointer to a ddictionary that has entries used for opening the url, such as framerate.
        Implementation(const std::string& url, AVInputFormat* pFmt, AVDictionary*& pDict):
        formatCtx_(FormatContext::INPUT),
        codexCtx_((AVCodec*) nullptr),
        pkt_(),
        stream_(-1)
        {
            //        av_register_all();          // Register codecs - his is deprecated for ffmpeg >4.0
            int ret = avformat_open_input( &formatCtx_.get(), url.c_str(), pFmt, &pDict );
            if( ret < 0 )  // Couldn't open file
            {
                throw MediaError("Could not open " + url, ret);
            }

            // Retrieve stream information
            ret = avformat_find_stream_info(formatCtx_.get(), nullptr);
            if( ret < 0 ) // Couldn't find stream information
            {
                throw MediaError("Could not find stream information", ret);
            }
            const int nStreams = formatCtx_->nb_streams;
            LOGD("MediaReader: Format context found.\n\tStart time = ", formatCtx_->start_time, "\n\t#streams = ", nStreams);

            // Open the first found video stream
            for (int i = 0; i < nStreams; ++i)
            {
                AVStream* pStream = formatCtx_->streams[i];
                assert(pStream && pStream->codecpar);
                const AVMediaType type = pStream->codecpar->codec_type;
                if ( type != AVMEDIA_TYPE_VIDEO )
                {
                    LOGD("MediaReader: Skipping unprocessed stream ", *pStream);
                    continue;
                }
                // Open codec
                AVCodecID id = pStream->codecpar->codec_id;
                AVCodec* pCodec = avcodec_find_decoder(id);
                if (!pCodec)
                {
                    throw MediaError("Unable to find decoder for " + std::to_string(id));
                }
                int ret = avcodec_parameters_to_context(codexCtx_.get(), pStream->codecpar);
                if (ret < 0)
                {
                    throw MediaError("Unable to initialize codec context " , ret);
                }

                Dictionary codecOpts;
                codecOpts.add("refcounted_frames", 1);
                ret = avcodec_open2(codexCtx_.get(), pCodec, &codecOpts.get());
                if (ret < 0)
                {
                    throw MediaError("Unable to open decoding codec" , ret);
                }
                codexCtx_->time_base = pStream->time_base;

                LOGD("MediaReader: Opened decoder for stream:\n", *pStream);
                stream_ = i;
                break;
            }
            if (stream_ < 0)
            {
                throw MediaError("Unable to open any video streams");
            }
        }

        static std::unique_ptr<Implementation> OpenFile(const std::string& url, Dictionary& dict)
        {
            avdevice_register_all();    // Register devices
            return std::make_unique<Implementation>(url, nullptr, dict.get());
        }

        static std::unique_ptr<Implementation> OpenFile(const std::string& url)
        {
            Dictionary tmp;
            return OpenFile(url, tmp);
        }

        static std::unique_ptr<Implementation> OpenStream(const std::string& url, Dictionary& dict)
        {
            avdevice_register_all();    // Register devices
            AVInputFormat *pFormat = av_find_input_format(INPUT_DRIVER.c_str());
            if (!pFormat)
            {
                throw MediaError("Cannot determine input format for " + INPUT_DRIVER);
            }
            return std::make_unique<Implementation>(url, pFormat, dict.get());
        }

        static std::unique_ptr<Implementation> OpenStream(const std::string& url)
        {
            Dictionary tmp;
            return OpenStream(url, tmp);
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
                    LOGD("Reached end of file. Closing.");
                    return nullptr;
                }
                else if (ret < 0)
                {
                    throw MediaError("Error reading packets", ret);
                }
                stream = pkt_->stream_index;
                if ( stream != stream_ )   //no decoder open for this stream. Skip & read more packets
                {
                    LOGD("Skipping packet from undecoded stream.");
                    pkt_.unref();
                    return read(frame);
                }
                //Valid packet & decoder - send packet to decoder
                ret = avcodec_send_packet(codexCtx_.get(), pkt_.get());
                if (ret < 0)
                {
                    throw MediaError("Unable to decode packet", ret);
                }
            }
            // Receive decoded frame if available
            assert (stream == stream_);
            ret = avcodec_receive_frame(codexCtx_.get(), frame.get());
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
                    LOGD("StreamDecoder: End of file, no more frames to receive.");
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
    
    MediaReader::MediaReader(const std::string& url, InputMediaType type/*=InputType::FILE*/) try:
    pImpl_( type == InputMediaType::FILE ? Implementation::OpenFile(url) : Implementation::OpenStream(url) )
    {
        assert (pImpl_);
        LOGD(logging::LINE_SINGLE, "Opened video stream:", *getVideoStream(), "\n", logging::LINE_SINGLE);
    }
    catch (std::exception& err)
    {
        std::throw_with_nested( MediaError("MediaReader: Unable to open " + url) );
    }
    
    MediaReader::MediaReader(const std::string& url, Dictionary& dict, InputMediaType type/*=InputType::FILE*/) try:
    pImpl_( type == InputMediaType::FILE ? Implementation::OpenFile(url, dict) : Implementation::OpenStream(url, dict))
    {
        assert (pImpl_);
        LOGD(logging::LINE_SINGLE, "Opened video stream:", *getVideoStream(), "\n", logging::LINE_SINGLE);
#ifndef NDEBUG
        {
            LOGD("Unused options while opening MediaReader:\n", dict.as_string());
        }
#endif

    }
    catch (std::exception& err)
    {
        std::throw_with_nested( MediaError("MediaReader: Unable to open " + url) );
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
                LOGD("End of stream reached. closing MediaReader.");
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
