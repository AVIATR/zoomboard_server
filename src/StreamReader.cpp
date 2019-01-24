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

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>
#include <libavutil/frame.h>
#include <libavdevice/avdevice.h>
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
    public:
        /// Ctor
        /// @param[in] filename name of file to open
        Implementation(const std::string& streamName):
        hFormatCtx_( nullptr ),
        hPkt_( allocatePacket() ),
        hCC_( allocateCodecContext() ),
        hFrame_( allocateFrame() ),
        stream_(-1)
        {
            if (!hPkt_ || !hCC_ || !hFrame)
            {
                throw StreamError("Allocation error.");
            }
            hPkt_->stream_index = -1;   //no streams opened yet
            //Open format context
            //Register devices:
            avdevice_register_all();
            AVFormatContext* p = nullptr;
            int status = avformat_open_input( &p, streamName.c_str(), nullptr, nullptr);
            if( status < 0 )  // Couldn't open file
            {
                throw StreamError("Could not open stream " + filename, status);
            }
            assert(p);
            hFormatCtx_ = avtools::FormatContextHandle(p, [](AVFormatContext* pp) {avformat_close_input(&pp); });

            // Register codecs
            av_register_all();

            // Retrieve stream information
            status = avformat_find_stream_info(hFormatCtx_.get(), nullptr);
            if( status < 0 ) // Couldn't find stream information
            {
                throw StreamError("Could not find stream information in " + filename, status);
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
                AVCodecContext* pCC = h->hCC_.get();
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
                throw StreamError("Unable to open any video streams from " + streamName);
            }
        }

        /// @return a list of opened streams
        const AVStream* stream() const
        {
            assert( (stream_ >= 0) && (stream < hFormatCtx_->nb_streams) );
            return hFormatCtx_->streams[stream_]
        }

        /// Dtor
        ~Implementation()
        {
            close()
        }

        /// Reads a frame
        /// @param[out] pFrame pointer to frame. Will contain new frame upon return
        /// @return pointer to the stream that the frame is from. Will be nullptr when finished reading without errors.
        /// @throw MMError if there was a problem reading frames.
        const AVStream* read(AVFrame const *& pFrame)
        {
            assert(stream_ >= 0);    //Otherwise stream is closed and we shouldn't be calling this
            int stream = hPkt_->stream_index;
            if (stream < 0)    //read more packets
            {
                int ret = av_read_frame(hFormatCtx_.get(), hPkt_.get()); //read a new packet
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
                if ( stream != stream_] )   //no decoder open for this stream. Skip & read more packets
                {
                    LOGD("StreamReader: Skipping packet from undecoded stream.");
                    unrefPacket(hPkt_);
                    hPkt_->stream_index = -1;
                    return read(pFrame);
                }
                //Valid packet & decoder - send packet to decoder
                ret = avcodec_send_packet(hCC_.get(), &pkt);
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
                        hFrame_->pts = av_frame_get_best_effort_timestamp( hFrame_.get() );
                    }
                    pFrame = hFrame_.get();
                    return stream();
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
    
    StreamReader::StreamReader(const std::string& streamName) try:
    pImpl_(new Implementation(streamName))
    {
        assert (pImpl);
        LOGD(logging::LINE_SINGLE, "Opened video stream:", avtools::getStreamInfo(getVideoStream()), "\n", logging::LINE_SINGLE);
    }
    catch (std::exception& err)
    {
        std::throw_with_nested( StreamError("StreamReader: Unable to open " + filename) );
    }
    
    StreamReader::~StreamReader() = default;
    
    StreamReader::Handle StreamReader::Open(const std::string& streamName)
    {
        try
        {
            return Handle(new StreamReader(filename))
        }
        catch (std::exception& err)
        {
            LOGE("StreamReader: Unable to open stream ", streamName);
            return nullptr
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
            pImpl_->reset(nullptr);
            std::throw_with_nested( StreamError("StreamReader: Unable to read frames.") );
        }
    }

    const AVStream* StreamReader::getVideoStream() const
    {
        assert(pImpl_);
        return pImpl_->stream();
    }
}   //::avtools
