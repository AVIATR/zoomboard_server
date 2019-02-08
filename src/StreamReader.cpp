//
//  StreamReader.cpp
//  StreamReader
//
//  Created by Ender Tekin on 10/8/14.
//  Copyright (c) 2014 Smith-Kettlewell. All rights reserved.
//

#include "StreamReader.hpp"
#include "Stream.hpp"
//#include "MediaUtils.hpp"
#include <stdexcept>
//#include <iostream>
#include "log.hpp"
#include <memory>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>
#include <libavutil/frame.h>
#include <libavdevice/avdevice.h>
}

using avtools::StreamError;
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
//            throw StreamError("Could not open " + std::string(url), status);
//        }
//        assert(p);
//        return avtools::FormatContextHandle(p, [](AVFormatContext* pp) {avformat_close_input(&pp); });
//    }
}

namespace avtools
{
    //==========================================
    //
    // StreamReader Implementation
    //
    //==========================================
    
    /// @class StreamReader Implementation
    /// The code is based on https://github.com/chelyaev/ffmpeg-tutorial
    /// which is an updated (as of 02/13/2014) version of Stephen Dranger's original tutorials
    /// The originals can be found at http://dranger.com/ffmpeg/
    /// Also see https://stackoverflow.com/questions/35340437/how-can-i-use-avformat-open-input-function-ffmpeg for opening input devices
    class StreamReader::Implementation
    {
    private:
        FormatContextHandle         hFormatCtx_;        ///< Format I/O context
        PacketHandle                hPkt_;              ///< packet to be used for reading from file
        CodecContextHandle          hCC_;               ///< codec context handle for the video codec context of opened stream
        FrameHandle                 hFrame_;            ///< decoded video frame data
        int                         stream_;            ///< Index of the opened video stream
        
        void close()
        {
            //Close the stream if still open
            if (stream_ > 0)
            {
                hPkt_.reset(nullptr);
                hFrame_.reset(nullptr);
                {
                    LOGD("StreamReader: closing decoder for stream ", *hFormatCtx_->streams[stream_] );
                    hCC_.reset(nullptr);
                }
                if (hFormatCtx_)
                {
                    LOGD("StreamReader: Closing media file.");
                    hFormatCtx_.reset(nullptr);
                }
                stream_ = -1;
            }
        }

        /// Ctor to open a file
        /// @param[in] filename name of file to open
        Implementation( FormatContextHandle h):
        hFormatCtx_( std::move(h) ),
        hPkt_( allocatePacket() ),
        hCC_( allocateCodecContext() ),
        hFrame_( allocateFrame() ),
        stream_(-1)
        {
            if (!hPkt_ || !hCC_ || !hFrame_)
            {
                throw StreamError("Allocation error.");
            }
            hPkt_->stream_index = -1;   //no streams opened yet

            // Retrieve stream information
            int ret = avformat_find_stream_info(hFormatCtx_.get(), nullptr);
            if( ret < 0 ) // Couldn't find stream information
            {
                throw StreamError("Could not find stream information", ret);
            }
            const int nStreams = hFormatCtx_->nb_streams;
            LOGD("StreamReader: Format context found.\n\tStart time = ", hFormatCtx_->start_time, "\n\t#streams = ", nStreams);

            // Open the first found video stream
            for (int i = 0; i < nStreams; ++i)
            {
                AVStream* pStream = hFormatCtx_->streams[i];
                assert(pStream && pStream->codecpar);
                const AVMediaType type = pStream->codecpar->codec_type;
                if ( type != AVMEDIA_TYPE_VIDEO )
                {
                    LOGD("StreamReader: Skipping unprocessed stream ", *pStream);
                    continue;
                }
                // Open codec
                AVCodecID id = pStream->codecpar->codec_id;
                AVCodec* pCodec = avcodec_find_decoder(id);
                if (!pCodec)
                {
                    throw StreamError("Unable to find decoder for " + std::to_string(id));
                }
                AVDictionary *opts = nullptr;
                av_dict_set(&opts, "refcounted_frames", "1", 0);
                AVCodecContext* pCC = hCC_.get();
                int ret = avcodec_parameters_to_context(pCC, pStream->codecpar);
                if (ret < 0)
                {
                    throw StreamError("Unable to initialize codec context " , ret);
                }
                ret = avcodec_open2(pCC, pCodec, &opts);
                if (ret < 0)
                {
                    throw StreamError("Unable to open decoding codec" , ret);
                }
                pCC->time_base = pStream->time_base;

                LOGD("StreamReader: Opened decoder for stream:\n", avtools::getBriefStreamInfo(pStream));
                stream_ = i;
                break;
            }
            if (stream_ < 0)
            {
                throw StreamError("Unable to open any video streams");
            }
        }
    public:

        static std::unique_ptr<Implementation> Open(const AVDictionary& dict)
        {
            avdevice_register_all();    // Register devices
            //        av_register_all();          // Register codecs - his is deprecated for ffmpeg >4.0
            AVFormatContext* p = nullptr;
            AVInputFormat *pFormat = nullptr;
            // Get the input format
            const AVDictionaryEntry* pDriver = av_dict_get(&dict, "driver", nullptr, 0);
            if (pDriver)
            {
                pFormat = av_find_input_format(pDriver->value);
            }
            
            assert(av_dict_get(&dict, "url", nullptr, 0));    //url must be provided
            const char* url = av_dict_get(&dict, "url", nullptr, 0)->value;
            AVDictionary* pDict = nullptr;
            int status = av_dict_copy(&pDict, &dict, 0);
            if (status < 0)
            {
                throw StreamError("Unable to clone input dictionary.");
            }
            
            status = avformat_open_input( &p, url, pFormat, &pDict );
            if( status < 0 )  // Couldn't open file
            {
                throw StreamError("Could not open " + std::string(url), status);
            }
#ifndef NDEBUG
            char* buf=nullptr;
            status = av_dict_get_string(pDict, &buf, '\t', '\n');
            if (status < 0)
            {
                LOGE("Could not get unused options: ", av_err2str(status));
            }
            LOGD("Unused options while opening StreamReader:", buf);
            av_freep(&buf);
#endif
            assert(p);
            return std::unique_ptr<Implementation>(
                            new Implementation(FormatContextHandle(p, [](AVFormatContext* pp){avformat_close_input(&pp);})) );
        }
        
        /// Static constructor
        /// @param[in] url url to open
        /// @return a handle to a StreamReader::Implementation instance.
        static std::unique_ptr<Implementation> Open(const std::string& filename)
        {
            AVDictionary* pOpts = nullptr;
            int ret = av_dict_set(&pOpts, "url", filename.c_str(), 0);
            avtools::Handle<AVDictionary> hDict(pOpts, [](AVDictionary* p){if (p) av_dict_free(&p);});
            if (ret < 0)
            {
                throw StreamError("Unable to set url in dictionary. ", ret);
            }
            return Open(*pOpts);
        }
        
        /// @return a list of opened streams
        const AVStream* stream() const
        {
            assert( (stream_ >= 0) && (stream_ < hFormatCtx_->nb_streams) );
            return hFormatCtx_->streams[stream_];
        }

        /// Dtor
        ~Implementation()
        {
            close();
        }

        /// Reads a frame
        /// @param[out] pFrame pointer to frame. Will contain new frame upon return
        /// @return pointer to the stream that the frame is from. Will be nullptr when finished reading without errors.
        /// @throw MMError if there was a problem reading frames.
        const AVStream* read(AVFrame const *& pFrame)
        {
            assert(stream_ >= 0);    //Otherwise stream is closed and we shouldn't be calling this
            int ret;
            int stream = hPkt_->stream_index;
            if (stream < 0)    //read more packets
            {
                ret = av_read_frame(hFormatCtx_.get(), hPkt_.get()); //read a new packet
                if (AVERROR_EOF == ret)
                {
                    LOGD("StreamReader: reached end of file. Closing.");
                    close();
                    pFrame = nullptr;
                    return nullptr;
                }
                else if (ret < 0)
                {
                    throw StreamError("Error reading packets", ret);
                }
                stream = hPkt_->stream_index;
                if ( stream != stream_ )   //no decoder open for this stream. Skip & read more packets
                {
                    LOGD("StreamReader: Skipping packet from undecoded stream.");
                    unrefPacket(hPkt_);
                    hPkt_->stream_index = -1;
                    return read(pFrame);
                }
                //Valid packet & decoder - send packet to decoder
                ret = avcodec_send_packet(hCC_.get(), hPkt_.get());
                if (ret < 0)
                {
                    throw StreamError("Unable to decode packet", ret);
                }
            }
            // Receive decoded frame if available
            assert (stream == stream_);
            ret = avcodec_receive_frame(hCC_.get(), hFrame_.get());
            switch (ret)
            {
                case 0:
                    assert(hFrame_);
                    if (hFrame_->pts == AV_NOPTS_VALUE)
                    {
                        hFrame_->pts = hFrame_->best_effort_timestamp;
                    }
                    pFrame = hFrame_.get();
                    return hFormatCtx_->streams[stream_];
                case AVERROR(EAGAIN):   //more packets need to be read & sent to the decoder before decoding
                    unrefPacket(hPkt_);
                    hPkt_->stream_index = -1;
                    return read(pFrame);
                case AVERROR(EOF):
                    LOGW("StreamDecoder: End of file, no more frames to receive.");
                    pFrame = nullptr;
                    return nullptr;
                case AVERROR(EINVAL):
                    pFrame = nullptr;
                    throw StreamError("Codec not opened, or it is an encoder");
                default:
                    pFrame = nullptr;
                    throw StreamError("Decoding error.", ret);
            }
        }
    };  // avtools::StreamReader::Implementation

    //==========================================
    //
    // StreamReader Definitions
    //
    //==========================================
    
    StreamReader::StreamReader(const std::string& url) try:
    pImpl_( Implementation::Open(url) )
    {
        assert (pImpl_);
        LOGD(logging::LINE_SINGLE, "Opened video stream:", avtools::getStreamInfo(getVideoStream()), "\n", logging::LINE_SINGLE);
    }
    catch (std::exception& err)
    {
        std::throw_with_nested( StreamError("StreamReader: Unable to open reader") );
    }
    
    StreamReader::StreamReader(const AVDictionary& opts) try:
    pImpl_( Implementation::Open( opts ) )
    {
        assert (pImpl_);
        LOGD(logging::LINE_SINGLE, "Opened video stream:", avtools::getStreamInfo(getVideoStream()), "\n", logging::LINE_SINGLE);
    }
    catch (std::exception& err)
    {
        std::throw_with_nested( StreamError("StreamReader: Unable to open reader") );
    }

    StreamReader::~StreamReader() = default;
    
    StreamReader::Handle StreamReader::Open(const std::string& url) noexcept
    {
        try
        {
            return Handle(new StreamReader(url));
        }
        catch (std::exception& err)
        {
            LOGE("StreamReader: Unable to open device. Err: ", err.what());
            return nullptr;
        }
    }
    
    StreamReader::Handle StreamReader::Open(const AVDictionary& opts) noexcept
    {
        try
        {
            return Handle(new StreamReader(opts));
        }
        catch (std::exception& err)
        {
            LOGE("StreamReader: Unable to open device. Err: ", err.what());
            return nullptr;
        }
    }

    const AVStream* StreamReader::read(AVFrame const*& pFrame)
    {
        assert(pImpl_);
        try
        {
            return pImpl_->read(pFrame);
        }
        catch (std::exception& err)
        {
            pImpl_.reset(nullptr);
            std::throw_with_nested( StreamError("StreamReader: Unable to read frames.") );
        }
    }

    const AVStream* StreamReader::getVideoStream() const
    {
        assert(pImpl_);
        return pImpl_->stream();
    }
}   //::avtools
