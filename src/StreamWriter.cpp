//
//  StreamWriter.cpp
//  MM
//
//  Created by Ender Tekin on 11/12/14.
//
//

#include "StreamWriter.hpp"
#include "MultiMedia.hpp"
#include "MediaUtils.hpp"
#include "log.hpp"
#include <string>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

#ifndef NDEBUG
#include <map>
#endif

namespace
{
    /// Allocates a new output format context
    /// @param[in] filename name of file. Container type is guessed from filename.
    /// @return a newly initialized FormatContext
    avtools::FormatContextHandle allocateOutputFormatContext(const std::string& filename)
    {
        AVFormatContext* p = nullptr;
        if (avformat_alloc_output_context2(&p, nullptr, nullptr, filename.c_str()) < 0)
        {
            return nullptr;
        }
        assert(p);
        return avtools::FormatContextHandle(p, [](AVFormatContext* pp) {avformat_free_context(pp); });
    }

    ///@class Encoder for a particular stream
    class StreamEncoder
    {
    private:

        avtools::CodecContextHandle hCC_;   ///< encoder codec context
        avtools::PacketHandle hPkt_;
        
        /// Ctor
        /// @param[in] codecParam codec parameters to use
        StreamEncoder(const AVCodecParameters& codecParam, avtools::TimeBaseType timebase, bool useGlobalHeader, int compliance):
        hCC_( avtools::allocateCodecContext() ),
        hPkt_( avtools::allocatePacket() )
        {
            if (!hCC_)
            {
                throw EncoderError("Unable to allocate codec context");
            }
            if (!hPkt_)
            {
                throw EncoderError("nable to allocate packet");
            }
            avtools::initPacket(hPkt_);
            
            // Set up encoder context
            int ret = avcodec_parameters_to_context(hCC_.get(), &codecParam);
            if (ret < 0)
            {
                throw EncoderError("Unable to copy codec parameters to encoder context.", ret);
            }
            AVDictionary *opts = nullptr;
            
            //Initialize stream to use the provided codec context
            hCC_->strict_std_compliance = compliance;
            const AVMediaType codecType = codecParam.codec_type;
            const AVCodecID codecId = codecParam.codec_id;
            AVCodec* pEncoder = avcodec_find_encoder(codecId);
            if (!pEncoder)
            {
                throw EncoderError("Unable to find an encoder for " + std::string(avcodec_get_name(codecId)) );
            }

            switch (codecType)
            {
                case AVMEDIA_TYPE_AUDIO:
                    break;
                case AVMEDIA_TYPE_VIDEO:
                    if (AV_CODEC_ID_H264 == codecId)  // ffmpeg has issues with x264, and the default presets are broken. These need to be manually fixed for encoding
                    {
                        int losses = 0;
                        hCC_->pix_fmt = avcodec_find_best_pix_fmt_of_list(pEncoder->pix_fmts, (AVPixelFormat) codecParam.format, false, &losses);
                        if (losses)
                        {
                            LOGW("StreamEncoder: Converting picture format from ", (AVPixelFormat) codecParam.format, " to output format ", hCC_->pix_fmt, "will result in losses due to: ");
                            if (losses & FF_LOSS_RESOLUTION)
                            {
                                LOGD("\tresolution change");
                            }
                            if (losses & FF_LOSS_DEPTH)
                            {
                                LOGD("\tcolor depth change");
                            }
                            if (losses & FF_LOSS_COLORSPACE)
                            {
                                LOGD("\tcolorspace conversion");
                            }
                            if (losses & FF_LOSS_COLORQUANT)
                            {
                                LOGD("\tcolor quantization");
                            }
                            if (losses & FF_LOSS_CHROMA)
                            {
                                LOGD("\tloss of chroma");
                            }
                        }
                        hCC_->qmin = 10;
                        hCC_->qmax = 51;
                        hCC_->qcompress = 0.6;
                        hCC_->max_qdiff = 4;
                        hCC_->me_range = 3;
                        hCC_->max_b_frames = 3;
//                        hCC_->bit_rate = 1000000;
                        hCC_->refs = 3;
                        ret = av_dict_set(&opts, "profile", "main", 0);   //also initializes opts
                        if (ret < 0)
                        {
                            LOGE("StreamEncoder: Unable to set profile for H264 encoder: ", av_err2str(ret));
                        }
                        ret = av_dict_set(&opts, "preset", "medium", 0);
                        if (ret < 0)
                        {
                            LOGE("StreamEncoder: Unable to set preset for H264 encoder: ", av_err2str(ret));
                        }
                        ret = av_dict_set(&opts, "level", "3.1", 0);
                        if (ret < 0)
                        {
                            LOGE("StreamEncoder: Unable to set level for H264 encoder: ", av_err2str(ret));
                        }
                        ret = av_dict_set(&opts, "mpeg_quant", "0", 0);
                        if (ret < 0)
                        {
                            LOGE("StreamEncoder: Unable to set mpeg quantization for H264 encoder: ", av_err2str(ret));
                        }
                        assert(opts);
                    }
                    break;
                default:
                    assert(false);  //should not come here.
            }
            
            // let codec know if we are using global header
            if (useGlobalHeader)
            {
                hCC_->flags |= CODEC_FLAG_GLOBAL_HEADER;
            }
            
            // Set timebase
            hCC_->time_base = timebase;
            
            // open encoder
            ret = avcodec_open2(hCC_.get(), pEncoder, &opts);
            if (ret < 0)
            {
                throw EncoderError("Unable to open encoder for codec " + std::string(avcodec_get_name(codecId)), ret);
            }
            LOGD("StreamEncoder: Opened encoder for ", avtools::getCodecInfo(hCC_.get()));
        }

    public:
        
        /// Handle type
        typedef std::unique_ptr<StreamEncoder> Handle;

        static Handle Create(const AVCodecParameters& codecParam, avtools::TimeBaseType timebase, bool useGlobalHeader, int compliance) noexcept
        {
            try
            {
                Handle h(new StreamEncoder(codecParam, timebase, useGlobalHeader, compliance));
                assert( h->isOpen() );
                return h;
            }
            catch(std::exception& err)
            {
                LOGE("StreamEncoder: Error opening encoder: ", err.what());
                return nullptr;
            }
        }

        /// Dtor
        ~StreamEncoder()
        {
            LOGD("StreamEncoder: Closing codec ", hCC_->codec_id);
        }
        
#ifndef NDEBUG
        inline const AVCodec* codec() const
        {
            assert( isOpen() );
            return hCC_->codec;
        }
        
        inline const AVCodecContext* context() const
        {
            assert( isOpen() );
            return hCC_.get();
        }
#endif
        
        void push(const AVFrame* pFrame)
        {
            assert(isOpen());
//            AVFrame* pp = const_cast<AVFrame*>(pFrame);
//            pp->dts = AV_NOPTS_VALUE;
            
            int ret = avcodec_send_frame(hCC_.get(), pFrame);
            if (ret < 0)
            {
                throw EncoderError("Error sending frames to encoder", ret);
            }
        }
        
        AVPacket* pop()
        {
            assert(isOpen());
            avtools::unrefPacket(hPkt_);
            int ret = avcodec_receive_packet(hCC_.get(), hPkt_.get());
            if (ret == AVERROR(EAGAIN))
            {
                return nullptr;
            }
            else if (ret == AVERROR_EOF)
            {
                return nullptr;
            }
            else if (ret < 0)
            {
                throw EncoderError("Error reading packets from encoder", ret);
            }
            return hPkt_.get();
        }
        
        /// @return type of media this decoder belongs to
        inline AVMediaType type() const
        {
            assert(isOpen());
            return hCC_->codec_type;
        }

#ifndef NDEBUG
        /// @return true if a encoder is open, false otherwise.
        inline bool isOpen() const noexcept
        {
            assert(hCC_);
            return avcodec_is_open(hCC_.get());
        }
#endif

    };
    
}   //::<anon>

namespace avtools
{
    //=====================================================
    //
    //StreamWriter Implementation
    //
    //=====================================================
    class StreamWriter::Implementation
    {
    private:
        FormatContextHandle                 hCtx_;      ///< format context for the output file
        avtools::CodecContextHandle         hCC_;       ///< encoder codec context
        avtools::PacketHandle               hPkt_;      ///< packet to encode the frames in
        
    public:
        /// Ctor
        Implementation(const std::string& filename):
        hCtx_( allocateOutputFormatContext(filename) ),
        hCC_( avtools::allocateCodecContext() ),
        hPkt_( avtools::allocatePacket() )
        {
            av_register_all();
            if (!hCtx_ || !hCC_ || !hPkt_)
            {
                throw StreamError("Unable to allocate contexts.");
            }
            avtools::initPacket(hPkt_);
            
            //Open the output file
            if ( !(pCtx_->flags & AVFMT_NOFILE) )
            {
                int ret = avio_open(&pCtx_->pb, filename.c_str(), AVIO_FLAG_WRITE);
                if (ret < 0)
                {
                    throw StreamError("Could not open " + filename, ret);
                }
                assert(pCtx_->pb);
            }
            LOGD("StreamWriter: Opened output stream ", filename, " in ", pCtx_->oformat->long_name, " format.");
            
            //Add the video stream to the output context
            // Ensure that the provided container can contain the requested output codec
            assert (codecParam.codec_type == AVMEDIA_TYPE_VIDEO);
            const int compliance = (allowExperimentalCodecs ? FF_COMPLIANCE_EXPERIMENTAL : FF_COMPLIANCE_NORMAL);
            const AVCodecID codecId = codecParam.codec_id;            
            AVOutputFormat* pOutFormat = pCtx_->oformat;
            assert(pOutFormat);
            int ret = avformat_query_codec(pOutFormat, codecId, compliance);
            if ( ret <= 0 )
            {
                throw StreamError("File format " + pOutFormat->name + " is unable to store " + std::to_string(codecId) + " streams.", ret);
            }
            
            // Find encoder
            const AVCodec* pEncoder = avcodec_find_encoder(codecId);
            if (!pEncoder)
            {
                throw StreamError("Cannot find an encoder for " + std::to_string(codecId));
            }
            // Set up encoder context
            int ret = avcodec_parameters_to_context(hCC_.get(), &codecParam);
            if (ret < 0)
            {
                throw StreamError("Unable to copy codec parameters to encoder context.", ret);
            }
            AVDictionary *opts = nullptr;

            //Initialize codec context
            hCC_->strict_std_compliance = compliance;
            if (useGlobalHeader)               // let codec know if we are using global header
            {
                hCC_->flags |= CODEC_FLAG_GLOBAL_HEADER;
            }
            hCC_->time_base = timebase;        // Set timebase
            if (AV_CODEC_ID_H264 == codecId)   // ffmpeg has issues with x264, and the default presets are broken. These need to be manually fixed for encoding
            {
                int losses = 0;
                hCC_->pix_fmt = avcodec_find_best_pix_fmt_of_list(pEncoder->pix_fmts, (AVPixelFormat) codecParam.format, false, &losses);
                if (losses)
                {
                    LOGW("StreamEncoder: Converting picture format from ", (AVPixelFormat) codecParam.format, " to output format ", hCC_->pix_fmt, "will result in losses due to: ");
                    if (losses & FF_LOSS_RESOLUTION)
                    {
                        LOGD("\tresolution change");
                    }
                    if (losses & FF_LOSS_DEPTH)
                    {
                        LOGD("\tcolor depth change");
                    }
                    if (losses & FF_LOSS_COLORSPACE)
                    {
                        LOGD("\tcolorspace conversion");
                    }
                    if (losses & FF_LOSS_COLORQUANT)
                    {
                        LOGD("\tcolor quantization");
                    }
                    if (losses & FF_LOSS_CHROMA)
                    {
                        LOGD("\tloss of chroma");
                    }
                }
                hCC_->qmin = 10;
                hCC_->qmax = 51;
                hCC_->qcompress = 0.6;
                hCC_->max_qdiff = 4;
                hCC_->me_range = 3;
                hCC_->max_b_frames = 3;
//                        hCC_->bit_rate = 1000000;
                hCC_->refs = 3;
                ret = av_dict_set(&opts, "profile", "main", 0);   //also initializes opts
                if (ret < 0)
                {
                    LOGE("StreamEncoder: Unable to set profile for H264 encoder: ", av_err2str(ret));
                }
                ret = av_dict_set(&opts, "preset", "medium", 0);
                if (ret < 0)
                {
                    LOGE("StreamEncoder: Unable to set preset for H264 encoder: ", av_err2str(ret));
                }
                ret = av_dict_set(&opts, "level", "3.1", 0);
                if (ret < 0)
                {
                    LOGE("StreamEncoder: Unable to set level for H264 encoder: ", av_err2str(ret));
                }
                ret = av_dict_set(&opts, "mpeg_quant", "0", 0);
                if (ret < 0)
                {
                    LOGE("StreamEncoder: Unable to set mpeg quantization for H264 encoder: ", av_err2str(ret));
                }
                assert(opts);
            }
            
            // open codec for encoding
            ret = avcodec_open2(hCC_.get(), pEncoder, &opts);
            if (ret < 0)
            {
                throw StreamError("Unable to open encoder for codec " + std::to_string(codecId), ret);
            }
            assert( avcodec_is_open(hCC_.get()) );
            LOGD("StreamEncoder: Opened encoder for ", avtools::getCodecInfo(hCC_.get()));
            
            // Add stream
            AVStream* pStr = avformat_new_stream(pCtx_.get(), pEncoder);
            if ( !pStr )
            {
                throw StreamError("Unable to add new stream for " + std::to_string(codecId));
            }
            
            //copy codec params to stream
            avcodec_parameters_from_context(pStr->codecpar, hCC_.get());
            pStr->time_base = timebase;

            assert( pStr->codecpar && (pStr->codecpar->codec_id == codecId) && (pStr->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) );
            assert( (pCtx_->nb_streams == 1) && (pStr == pCtx_->streams[0]) );
            LOGD("StreamWriter: Opened ", *pStr);
#ifndef NDEBUG
            avtools::dumpStreamInfo(pCtx_.get(), 0, true);
#endif
            // Write stream header
            int ret = avformat_write_header(pCtx_.get(), nullptr);
            if (ret < 0)
            {
                close();
                throw StreamError("Error occurred when writing output stream header.", ret);
            }
            LOGD("StreamWriter: Opened output stream ", pCtx_->filename);
#ifndef NDEBUG
            avtools::dumpContainerInfo(pCtx_.get(), true);
#endif
        }
        
        /// Dtor
        ~Implementation()
        {
            assert(hCtx_);
            //pop all delayed packets from encoder
            LOGD("StreamWriter: Flushing delayed packets from encoders.");
            try
            {
                write(nullptr);
            }
            catch (std::exception& err)
            {
                LOGE("Error flushing packets and closing encoder: ", err.what())
            }
            //Write trailer
            av_write_trailer(hCtx_.get());
#ifndef NDEBUG
            avtools::dumpContainerInfo(hCtx_.get(), true);
#endif
            LOGD("StreamWriter: Closing stream.");
        
            // Close file if output is file
            if ( !(hCtx_->oformat->flags & AVFMT_NOFILE) )
            {
                avio_close(hCtx_->pb);
            }
        }
        
        /// @return list of opened streams in the output file
        inline const AVStream* stream() const
        {
            assert(pCtx_ && (pCtx_->nb_streams == 1) );
            return pCtx_->streams[0];
        }
        
        /// Writes a frame to a particular stream.
        /// @param[in] pFrame frame data to write
        void write(const AVFrame* pFrame)
        {
            // send frame to encoder
            int ret = avcodec_send_frame(hCC_.get(), pFrame);
            if (ret < 0)
            {
                if (pFrame)
                {
                    throw StreamError("Error sending frames to encoder", ret);
                }
                else
                {
                    throw StreamError("Error flushing encoder", ret);
                }
            }
            avtools::unrefPacket(hPkt_);
            while (true) //write all available packets
            {
                ret = avcodec_receive_packet(hCC_.get(), hPkt_.get());
                if (ret == AVERROR(EAGAIN))
                {
                    break;
                }
                else if (ret == AVERROR_EOF)
                {
                    break;
                }
                else if (ret < 0)
                {
                    throw StreamError("Error reading packets from encoder", ret);
                }
                //Write packet to file
                pkt_->dts = AV_NOPTS_VALUE;  //let the muxer figure this out
                pkt_->stream_index = 0;
                pkt_->pts = av_rescale_q(pkt_->pts, hCC_->time_base, stream()->time_base);   //this is necessary since writing the header can change the time_base of the stream.
//                LOGD("Muxing packet for ", *pCtx_->streams[stream]);
//                LOGD("\tpts = ", pkt->pts, ", time_base = ", pCtx_->streams[stream]->time_base, ";\ttime(s) = ", avtools::calculateTime(pkt->pts, pCtx_->streams[stream]->time_base));
        
                //mux encoded frame
                ret = av_interleaved_write_frame(pCtx_.get(), pkt_);
                if (ret < 0)
                {
                    throw StreamError("Error muxing packet", ret);
                }
            }
//            av_log_set_level(AV_LOG_WARNING);
        }
        
    };  //::avtools::StreamWriter::Implementation
    
    //=====================================================
    //
    //StreamWriter Definitions
    //
    //=====================================================
    
    StreamWriter::StreamWriter(
        const std::string& url, 
        const AVCodecParameters& codecParam, 
        const TimeBaseType& timebase, 
        bool allowExperimentalCodecs=false
    ):
    pImpl_( new Implementation(url, codecParam, timebase, allowExperimentalCodecs) )
    {
        assert ( pImpl_ );
    }

    StreamWriter::~StreamWriter() = default;

    StreamWriter::Handle StreamWriter::Open(
        const std::string& url, 
        const AVCodecParameters& codecParam, 
        const TimeBaseType& timebase, 
        bool allowExperimentalCodecs=false
    ) noexcept
    {
        try
        {
            Handle h(new StreamWriter(filename));
            assert(h);
            return h;
        }
        catch (std::exception& err)
        {
            LOGE("StreamWriter: Unable to open ", url, "\n", err.what());
            return nullptr;
        }
    }
        
    const AVStream* StreamWriter::getStream() const
    {
        assert(pImpl_);
        return pImpl_->stream();
    }
    
    void StreamWriter::write(const AVFrame* pFrame)
    {
        assert( pImpl_ );
        try
        {
            if (pFrame)
            {
                pImpl_->write(pFrame);
            }
            else
            {
                pImpl_.reset(nullptr);
            }
        }
        catch (std::exception& e)
        {
            std::throw_with_nested(StreamError("StreamWriter: Error writing to stream " + std::to_string(stream)));
        }
    }
}   //::ski
