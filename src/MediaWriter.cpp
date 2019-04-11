//
//  MediaWriter.cpp
//  MM
//
//  Created by Ender Tekin on 11/12/14.
//
//

#include "MediaWriter.hpp"
#include <string>
#include "log4cxx/logger.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
}

namespace
{
    log4cxx::LoggerPtr logger(log4cxx::Logger::getLogger("zoombrd.MediaWriter"));
}

namespace avtools
{
    //=====================================================
    //
    //MediaWriter Implementation
    //
    //=====================================================
    class MediaWriter::Implementation
    {
    private:
        FormatContext formatCtx_;                   ///< format context for the output file
        CodecContext codecCtx_;                     ///< codec context for the output video stream
        Packet pkt_;                                ///< Packet to use for encoding frames

//        const int COMPLIANCE = FF_COMPLIANCE_NORMAL; //alternatively, to allow experimental codecs use FF_COMPLIANCE_EXPERIMENTAL
        const int COMPLIANCE = FF_COMPLIANCE_STRICT;
//        /// Ctor
//        Implementation(
//            const std::string& url,
//            const AVCodecParameters& codecParam,
//            const TimeBaseType& timebase,
//            bool allowExperimentalCodecs=false
//        ):
//        hCtx_( allocateOutputFormatContext(url) ),
//        hCC_( avtools::allocateCodecContext() ),
//        hPkt_( avtools::allocatePacket() )
//        {
////            av_register_all();
//            if (!hCtx_ || !hCC_ || !hPkt_)
//            {
//                throw MediaError("Unable to allocate contexts.");
//            }
//            avtools::initPacket(hPkt_);
//
//            //Open the output file
//            if ( !(hCtx_->flags & AVFMT_NOFILE) )
//            {
//                int ret = avio_open(&hCtx_->pb, url.c_str(), AVIO_FLAG_WRITE);
//                if (ret < 0)
//                {
//                    throw MediaError("Could not open " + url, ret);
//                }
//                assert(hCtx_->pb);
//            }
//            LOGD("MediaWriter: Opened output stream ", url, " in ", hCtx_->oformat->long_name, " format.");
//
//            //Add the video stream to the output context
//            // Ensure that the provided container can contain the requested output codec
//            assert (codecParam.codec_type == AVMEDIA_TYPE_VIDEO);
//            const int compliance = (allowExperimentalCodecs ? FF_COMPLIANCE_EXPERIMENTAL : FF_COMPLIANCE_NORMAL);
//            const AVCodecID codecId = codecParam.codec_id;
//            AVOutputFormat* pOutFormat = hCtx_->oformat;
//            assert(pOutFormat);
//            int ret = avformat_query_codec(pOutFormat, codecId, compliance);
//            if ( ret <= 0 )
//            {
//                throw MediaError("File format " + std::string(pOutFormat->name) + " is unable to store " + std::to_string(codecId) + " streams.", ret);
//            }
//
//            // Find encoder
//            const AVCodec* pEncoder = avcodec_find_encoder(codecId);
//            if (!pEncoder)
//            {
//                throw MediaError("Cannot find an encoder for " + std::to_string(codecId));
//            }
//            // Set up encoder context
//            ret = avcodec_parameters_to_context(hCC_.get(), &codecParam);
//            if (ret < 0)
//            {
//                throw MediaError("Unable to copy codec parameters to encoder context.", ret);
//            }
//            AVDictionary *opts = nullptr;
//
//            //Initialize codec context
//            hCC_->strict_std_compliance = compliance;
//            hCC_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER; // let codec know if we are using global header
//            hCC_->time_base = timebase;        // Set timebase
//            if (AV_CODEC_ID_H264 == codecId)   // ffmpeg has issues with x264, and the default presets are broken. These need to be manually fixed for encoding
//            {
//                int losses = 0;
//                hCC_->pix_fmt = avcodec_find_best_pix_fmt_of_list(pEncoder->pix_fmts, (AVPixelFormat) codecParam.format, false, &losses);
//                if (losses)
//                {
//                    LOGW("StreamEncoder: Converting picture format from ", (AVPixelFormat) codecParam.format, " to output format ", hCC_->pix_fmt, "will result in losses due to: ");
//                    if (losses & FF_LOSS_RESOLUTION)
//                    {
//                        LOGD("\tresolution change");
//                    }
//                    if (losses & FF_LOSS_DEPTH)
//                    {
//                        LOGD("\tcolor depth change");
//                    }
//                    if (losses & FF_LOSS_COLORSPACE)
//                    {
//                        LOGD("\tcolorspace conversion");
//                    }
//                    if (losses & FF_LOSS_COLORQUANT)
//                    {
//                        LOGD("\tcolor quantization");
//                    }
//                    if (losses & FF_LOSS_CHROMA)
//                    {
//                        LOGD("\tloss of chroma");
//                    }
//                }
//                hCC_->qmin = 10;
//                hCC_->qmax = 51;
//                hCC_->qcompress = 0.6;
//                hCC_->max_qdiff = 4;
//                hCC_->me_range = 3;
//                hCC_->max_b_frames = 3;
//                //hCC_->bit_rate = 1000000;
//                hCC_->refs = 3;
//                ret = av_dict_set(&opts, "profile", "main", 0);   //also initializes opts
//                if (ret < 0)
//                {
//                    LOGE("StreamEncoder: Unable to set profile for H264 encoder: ", av_err2str(ret));
//                }
//                ret = av_dict_set(&opts, "preset", "medium", 0);
//                if (ret < 0)
//                {
//                    LOGE("StreamEncoder: Unable to set preset for H264 encoder: ", av_err2str(ret));
//                }
//                ret = av_dict_set(&opts, "level", "3.1", 0);
//                if (ret < 0)
//                {
//                    LOGE("StreamEncoder: Unable to set level for H264 encoder: ", av_err2str(ret));
//                }
//                ret = av_dict_set(&opts, "mpeg_quant", "0", 0);
//                if (ret < 0)
//                {
//                    LOGE("StreamEncoder: Unable to set mpeg quantization for H264 encoder: ", av_err2str(ret));
//                }
//                assert(opts);
//            }
//
//            // open codec for encoding
//            ret = avcodec_open2(hCC_.get(), pEncoder, &opts);
//            if (ret < 0)
//            {
//                throw MediaError("Unable to open encoder for codec " + std::to_string(codecId), ret);
//            }
//            assert( avcodec_is_open(hCC_.get()) );
//            LOGD("StreamEncoder: Opened encoder for ", avtools::getCodecInfo(hCC_.get()));
//
//            // Add stream
//            AVStream* pStr = avformat_new_stream(hCtx_.get(), pEncoder);
//            if ( !pStr )
//            {
//                throw MediaError("Unable to add stream for " + std::to_string(codecId));
//            }
//
//            //copy codec params to stream
//            avcodec_parameters_from_context(pStr->codecpar, hCC_.get());
//            pStr->time_base = timebase;
//
//            assert( pStr->codecpar && (pStr->codecpar->codec_id == codecId) && (pStr->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) );
//            assert( (hCtx_->nb_streams == 1) && (pStr == hCtx_->streams[0]) );
//            LOGD("MediaWriter: Opened ", *pStr);
//            // Write stream header
//            ret = avformat_write_header(hCtx_.get(), nullptr);
//            if (ret < 0)
//            {
//                close();
//                throw MediaError("Error occurred when writing output stream header.", ret);
//            }
//            LOGD("MediaWriter: Opened output container ", hCtx_->url);
//#ifndef NDEBUG
//            avtools::dumpContainerInfo(hCtx_.get(), true);
//#endif
//        }
    public:

        Implementation(
            const std::string& url,
            const CodecParameters& codecParam,
            const TimeBaseType& timebase,
            Dictionary& opts
        ):
        formatCtx_(FormatContext::OUTPUT),
        codecCtx_(codecParam),
        pkt_()
        {
            assert (codecParam->codec_type == AVMEDIA_TYPE_VIDEO);
            avdevice_register_all();    // Register devices

            // Find encoder
            const AVCodecID codecId = codecCtx_->codec_id;
            //Init outout format context, open output file or stream
            int ret = avformat_alloc_output_context2(&formatCtx_.get(), nullptr, nullptr, url.c_str());
            if (ret < 0)
            {
                throw MediaError("Unable to allocate output context.", ret);
            }

            //Test that container can store this codec.
            AVOutputFormat* pOutFormat = formatCtx_->oformat;
            assert(pOutFormat);
            ret = avformat_query_codec(pOutFormat, codecId, COMPLIANCE);
            if ( ret <= 0 )
            {
                throw MediaError("File format " + std::string(pOutFormat->name) + " is unable to store " + std::to_string(codecId) + " streams.", ret);
            }
            codecCtx_->codec_tag = av_codec_get_tag(pOutFormat->codec_tag, codecCtx_->codec_id);
            LOG4CXX_DEBUG(logger, "MediaWriter will use a " << pOutFormat->name << " container to store " << codecId << " encoded video." );

            // Open IO Context
            if ( !(formatCtx_->flags & AVFMT_NOFILE) )
            {
                ret = avio_open2(&formatCtx_->pb, url.c_str(), AVIO_FLAG_WRITE, nullptr, &opts.get());
                if (ret < 0)
                {
                    throw MediaError("Could not open " + url, ret);
                }
                assert(formatCtx_->pb);
            }
            LOG4CXX_DEBUG(logger, "MediaWriter: Opened output file " << url << " in " << pOutFormat->long_name << " format.");
            //Add the video stream to the output context
            // Set up encoder context
//            ret = avcodec_parameters_to_context(codecCtx_.get(), codecParam.get());
//            if (ret < 0)
//            {
//                throw MediaError("Unable to copy codec parameters to encoder context.", ret);
//            }

            //Initialize codec context
            const AVCodec* pEncoder = avcodec_find_encoder(codecId);
            if (!pEncoder)
            {
                throw MediaError("Cannot find an encoder for " + std::to_string(codecId));
            }

            codecCtx_->strict_std_compliance = COMPLIANCE;
            codecCtx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;    // let codec know we are using global header
            codecCtx_->time_base = timebase;                    // Set timebase
            codecCtx_->bit_rate_tolerance = 0;
            codecCtx_->rc_max_rate = 0;
            codecCtx_->rc_buffer_size = 0;
            codecCtx_->gop_size = 5;
            codecCtx_->max_b_frames = 0;
            codecCtx_->me_cmp = 1;
            codecCtx_->me_range = 16;
            codecCtx_->me_subpel_quality = 5;
            codecCtx_->qmin = 10;
            codecCtx_->qmax = 51;
            codecCtx_->i_quant_factor = 0.71;
            codecCtx_->qcompress = 0.6;
            codecCtx_->max_qdiff = 4;
            av_opt_set(codecCtx_->priv_data, "profile", "high422", 0);
            av_opt_set(codecCtx_->priv_data, "preset", "ultrafast", 0);
            av_opt_set(codecCtx_->priv_data, "tune", "zerolatency", 0);
            av_opt_set(codecCtx_->priv_data, "b_frame_strategy", "1", 0);
            av_opt_set(codecCtx_->priv_data, "coder_type", "1", 0);
            av_opt_set(codecCtx_->priv_data, "scenechange_threshold", "40", 0);

//            codecCtx_->pix_fmt = avcodec_default_get_format(codecCtx_.get(), pEncoder->pix_fmts);
//            codecCtx_->pix_fmt = AVPixelFormat::AV_PIX_FMT_YUV420P;
            int losses = 0;
            codecCtx_->pix_fmt = avcodec_find_best_pix_fmt_of_list(pEncoder->pix_fmts, (AVPixelFormat) codecParam->format, false, &losses);
            if (codecCtx_->pix_fmt != codecParam->format)
            {
                LOG4CXX_INFO(logger, "Setting output pixel format to " << codecCtx_->pix_fmt)
            }

            ret = avcodec_open2(codecCtx_.get(), pEncoder, &opts.get());
            if (ret < 0)
            {
                throw MediaError("Unable to open encoder context", ret);
            }
            assert( codecCtx_.isOpen() );
            LOG4CXX_DEBUG(logger, "MediaWriter: Opened encoder for " << codecCtx_.info());

            // Add stream
            AVStream* pStr = avformat_new_stream(formatCtx_.get(), pEncoder);
            if ( !pStr )
            {
                throw MediaError("Unable to add stream for " + std::to_string(codecId));
            }

            //copy codec params to stream
            avcodec_parameters_from_context(pStr->codecpar, codecCtx_.get());
            pStr->time_base = timebase;

            assert( pStr->codecpar && (pStr->codecpar->codec_id == codecId) && (pStr->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) );
            assert( (formatCtx_->nb_streams == 1) && (pStr == formatCtx_->streams[0]) );
            LOG4CXX_DEBUG(logger, "MediaWriter: Opened " << *pStr);
            // Write stream header
            ret = avformat_write_header(formatCtx_.get(), nullptr);
            if (ret < 0)
            {
                avtools::CharBuf buf(AV_FOURCC_MAX_STRING_SIZE);
                LOG4CXX_DEBUG(logger, "Output codec: " << av_fourcc_make_string(buf.get(), codecCtx_->codec_tag) << ":" << codecCtx_->codec_id);
                throw MediaError("Error occurred when writing output stream header.", ret);
            }
            LOG4CXX_DEBUG(logger, "MediaWriter: Opened output file " << formatCtx_->url);
#ifndef NDEBUG
            formatCtx_.dumpContainerInfo();
            LOG4CXX_DEBUG(logger, "Unused stream options: " << opts.as_string());
#endif
        }

        Implementation(
                       const std::string& url,
                       const AVCodecParameters& codecParam,
                       const TimeBaseType& timebase,
                       Dictionary& opts
                       ):
        Implementation(url, CodecParameters(&codecParam), timebase, opts)
        {}

        /// Dtor
        ~Implementation()
        {
            try
            {
                write(nullptr);
            }
            catch (std::exception& err)
            {
                LOG4CXX_ERROR(logger, "Error while flushing packets and closing encoder: " << err.what());
            }
            //Write trailer
            av_write_trailer(formatCtx_.get());
            // Close file if output is file
            if ( !(formatCtx_->oformat->flags & AVFMT_NOFILE) )
            {
                avio_close(formatCtx_->pb);
            }
#ifndef NDEBUG
            formatCtx_.dumpContainerInfo();
#endif
        }
        
        /// @return list of opened streams in the output file
        inline const AVStream* stream() const
        {
            assert(formatCtx_ && (formatCtx_->nb_streams == 1) );
            return formatCtx_->streams[0];
        }
        
        /// Writes a frame to a particular stream.
        /// @param[in] pFrame frame data to write
        void write(const AVFrame* pFrame)
        {
            assert( formatCtx_ );
            // send frame to encoder
            int ret = avcodec_send_frame(codecCtx_.get(), pFrame);
            if (ret < 0)
            {
                if (pFrame)
                {
                    throw MediaError("Error sending frames to encoder", ret);
                }
                else
                {
                    throw MediaError("Error flushing encoder", ret);
                }
            }
            pkt_.unref();
            const AVStream* pStr = stream();
            while (true) //write all available packets
            {
                ret = avcodec_receive_packet(codecCtx_.get(), pkt_.get());
                if (ret == AVERROR(EAGAIN))
                {
                    break;  //need to write more frames to get packets
                }
                else if (ret == AVERROR_EOF)
                {
                    break;  //end of file
                }
                else if (ret < 0)
                {
                    throw MediaError("Error reading packets from encoder", ret);
                }
                //Write packet to file
                pkt_->dts = AV_NOPTS_VALUE;  //let the muxer figure this out
                pkt_->stream_index = 0; //only one output stream
                pkt_->pts = av_rescale_q(pkt_->pts, codecCtx_->time_base, pStr->time_base);   //this is necessary since writing the header can change the time_base of the stream.
                pkt_->dts = av_rescale_q(pkt_->dts, codecCtx_->time_base, pStr->time_base);
                pkt_->duration = av_rescale_q(pkt_->duration, codecCtx_->time_base, pStr->time_base);
//                LOGD("Muxing packet for ", *pCtx_->streams[stream]);
//                LOGD("\tpts = ", pkt->pts, ", time_base = ", pCtx_->streams[stream]->time_base, ";\ttime(s) = ", avtools::calculateTime(pkt->pts, pCtx_->streams[stream]->time_base));
        
                //mux encoded frame
                ret = av_interleaved_write_frame(formatCtx_.get(), pkt_.get());
                if (ret < 0)
                {
                    throw MediaError("Error muxing packet", ret);
                }
            }
        }
        
    };  //::avtools::MediaWriter::Implementation
    
    //=====================================================
    //
    //MediaWriter Definitions
    //
    //=====================================================

    MediaWriter::MediaWriter
    (
         const std::string& url,
         const AVCodecParameters& codecParam,
         const TimeBaseType& timebase
    ):
    pImpl_( nullptr )
    {
        Dictionary dict;
        pImpl_ = std::make_unique<Implementation>(url, codecParam, timebase, dict);
        assert ( pImpl_ );
    }

    MediaWriter::MediaWriter
    (
        const std::string& url,
        const AVCodecParameters& codecParam, 
        const TimeBaseType& timebase,
        Dictionary& opts
    ):
    pImpl_( std::make_unique<Implementation>(url, codecParam, timebase, opts) )
    {
        assert ( pImpl_ );
    }

    MediaWriter::MediaWriter
    (
        const std::string& url,
        const CodecParameters& codecParam,
        const TimeBaseType& timebase,
        Dictionary& opts
    ):
    pImpl_( std::make_unique<Implementation>(url, codecParam, timebase, opts) )
    {
        assert ( pImpl_ );
    }

    MediaWriter::~MediaWriter() = default;

    const AVStream* MediaWriter::getStream() const
    {
        assert(pImpl_);
        return pImpl_->stream();
    }
    
    void MediaWriter::write(const AVFrame* pFrame)
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
                pImpl_.reset(nullptr);  //close stream
            }
        }
        catch (std::exception& e)
        {
            std::throw_with_nested(MediaError("MediaWriter: Error writing to video stream "));
        }
    }
}   //::ski
