//
//  MediaWriter.cpp
//  MM
//
//  Created by Ender Tekin on 11/12/14.
//
//

#include "MediaWriter.hpp"
#include "log.hpp"
#include <string>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
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
        FormatContextHandle                 hCtx_;      ///< format context for the output file
        CodecContextHandle                  hCC_;       ///< encoder codec context
        PacketHandle                        hPkt_;      ///< packet to encode the frames in
        
        /// Closes everything
        void close()
        {
            if (hCtx_)
            {
                //pop all delayed packets from encoder
                LOGD("MediaWriter: Flushing delayed packets from encoders.");
                try
                {
                    write(nullptr);
                }
                catch (std::exception& err)
                {
                    LOGE("Error flushing packets and closing encoder: ", err.what());
                }
                //Write trailer
                av_write_trailer(hCtx_.get());
#ifndef NDEBUG
                avtools::dumpContainerInfo(hCtx_.get(), true);
#endif
                LOGD("MediaWriter: Closing stream.");
                
                // Close file if output is file
                if ( !(hCtx_->oformat->flags & AVFMT_NOFILE) )
                {
                    avio_close(hCtx_->pb);
                }
                hCC_.reset(nullptr);
                hPkt_.reset(nullptr);
                hCtx_.reset(nullptr);
            }
        }
        
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
////                        hCC_->bit_rate = 1000000;
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
        Implementation(FormatContextHandle h):
        hCtx_( std::move(h) ),
        hCC_( avtools::allocateCodecContext() ),
        hPkt_( avtools::allocatePacket() )
        {
            if (!hCtx_ || !hCC_ || !hPkt_)
            {
                throw MediaError("Unable to allocate contexts.");
            }
            avtools::initPacket(hPkt_);

            //Add the video stream to the output context
            // Ensure that the provided container can contain the requested output codec
            assert (codecParam.codec_type == AVMEDIA_TYPE_VIDEO);
            const int compliance = (allowExperimentalCodecs ? FF_COMPLIANCE_EXPERIMENTAL : FF_COMPLIANCE_NORMAL);
            const AVCodecID codecId = codecParam.codec_id;
            AVOutputFormat* pOutFormat = hCtx_->oformat;
            assert(pOutFormat);
            int ret = avformat_query_codec(pOutFormat, codecId, compliance);
            if ( ret <= 0 )
            {
                throw MediaError("File format " + std::string(pOutFormat->name) + " is unable to store " + std::to_string(codecId) + " streams.", ret);
            }

            // Find encoder
            const AVCodec* pEncoder = avcodec_find_encoder(codecId);
            if (!pEncoder)
            {
                throw MediaError("Cannot find an encoder for " + std::to_string(codecId));
            }
            // Set up encoder context
            ret = avcodec_parameters_to_context(hCC_.get(), &codecParam);
            if (ret < 0)
            {
                throw MediaError("Unable to copy codec parameters to encoder context.", ret);
            }
            AVDictionary *opts = nullptr;

            //Initialize codec context
            hCC_->strict_std_compliance = compliance;
            hCC_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER; // let codec know if we are using global header
            hCC_->time_base = timebase;        // Set timebase    public:
        }

    public:
        /// Opens and returns a handle to the implementation
        /// @param[in] dict a dictionary to read format/codec parameters from
        /// @return a handle to the imlementation
        static std::unique_ptr<Implementation> Open(const AVDictionary& dict)
        {
            avdevice_register_all();    // Register devices
            //Init outout format context, open output file or stream
            AVFormatContext* pCtx = nullptr;
            AVOutputFormat *pFormat = nullptr;
            // Get the input format
            const AVDictionaryEntry* pContainer = av_dict_get(&dict, "container", nullptr, 0);
            if (pContainer)
            {
                pFormat = av_guess_format(pContainer->value, nullptr, nullptr);
            }
            const AVDictionaryEntry* pUrl = av_dict_get(&dict, "url", nullptr, 0);
            assert(pUrl);    //url must be provided
            AVDictionary* pDict = nullptr;
            int status = av_dict_copy(&pDict, &dict, 0);
            if (status < 0)
            {
                throw MediaError("Unable to clone input dictionary.");
            }

            status = avformat_alloc_output_context2(&pCtx, pFormat, nullptr, pUrl->value);
            if (status < 0)
            {
                throw MediaError("Unable to allocate output context.", status);
            }
            assert(pCtx);
            if ( !(pCtx->flags & AVFMT_NOFILE) )
            {
                status = avio_open2(&pCtx->pb, pUrl->value, AVIO_FLAG_WRITE, nullptr, &pDict);
                if (status < 0)
                {
                    throw MediaError("Could not open " + std::string(pUrl->value), status);
                }
                assert(pCtx->pb);
            }
            LOGD("MediaWriter: Opened output stream ", pUrl->value, " in ", pCtx->oformat->long_name, " format.");
            return std::make_unique<Implementation>(FormatContextHandle(pCtx, [](AVFormatContext* pp) {avformat_free_context(pp); }));


            ////////////
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
                throw MediaError("Unable to clone input dictionary.");
            }
            
            status = avformat_open_input( &p, url, pFormat, &pDict );
            if( status < 0 )  // Couldn't open file
            {
                throw MediaError("Could not open " + std::string(url), status);
            }

            return std::make_unique<Implementation>(dict);
        }
        
        static std::unique_ptr<Implementation> Open(
            const std::string& url,
            const AVCodecParameters& codecParam,
            const TimeBaseType& timebase,
            bool allowExperimentalCodecs=false
        )
        {
            AVDictionary* pOpts = nullptr;
            int ret = av_dict_set(&pOpts, "url", url.c_str(), 0);
            if (ret < 0)
            {
                throw MediaError("Unable to set url in dictionary. ", ret);
            }
            
            avtools::Handle<AVDictionary> hDict(pOpts, [](AVDictionary* p){if (p) av_dict_free(&p);});
            return Open(*pOpts);
        }

        /// Dtor
        ~Implementation()
        {
            close();
        }
        
        /// @return list of opened streams in the output file
        inline const AVStream* stream() const
        {
            assert(hCtx_ && (hCtx_->nb_streams == 1) );
            return hCtx_->streams[0];
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
                    throw MediaError("Error sending frames to encoder", ret);
                }
                else
                {
                    throw MediaError("Error flushing encoder", ret);
                }
            }
            avtools::unrefPacket(hPkt_);
            while (true) //write all available packets
            {
                ret = avcodec_receive_packet(hCC_.get(), hPkt_.get());
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
                hPkt_->dts = AV_NOPTS_VALUE;  //let the muxer figure this out
                hPkt_->stream_index = 0;
                hPkt_->pts = av_rescale_q(hPkt_->pts, hCC_->time_base, stream()->time_base);   //this is necessary since writing the header can change the time_base of the stream.
//                LOGD("Muxing packet for ", *pCtx_->streams[stream]);
//                LOGD("\tpts = ", pkt->pts, ", time_base = ", pCtx_->streams[stream]->time_base, ";\ttime(s) = ", avtools::calculateTime(pkt->pts, pCtx_->streams[stream]->time_base));
        
                //mux encoded frame
                ret = av_interleaved_write_frame(hCtx_.get(), hPkt_.get());
                if (ret < 0)
                {
                    throw MediaError("Error muxing packet", ret);
                }
            }
//            av_log_set_level(AV_LOG_WARNING);
        }
        
    };  //::avtools::MediaWriter::Implementation
    
    //=====================================================
    //
    //MediaWriter Definitions
    //
    //=====================================================
    
    MediaWriter::MediaWriter(
        const std::string& url,
        const AVCodecParameters& codecParam, 
        const TimeBaseType& timebase, 
        bool allowExperimentalCodecs
    ):
    pImpl_( Implementation::Open(url, codecParam, timebase, allowExperimentalCodecs) )
    {
        assert ( pImpl_ );
    }
    
    MediaWriter::MediaWriter(const AVDictionary& dict):
    pImpl_ (Implementation::Open(dict))
    {
        assert( pImpl_);
    }

    MediaWriter::~MediaWriter() = default;

    MediaWriter::Handle MediaWriter::Open(
        const std::string& url, 
        const AVCodecParameters& codecParam, 
        const TimeBaseType& timebase, 
        bool allowExperimentalCodecs
    ) noexcept
    {
        try
        {
            Handle h(new MediaWriter(url, codecParam, timebase, allowExperimentalCodecs));
            assert(h);
            return h;
        }
        catch (std::exception& err)
        {
            LOGE("MediaWriter: Unable to open ", url, ":", err.what());
            return nullptr;
        }
    }
        
    MediaWriter::Handle MediaWriter::Open(const AVDictionary& dict) noexcept
    {
        try
        {
            Handle h(new MediaWriter(dict));
            assert(h);
            return h;
        }
        catch (std::exception& err)
        {
            LOGE("MediaWriter: Unable to open MediaWriter: ", err.what());
            return nullptr;
        }
    }
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
                pImpl_.reset(nullptr);
            }
        }
        catch (std::exception& e)
        {
            std::throw_with_nested(MediaError("MediaWriter: Error writing to video stream "));
        }
    }
}   //::ski
