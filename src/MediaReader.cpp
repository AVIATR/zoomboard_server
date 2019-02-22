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
    /// Initializes and returns a format context using the information in the provided dictionary
    
//    avtools::FormatContextHandle initFormatContext(AVDictionary* pDict)
//    {
//        assert(pDict);
//        avdevice_register_all();    // Register devices
//        //        av_register_all();          // Register codecs - his is deprecated for ffmpeg >4.0
//        AVFormatContext* p = nullptr;
//        AVInputFormat *pFormat = nullptr;
//        // Get the input format
//        const AVDictionaryEntry* pDriver = av_dict_get(pDict, "driver", nullptr, 0);
//        if (pDriver)
//        {
//            pFormat = av_find_input_format(pDriver->value);
//        }
//
//        assert(av_dict_get(pDict, "url", nullptr, 0));    //url must be provided
//        const char* url = av_dict_get(pDict, "url", nullptr, 0)->value;
//        int status = avformat_open_input( &p, url, pFormat, &pDict );
//        if( status < 0 )  // Couldn't open file
//        {
//            throw MediaError("Could not open " + std::string(url), status);
//        }
//        assert(p);
//        return avtools::FormatContextHandle(p, [](AVFormatContext* pp) {avformat_close_input(&pp); });
//    }
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
        Frame                       frame_;            ///< decoded video frame data
        int                         stream_;           ///< Index of the opened video stream

    public:
        /// Ctor
        /// @param[in] url url or filename to open
        /// @param[in] pFmt ptr to input format. If none is provided, it is guessed from the url
        /// @param[in] pDict pointer to a ddictionary that has entries used for opening the url, such as framerate.
        Implementation(const std::string& url, AVInputFormat* pFmt, AVDictionary* pDict):
        formatCtx_(FormatContext::INPUT),
        codexCtx_(),
        pkt_(),
        frame_(),
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
                AVDictionary *codecOpts = nullptr;   //codec options
                av_dict_set(&codecOpts, "refcounted_frames", "1", 0);
                ret = avcodec_open2(codexCtx_.get(), pCodec, &codecOpts);
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
#ifndef NDEBUG
            CharBuf buf;
            ret = av_dict_get_string(pDict, &buf.get(), '\t', '\n');
            if (ret < 0)
            {
                LOGE("Could not get unused options: ", av_err2str(ret));
            }
            LOGD("Unused options while opening MediaReader:", buf.get());
#endif
        }

        static std::unique_ptr<Implementation> Open(const std::string& url)
        {
            avdevice_register_all();    // Register devices
            return std::make_unique<Implementation>(url, nullptr, nullptr);
        }

        static std::unique_ptr<Implementation> Open(const std::string& url, const Dictionary& dict)
        {
            avdevice_register_all();    // Register devices
            AVInputFormat *pFormat = nullptr;
            if (dict.has("driver"))
            {
                pFormat = av_find_input_format(dict["driver"].c_str());
                if (!pFormat)
                {
                    throw MediaError("Cannot determine input format for " + dict["driver"]);
                }
            }
            Dictionary tmpDict = dict;
            return std::make_unique<Implementation>(url, pFormat, tmpDict.get());
        }

        /// @return a list of opened streams
        const AVStream* stream() const
        {
            assert( (stream_ >= 0) && (stream_ < formatCtx_->nb_streams) );
            return formatCtx_->streams[stream_];
        }

        /// Dtor
        ~Implementation() = default;

        /// Reads a frame
        /// @param[out] pFrame pointer to frame. Will contain new frame upon return
        /// @return pointer to the stream that the frame is from. Will be nullptr when finished reading without errors.
        /// @throw MMError if there was a problem reading frames.
        const AVStream* read(AVFrame const *& pFrame)
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
                    pFrame = nullptr;
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
                    return read(pFrame);
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
            ret = avcodec_receive_frame(codexCtx_.get(), frame_.get());
            switch (ret)
            {
                case 0:
                    if (frame_->pts == AV_NOPTS_VALUE)
                    {
                        frame_->pts = frame_->best_effort_timestamp;
                    }
                    pFrame = frame_.get();
                    return formatCtx_->streams[stream_];
                case AVERROR(EAGAIN):   //more packets need to be read & sent to the decoder before decoding
                    pkt_.unref();
                    return read(pFrame);
                case AVERROR(EOF):
                    LOGW("StreamDecoder: End of file, no more frames to receive.");
                    pFrame = nullptr;
                    return nullptr;
                case AVERROR(EINVAL):
                    pFrame = nullptr;
                    throw MediaError("Codec not opened, or it is an encoder");
                default:
                    pFrame = nullptr;
                    throw MediaError("Decoding error.", ret);
            }
        }
    };  // avtools::MediaReader::Implementation

    //==========================================
    //
    // MediaReader Definitions
    //
    //==========================================
    
    MediaReader::MediaReader(const std::string& url) try:
    pImpl_( Implementation::Open(url) )
    {
        assert (pImpl_);
        LOGD(logging::LINE_SINGLE, "Opened video stream:", *getVideoStream(), "\n", logging::LINE_SINGLE);
    }
    catch (std::exception& err)
    {
        std::throw_with_nested( MediaError("MediaReader: Unable to open " + url) );
    }
    
    MediaReader::MediaReader(const std::string& url, const Dictionary& opts) try:
    pImpl_( Implementation::Open( url, opts ) )
    {
        assert (pImpl_);
        LOGD(logging::LINE_SINGLE, "Opened video stream:", *getVideoStream(), "\n", logging::LINE_SINGLE);
    }
    catch (std::exception& err)
    {
        std::throw_with_nested( MediaError("MediaReader: Unable to open " + url) );
    }



    MediaReader::~MediaReader() = default;
    
    const AVStream* MediaReader::read(AVFrame const*& pFrame)
    {
        assert(pImpl_);
        try
        {
            const AVStream* pStr = pImpl_->read(pFrame);
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
