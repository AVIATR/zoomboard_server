//
//  MediaWriter.cpp
//  MM
//
//  Created by Ender Tekin on 11/12/14.
//
//

#include "MediaWriter.hpp"
#include "Media.hpp"
#include <string>
#include "log4cxx/logger.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavutil/parseutils.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
}

namespace
{
    log4cxx::LoggerPtr logger(log4cxx::Logger::getLogger("zoombrd.MediaWriter"));

    /// Adds a filter to a graph, and returns the corresponding filter context
    /// Arguments can then be passed by setting the corresponding flags in the filter context
    /// @param[in] filter type filter type to use, see https://libav.org/documentation/libavfilter.html
    /// @param[in] name name of the filter in the graph
    /// @param[in] pGraph ptr to the grap hto add the filter to
    /// @return an initialized filter context for this filter.
    [[maybe_unused]]
    AVFilterContext* addFilterToGraph(const std::string& filterType, const std::string& name, const avtools::Dictionary& args, AVFilterGraph* pGraph)
    {
        assert(pGraph);
        AVFilterContext* pCtx = nullptr;
        const AVFilter *pF  = avfilter_get_by_name(filterType.c_str());
        if (!pF)
        {
            throw std::runtime_error("Unable to find " + filterType + " filter");
        }

        const std::string filterArgs = (std::string) args;
        LOG4CXX_DEBUG(logger, "Adding " << filterType << " filter to graph with arguments " << filterArgs);
        int ret = avfilter_graph_create_filter(&pCtx, pF, name.c_str(), filterArgs.c_str(), NULL, pGraph);
        if (ret < 0)
        {
            throw avtools::MediaError("Unable to add " + filterType + " filter to filter graph", ret);
        }
        const int nFilters = pGraph->nb_filters;
        assert(pCtx == pGraph->filters[nFilters-1]);
        if (nFilters > 1)
        {
            AVFilterContext* pPrevFilter = pGraph->filters[nFilters-2];
            ret = avfilter_link(pPrevFilter, 0, pCtx, 0);
            if (ret < 0)
            {
                throw avtools::MediaError("Unable to link " + std::string(pPrevFilter->name) + " to " + name, ret);
            }
        }

        return pCtx;
    }
}   //::<anon>

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
        Frame filtFrame_;                           ///< filter that is read from the filtergraph
        Packet pkt_;                                ///< Packet to use for encoding frames
        AVFilterInOut *pIn_, *pOut_;                ///< filtergraph inputs/outputs
        AVFilterGraph *pGraph_;                     ///< filtergraph

        /// Initializes the filter graph
        /// @param[in] pFrame input frame
        /// @param[in] timebase timebase for the incoming frame's timestamps
        void initFilterGraph(const AVFrame* pFrame, avtools::TimeBaseType timebase)
        {
            assert( pIn_ && pOut_ && pGraph_);
            assert(pFrame);
            const AVStream* pStr = stream();
            assert(pStr);
            const AVCodecParameters *pCodecPar = pStr->codecpar;
            assert(pCodecPar);
            // Init source
            pIn_->name = av_strdup("input");
            pIn_->pad_idx = 0;
            pIn_->next = nullptr;
            {
                Dictionary args;
                args.add("width", pFrame->width);
                args.add("height", pFrame->height);
                args.add("time_base", timebase);
                args.add("sar", pFrame->sample_aspect_ratio);
                args.add("pix_fmt", (AVPixelFormat) pFrame->format);
                pIn_->filter_ctx = addFilterToGraph("buffer", "input", args, pGraph_);
                LOG4CXX_DEBUG(logger, "Added source to filtergraph");
            }

            // Convert framerate
            {
                Dictionary args;
                args.add("fps", pStr->avg_frame_rate);
                addFilterToGraph("fps", "change framerate", args, pGraph_);
                LOG4CXX_DEBUG(logger, "Added fps filter to convert to " << pStr->avg_frame_rate << "fps");
            }

            // Init scale filter if input & output have different sizes
            if ( (pFrame->width != pCodecPar->width) || (pFrame->height != pCodecPar->height) )
            {
                {
                    Dictionary scaleArgs, padArgs;
                    float cW = (float) pCodecPar->width / (float) pFrame->width;
                    float cH = (float) pCodecPar->height / (float) pFrame->height;
                    if ( cW > cH )
                    {
                        scaleArgs.add("w", -1);
                        scaleArgs.add("h", pCodecPar->height);
                        float pad = (pCodecPar->width - cH * pFrame->width) / 2.f;
                        padArgs.add("w", pCodecPar->width);
                        padArgs.add("h", pCodecPar->height);
                        padArgs.add("x", pad > 0 ? (int) pad : 0);
                        padArgs.add("y", 0);
                    }
                    else if ( cW < cH )
                    {
                        scaleArgs.add("w", pCodecPar->width);
                        scaleArgs.add("h", -1);
                        float pad = (pCodecPar->height - cW * pFrame->height) / 2.f;
                        padArgs.add("w", pCodecPar->width);
                        padArgs.add("h", pCodecPar->height);
                        padArgs.add("x", 0);
                        padArgs.add("y", pad > 0 ? (int) pad : 0);
                    }
                    else
                    {
                        scaleArgs.add("w", pCodecPar->width);
                        scaleArgs.add("h", pCodecPar->height);
                    }
                    addFilterToGraph("scale", "resize", scaleArgs, pGraph_);
                    LOG4CXX_DEBUG(logger, "Added scale filter to convert from "
                                  << pFrame->width << "x" << pFrame->height << " to "
                                  << cW * pFrame->width << "x" << cW * pFrame->height);
                    //Pad output if need be to match the requested output size
                    if ( !padArgs.empty() )
                    {
                        addFilterToGraph("pad", "add_padding", padArgs, pGraph_);
                        LOG4CXX_DEBUG(logger, "Added scale filter to pad to "
                                      << pCodecPar->width << "x" << pCodecPar->height);
                    }
                }
            }
            if ( pFrame->format != pCodecPar->format ) // Init format filter if needed. If scale filter is active, this is automatic
            {
                {
                    Dictionary args;
                    args.add("pix_fmts", pCodecPar->format);
                    addFilterToGraph("format", "change format", args, pGraph_);
                    LOG4CXX_DEBUG(logger, "Added format filter to convert from " << (AVPixelFormat) pFrame->format
                                  << " to " << (AVPixelFormat) pCodecPar->format);
                }
            }

            // Init aspect ratio filter if input & output have different sample aspect ratios -> this is pixel-wise!
            if ( 0 != av_cmp_q(pFrame->sample_aspect_ratio, pCodecPar->sample_aspect_ratio) )
            {
                {
                    Dictionary args;
                    args.add("sar", pCodecPar->sample_aspect_ratio);
                    addFilterToGraph("setsar", "adjust aspect", args, pGraph_);
                    LOG4CXX_DEBUG(logger, "Added setsar filter to convert aspect ratio from " << pFrame->sample_aspect_ratio << " to " << pCodecPar->sample_aspect_ratio);
                }
            }

            //Init sink
            pOut_->name = av_strdup("output");
            pOut_->pad_idx = 0;
            pOut_->next = nullptr;
            {
                Dictionary args;
                pOut_->filter_ctx = addFilterToGraph("buffersink", "output", args, pGraph_);
                LOG4CXX_DEBUG(logger, "Added sink filter to graph");
            }

            // Configure links
            int ret = avfilter_graph_config(pGraph_, nullptr);
            if (ret < 0)
            {
                throw MediaError("Unable to configure filter graph", ret);
            }

#ifndef NDEBUG
            const char* graphDesc = avfilter_graph_dump(pGraph_, nullptr);
            if (!graphDesc)
            {
                throw std::runtime_error("Unable to get graph description");
            }
            LOG4CXX_DEBUG(logger, "Filter graph initialized:\n" << graphDesc);
            av_freep( &graphDesc);
#endif
        }

        /// Encoders & multiplexes a video frame
        /// @param[in] pFrame video frame to write
        void encodeFrame(const AVFrame* pFrame)
        {
            assert(stream());
            LOG4CXX_DEBUG(logger, "Writer encoding frame");
            const avtools::TimeBaseType timebase = stream()->time_base;
            // Encode frame -> send frame to encoder, then read available packets & mux them.
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
            assert(0 == ret);
            while (true) //read all available packets from encoder, and mux them
            {
                pkt_.unref();
                LOG4CXX_DEBUG(logger, "Writer reading packet from encoder");
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
                assert(0 == ret);

                pkt_->stream_index = 0; //only one output stream
                av_packet_rescale_ts(pkt_.get(), codecCtx_->time_base, timebase);
//                pkt_->dts = AV_NOPTS_VALUE;  //let the muxer figure this out
//                pkt_->pts = av_rescale_q(pkt_->pts, codecCtx_->time_base, timebase);   //this is necessary since writing the header can change the time_base of the stream.
//                pkt_->duration = av_rescale_q(pkt_->duration, codecCtx_->time_base, timebase);
                LOG4CXX_DEBUG(logger, "Muxing packet to " << url() << ":\n " << pkt_.info(1));
                //mux encoded frame
//                ret = av_interleaved_write_frame(formatCtx_.get(), pkt_.get());
                ret = av_write_frame(formatCtx_.get(), pkt_.get()); //only one stream
                if (ret < 0)
                {
                    throw MediaError("Error muxing packet", ret);
                }
                assert(0 == ret);
            }
        }

    public:

        Implementation(
            const std::string& url,
            Dictionary& codecOpts,
            Dictionary& muxerOpts
        ):
        formatCtx_(FormatContext::OUTPUT),
        codecCtx_((AVCodec*) nullptr),
        filtFrame_(),
        pkt_(),
        pIn_(avfilter_inout_alloc()),
        pOut_(avfilter_inout_alloc()),
        pGraph_( avfilter_graph_alloc() )
        {
            // Initialize filtergraph
            if (!pIn_ || !pOut_)
            {
                throw std::runtime_error("Unable to initialize filtergraph inputs");
            }
            if ( !pGraph_)
            {
                throw std::runtime_error("Unable to initialize filtergraph");
            }

            //Init output format context, open output file or stream
            int ret = avformat_alloc_output_context2(&formatCtx_.get(), nullptr, nullptr, url.c_str());
            if (ret < 0)
            {
                throw MediaError("Unable to allocate output context.", ret);
            }

            //Test that container can store this codec.
            AVOutputFormat* pOutFormat = formatCtx_->oformat;
            assert(pOutFormat);

            LOG4CXX_DEBUG(logger, "Attempting to set muxer options:\n" << muxerOpts);
            ret = av_opt_set_dict(formatCtx_.get(), &muxerOpts.get());
            if (0 != ret)
            {
                throw MediaError("Unable to set muxer options", ret);
            }
            ret = av_opt_set_dict(formatCtx_->priv_data, &muxerOpts.get());
            if (0 != ret)
            {
                throw MediaError("Unable to set private muxer options", ret);
            }
#ifndef NDEBUG
            LOG4CXX_DEBUG(logger, "Unused muxer private options:\n" << muxerOpts);
            {
                avtools::CharBuf buf;
                ret = av_opt_serialize(formatCtx_.get(), AV_OPT_FLAG_ENCODING_PARAM, 0, &buf.get(), ':', '\n');
                if (ret < 0)
                {
                    throw avtools::MediaError("Unable to serialize muxer options", ret);
                }
                LOG4CXX_DEBUG(logger, "Available muxer options:\n" << buf.get());
                ret = av_opt_serialize(formatCtx_->priv_data, AV_OPT_FLAG_ENCODING_PARAM, 0, &buf.get(), ':', '\n');
                if (ret < 0)
                {
                    throw avtools::MediaError("Unable to serialize muxer private options", ret);
                }
                LOG4CXX_DEBUG(logger, "Available muxer private options:\n" << buf.get());
            }
#endif

            // Open IO Context for writing to the file
            if ( !(formatCtx_->flags & AVFMT_NOFILE) )
            {
                ret = avio_open(&formatCtx_->pb, url.c_str(), AVIO_FLAG_WRITE);
                if (ret < 0)
                {
                    throw MediaError("Could not open " + url, ret);
                }
                assert(formatCtx_->pb);
            }
            LOG4CXX_DEBUG(logger, "MediaWriter: Opened output file " << url << " in " << pOutFormat->long_name << " format.");
            LOG4CXX_DEBUG(logger, "Format context compliance: " << formatCtx_->strict_std_compliance);

            // Find encoder
            const AVCodecDescriptor* pCodecDesc = nullptr;
            if (!codecOpts.has("name"))   //none specified, find first compatible codec
            {
                while ( (pCodecDesc = avcodec_descriptor_next(pCodecDesc)) )
                {
                    if (avformat_query_codec(pOutFormat, pCodecDesc->id, formatCtx_->strict_std_compliance)
                        && pCodecDesc->type == AVMEDIA_TYPE_VIDEO)
                    {
                        LOG4CXX_INFO(logger, "No codec was specified, will use " << pCodecDesc->name);
                        break;
                    }
                }
                if (!pCodecDesc)
                {
                    throw std::runtime_error("Unable to find a suitable codec for " + std::string(pOutFormat->long_name) + " container.");
                }
            }
            else
            {
                std::string encoderName = codecOpts["name"];
                pCodecDesc = avcodec_descriptor_get_by_name(encoderName.c_str());
                if (!pCodecDesc)
                {
                    throw std::runtime_error("Unable to find a descriptor for codec " + encoderName);
                }
                ret = avformat_query_codec(pOutFormat, pCodecDesc->id, formatCtx_->strict_std_compliance);
                if ( ret <= 0 )
                {
                    throw MediaError("File format " + std::string(pOutFormat->name) + " is unable to store " + std::string(pCodecDesc->name) + " streams.", ret);
                }
                LOG4CXX_DEBUG(logger, "Using " << pCodecDesc->id << " codec.");
            }
            codecOpts.add("codec_tag", av_codec_get_tag(pOutFormat->codec_tag, pCodecDesc->id));

            //Initialize codec context
            LOG4CXX_DEBUG(logger, "MediaWriter will use a " << pOutFormat->name << " container to store " << pCodecDesc->name << " encoded video." );
             // let codec know if we are using global header
            if (formatCtx_->oformat->flags & AVFMT_GLOBALHEADER)
            {
                codecCtx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            }

            // Open encoder
            const AVCodec* pEncoder = avcodec_find_encoder(pCodecDesc->id);
            if (!pEncoder)
            {
                throw MediaError("Cannot find an encoder for " + std::string(pCodecDesc->name));
            }

            // Set the timebase
            //get frame rate
            AVRational framerate;
            assert(muxerOpts.has("framerate"));
            ret = av_parse_video_rate(&framerate, muxerOpts["framerate"].c_str());
            if (ret < 0)
            {
                throw MediaError("Unable to parse frame rate from muxer options: " + muxerOpts["framerate"], ret);
            }

            codecCtx_->time_base = av_inv_q(framerate);;                    // Set timebase
            LOG4CXX_DEBUG(logger, "Setting time base to " << codecCtx_->time_base);

            assert(codecOpts.has("pixel_format"));
            int losses = 0;
            AVPixelFormat fmt = av_get_pix_fmt(codecOpts["pixel_format"].c_str());
            codecCtx_->pix_fmt = avcodec_find_best_pix_fmt_of_list(pEncoder->pix_fmts, fmt, false, &losses);
            if (codecCtx_->pix_fmt != fmt)
            {
                LOG4CXX_INFO(logger, "Setting output pixel format to " << codecCtx_->pix_fmt)
                codecOpts.set("pixel_format", codecCtx_->pix_fmt);
            }

            ret = avcodec_open2(codecCtx_.get(), pEncoder, &codecOpts.get());
            if (ret < 0)
            {
                throw MediaError("Unable to open encoder context", ret);
            }
            assert( codecCtx_.isOpen() );
            LOG4CXX_DEBUG(logger, "MediaWriter: Opened encoder for " << codecCtx_.info());
#ifndef NDEBUG
            LOG4CXX_DEBUG(logger, "Unused codec options:\n" << codecOpts);
            {
                avtools::CharBuf buf;
                ret = av_opt_serialize(codecCtx_.get(), AV_OPT_FLAG_ENCODING_PARAM, 0, &buf.get(), ':', '\n');
                if (ret < 0)
                {
                    throw avtools::MediaError("Unable to serialize codec options", ret);
                }
                LOG4CXX_DEBUG(logger, "Available codec options:\n" << buf.get());
                ret = av_opt_serialize(codecCtx_->priv_data, AV_OPT_FLAG_ENCODING_PARAM, 0, &buf.get(), ':', '\n');
                if (ret < 0)
                {
                    throw avtools::MediaError("Unable to serialize private codec options", ret);
                }
                LOG4CXX_DEBUG(logger, "Available codec private options:\n" << buf.get());
            }
#endif

            // Add stream
            AVStream* pStr = avformat_new_stream(formatCtx_.get(), pEncoder);
            if ( !pStr )
            {
                throw MediaError("Unable to add stream for " + std::string(pEncoder->name));
            }
            pStr->avg_frame_rate = framerate;
            //copy codec params to stream
            avcodec_parameters_from_context(pStr->codecpar, codecCtx_.get());
            pStr->time_base = codecCtx_->time_base;
            pStr->start_time = AV_NOPTS_VALUE;

            assert( pStr->codecpar && (pStr->codecpar->codec_id == pCodecDesc->id) && (pStr->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) );
            assert( (formatCtx_->nb_streams == 1) && (pStr == formatCtx_->streams[0]) );
            LOG4CXX_DEBUG(logger, "MediaWriter: Opened " << *pStr);
            // Write stream header
            ret = avformat_write_header(formatCtx_.get(), nullptr);
            if (ret < 0)
            {
                throw MediaError("Error occurred when writing output stream header.", ret);
            }
            LOG4CXX_DEBUG(logger, "MediaWriter: Opened output file " << formatCtx_->url);

            // Initialize output frame
            filtFrame_ = Frame(pStr->codecpar->width, pStr->codecpar->height, (AVPixelFormat) pStr->codecpar->format);
#ifndef NDEBUG
            formatCtx_.dumpContainerInfo();
#endif
        }

        /// Dtor
        ~Implementation()
        {
            assert(formatCtx_);
            try
            {
                LOG4CXX_DEBUG(logger, "Flushing writer")
                write(nullptr, TimeBaseType{});
            }
            catch (std::exception& err)
            {
                LOG4CXX_ERROR(logger, "Error while flushing packets and closing encoder: " << err.what());
            }
            //Write trailer
            LOG4CXX_DEBUG(logger, "Writing trailer")
            int ret = av_write_trailer(formatCtx_.get());
            if (ret < 0)
            {
                LOG4CXX_ERROR(logger, "Error writing trailer: " << av_err2str(ret));
            }
            // Close file if output is file
            LOG4CXX_DEBUG(logger, "Closing file")
            if ( formatCtx_->oformat && !(formatCtx_->oformat->flags & AVFMT_NOFILE) )
            {
                ret = avio_close(formatCtx_->pb);
                if (ret < 0)
                {
                    LOG4CXX_ERROR(logger, "Error closing output file " << av_err2str(ret));
                }
            }

            LOG4CXX_DEBUG(logger, "Freeing filter graph")
            avfilter_inout_free(&pIn_);
            avfilter_inout_free(&pOut_);
            avfilter_graph_free(&pGraph_);

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
        /// @param[in] timebase timebase of the incoming frames
        void write(const AVFrame* pFrame, avtools::TimeBaseType timebase)
        {
            assert( formatCtx_ );
            // send frame to encoder
            AVStream* pStr = formatCtx_->streams[0];
            if (pFrame)
            {
                if (AV_NOPTS_VALUE == pStr->start_time) //first frame, set start time
                {
                    pStr->start_time = pFrame->best_effort_timestamp;
                    LOG4CXX_DEBUG(logger, "Setting stream start time to " << pStr->start_time);
                    initFilterGraph(pFrame, timebase);
                }
            }
            else if (AV_NOPTS_VALUE == pStr->start_time)
            {
                LOG4CXX_WARN(logger, "Writer flushing with no frames written.");
                return; //no frames written
            }

            /// Push frame to filtergraph
            LOG4CXX_DEBUG(logger, "Writer pushing frame to filtergraph");
            int ret = av_buffersrc_write_frame(pIn_->filter_ctx, pFrame);
            if (ret < 0)
            {
                throw MediaError("Unable to write frame to filtergraph", ret);
            }

            /// Pop output frames from filtergraph
            while (true)
            {
                LOG4CXX_DEBUG(logger, "Writer reading frames from filtergraph");
                avtools::TimeBaseType outTimebase = av_buffersink_get_time_base(pOut_->filter_ctx);
                assert(filtFrame_);
                av_frame_unref(filtFrame_.get());
                ret = av_buffersink_get_frame(pOut_->filter_ctx, filtFrame_.get());
                if ( (ret == AVERROR(EAGAIN)) || (ret == AVERROR_EOF) )
                {
                    break;
                }
                else if (ret < 0)
                {
                    throw MediaError("Unable to receive frame from filter graph", ret);
                }
                //timestamps should be in terms of the input time_base, convert to output
                filtFrame_->best_effort_timestamp = av_rescale_q(filtFrame_->best_effort_timestamp, outTimebase, codecCtx_->time_base);
                filtFrame_->pts = av_rescale_q(filtFrame_->pts, outTimebase, codecCtx_->time_base);
                filtFrame_->pict_type = AV_PICTURE_TYPE_NONE;   //to let the encoder figure this out
                //encode frame
                encodeFrame(filtFrame_.get());
            }
        }

        std::string url() const
        {
            return formatCtx_->url;
        }
    };  //::avtools::MediaWriter::Implementation
    
    //=====================================================
    //
    //MediaWriter Definitions
    //
    //=====================================================
    MediaWriter::MediaWriter(
        const std::string& url,
        Dictionary& codecOpts,
        Dictionary& muxerOpts
    ):
    pImpl_( std::make_unique<Implementation>(url, codecOpts, muxerOpts) )
    {
        assert( pImpl_);
    }

    MediaWriter::MediaWriter(MediaWriter&& writer):
    pImpl_(std::move(writer.pImpl_))
    {}

    MediaWriter::~MediaWriter() = default;

    const AVStream* MediaWriter::getStream() const
    {
        assert(pImpl_);
        return pImpl_->stream();
    }

    void MediaWriter::write(const AVFrame* pFrame, avtools::TimeBaseType timebase)
    {
        assert( pImpl_ );
        try
        {
            if (pFrame)
            {
                pImpl_->write(pFrame, timebase);
            }
            else
            {
                pImpl_.reset(nullptr);  //close stream, implementation dtor flushes
            }
        }
        catch (std::exception& e)
        {
            std::throw_with_nested(MediaError("MediaWriter: Error writing to video stream"));
        }
    }

    void MediaWriter::write(const Frame& frame)
    {
        assert(frame.type == AVMediaType::AVMEDIA_TYPE_VIDEO);
        write(frame.get(), frame.timebase);
    }

    std::string MediaWriter::url() const
    {
        assert(pImpl_);
        return pImpl_->url();
    }

}   //::avtools
