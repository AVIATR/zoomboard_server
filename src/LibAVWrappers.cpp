//
//  LibAVWrappers.cpp
//  rtmp_server
//
//  Created by Ender Tekin on 2/8/19.
//

#include "LibAVWrappers.hpp"
#include "Media.hpp"
#include "log4cxx/logger.h"
#include <sstream>
extern "C" {
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
}

#ifdef av_err2str
#undef av_err2str
#endif
#define av_err2str(errnum) av_make_error_string((char*)__builtin_alloca(AV_ERROR_MAX_STRING_SIZE),AV_ERROR_MAX_STRING_SIZE, errnum)

namespace
{
    using avtools::MediaError;
    log4cxx::LoggerPtr logger(log4cxx::Logger::getLogger("zoombrd.LibAVWrappers"));
//    static const int ALIGNMENT = 32;

    std::string getPacketInfo(const AVPacket* pPkt, int indent)
    {
        assert(pPkt);
        const std::string filler(indent, '\t');
        std::stringstream ss;
        ss << filler << "pts:" << pPkt->pts << std::endl;
        ss << filler << "dts:" << pPkt->dts << std::endl;
        ss << filler << "duration:" << pPkt->duration << std::endl;
        ss << filler << "stream:" << pPkt->stream_index << std::endl;
        ss << filler << "isRefCounted:" << std::boolalpha << (pPkt->buf != nullptr) << std::endl;
        ss << filler << "hasKeyFrame:" << std::boolalpha << (pPkt->flags & AV_PKT_FLAG_KEY) << std::endl;
        return ss.str();
    }

} //<anon>

namespace avtools
{
    // -------------------------------------------------
    // Helper functions
    // -------------------------------------------------
    void initVideoFrame(AVFrame* pFrame, int width, int height, AVPixelFormat format, AVColorSpace cs/*=AVColorSpace::AVCOL_SPC_RGB*/)
    {
        assert(pFrame);
        //set metadata
        pFrame->colorspace = cs;
        pFrame->width = width;
        pFrame->height = height;
        pFrame->format = format;
        //set buffers
        int ret = av_frame_get_buffer(pFrame, 0);
        if (ret < 0)
        {
            throw MediaError("Error allocating data buffers for video frame", ret);
        }
        LOG4CXX_DEBUG(logger, "Initialized frame data at " << static_cast<void*>(pFrame->data[0]) << " with linesize = " << pFrame->linesize[0]);
    }

    // -------------------------------------------------
    // AVFrame wrapper
    // -------------------------------------------------

    Frame::Frame(AVFrame* pFrame, AVMediaType typ/*=AVMEDIA_TYPE_UNKNOWN*/, TimeBaseType tb/*=TimeBaseType{}*/):
    pFrame_(pFrame ? pFrame : av_frame_alloc()),
    type(typ),
    timebase(tb)
    {
        if (!pFrame_)
        {
            throw MediaError("Frame: Unable to allocate frame data.");
        }
        if (!pFrame)
        {
            pFrame_->pts = pFrame_->best_effort_timestamp = AV_NOPTS_VALUE;
        }
    }

    Frame::Frame(int width, int height, AVPixelFormat format, TimeBaseType tb/*=TimeBaseType{}*/, AVColorSpace cs/*=AVColorSpace::AVCOL_SPC_RGB*/):
    Frame(nullptr, AVMediaType::AVMEDIA_TYPE_VIDEO, tb)
    {
        assert(pFrame_);
        try
        {
            initVideoFrame(pFrame_, width, height, format, cs);
            pFrame_->best_effort_timestamp = pFrame_->pts = AV_NOPTS_VALUE;
        }
        catch (std::exception& err)
        {
            av_frame_free(&pFrame_);
            std::throw_with_nested(MediaError("Frame: Unable to initialize video frame."));
        }
    }
    
    Frame::Frame(const AVCodecParameters& cPar, TimeBaseType tb/*=TimeBaseType{}*/):
    Frame(nullptr, cPar.codec_type, tb)
    {
        try
        {
            switch (type)
            {
                case AVMEDIA_TYPE_VIDEO:
                    initVideoFrame(pFrame_, cPar.width, cPar.height, (AVPixelFormat) cPar.format, cPar.color_space);
                    break;
                default:    //may implement other types in the future
                    throw std::invalid_argument("Frames of type " + std::to_string(cPar.codec_type) + " are not implemented.");
            }
        }
        catch (std::exception& err)
        {
            if (pFrame_)
            {
                av_frame_free(&pFrame_);
            }
            std::throw_with_nested(MediaError("Frame: Unable to initialize frame."));
        }
    }

    Frame::Frame(const CodecParameters& cPar, TimeBaseType tb/*=TimeBaseType{}*/):
    Frame(*cPar.get(), tb)
    {
    }

    Frame::Frame(const Frame& frame):
    pFrame_( av_frame_clone(frame.get()) ),
    type(frame.type),
    timebase(frame.timebase)
    {
        if (!pFrame_)
        {
            throw MediaError("Frame: Unable to copy construct frame.");
        }
    }

    Frame Frame::clone() const
    {
        Frame out(pFrame_->width, pFrame_->height, (AVPixelFormat) pFrame_->format, timebase, pFrame_->colorspace);
        clone(out);
        return out;
    }

    void Frame::clone(Frame& frame) const
    {
        assert(frame);
        assert ( (frame->width == pFrame_->width) && (frame->height == pFrame_->height)
                && (frame->format == pFrame_->format) && (frame->colorspace == pFrame_->colorspace)
                && (frame.type == type) && (av_cmp_q(timebase, frame.timebase) == 0) );
        int ret = av_frame_copy_props(frame.get(), pFrame_);
        if (ret < 0)
        {
            throw MediaError("Unable to copy frame properties to cloned frame", ret);
        }
        ret = av_frame_copy(frame.get(), pFrame_);
        if (ret < 0)
        {
            throw MediaError("Unable to copy frame data to cloned frame", ret);
        }
    }

    Frame::~Frame()
    {
        if (pFrame_)
        {
            av_frame_free(&pFrame_);
        }
    }
    
    Frame& Frame::operator=(const Frame& frame)
    {
        if (pFrame_)
        {
            av_frame_unref(pFrame_);
        }
        type = AVMediaType::AVMEDIA_TYPE_UNKNOWN;
        int ret = av_frame_ref(pFrame_, frame.get());
        if (ret < 0)
        {
            throw MediaError("Frame: Unable to add reference to frame.", ret);
        }
        type = frame.type;
        timebase = frame.timebase;
        return *this;
    }

    std::string Frame::info(int indent/*=0*/) const
    {
        assert(pFrame_);
        return getFrameInfo(pFrame_, type, indent) \
        +  std::string(indent, '\t') + "time: " + std::to_string(calculateTime(pFrame_->best_effort_timestamp, timebase)) \
        + "s [timebase=" + std::to_string(timebase) + "]\n";
    }

    // -------------------------------------------------
    // AVPacket wrapper
    // -------------------------------------------------
    Packet::Packet(const AVPacket* pPkt/*=nullptr*/):
    pPkt_(pPkt ? av_packet_clone(pPkt) : av_packet_alloc())
    {
        if (!pPkt_)
        {
            throw MediaError("Packet: Unable to allocate or clone packet");
        }
        if (!pPkt)  //freshly allocated packet
        {
            av_init_packet(pPkt_);
            pPkt_->data = nullptr;  //no data buffers allocated yet
            pPkt_->size = 0;
        }
    }

    Packet::Packet(const Packet& pkt):
    Packet(pkt.get())
    {
    }

    Packet::Packet(std::uint8_t* data, int len):
    Packet(nullptr)
    {
        int ret = av_packet_from_data(pPkt_, data, len);
        if (ret < 0)
        {
            if (pPkt_)
            {
                av_packet_free(&pPkt_);
            }
            throw MediaError("Packet: Unable to allocate packet from data.", ret);
        }
    }

    Packet::~Packet()
    {
        if (pPkt_)
        {
            av_packet_free(&pPkt_);
        }
    }
    
    void Packet::unref()
    {
        av_packet_unref(pPkt_);
        pPkt_->stream_index = -1;
    }

    std::string Packet::info(int indent/*=0*/) const
    {
        assert(pPkt_);
        return getPacketInfo(pPkt_, indent);
    }

    // -------------------------------------------------
    // AVDictionary wrapper
    // -------------------------------------------------

    Dictionary::Dictionary(const AVDictionary* pDict/*=nullptr*/):
    pDict_( nullptr )
    {
        if (pDict)
        {
            int ret = av_dict_copy(&pDict_, pDict, 0);
            if (ret < 0)
            {
                throw MediaError("Unable to clone dictionary.");
            }
            assert(pDict_);
        }
    }

    Dictionary::~Dictionary()
    {
        if (pDict_)
        {
            av_dict_free(&pDict_);
        }
    }
    
    void Dictionary::add(const std::string& key, const std::string& value)
    {
        int ret = av_dict_set(&pDict_, key.c_str(), value.c_str(), 0);
        if (ret < 0)
        {
            throw MediaError("Unable to add key " + key + " to dictionary", ret);
        }
    }
    
    void Dictionary::add(const std::string& key, TimeType value)
    {
        int ret = av_dict_set_int(&pDict_, key.c_str(), value, 0);
        if (ret < 0)
        {
            throw MediaError("Unable to add key " + key + " to dictionary", ret);
        }
    }

    void Dictionary::add(const std::string& key, AVRational value)
    {
        int ret = av_dict_set(&pDict_, key.c_str(), std::to_string(value).c_str(), 0);
        if (ret < 0)
        {
            throw MediaError("Unable to add key " + key + " to dictionary", ret);
        }
    }

    void Dictionary::add(const std::string& key, AVPixelFormat value)
    {
        int ret = av_dict_set(&pDict_, key.c_str(), std::to_string(value).c_str(), 0);
        if (ret < 0)
        {
            throw MediaError("Unable to add key " + key + " to dictionary", ret);
        }
    }

    std::string Dictionary::operator[](const std::string& key) const
    {
        assert(pDict_);
        AVDictionaryEntry *pEntry = av_dict_get(pDict_, key.c_str(), nullptr, 0);
        if (!pEntry)
        {
            throw std::out_of_range("Key " + key + " not found in dictionary.");
        }
        return pEntry->value;
    }
    
    bool Dictionary::has(const std::string &key) const
    {
        return (pDict_ ? av_dict_get(pDict_, key.c_str(), nullptr, 0) != nullptr : false);
    }

    Dictionary::operator std::string() const
    {
        CharBuf buf;
        if (pDict_)
        {
            int ret = av_dict_get_string(pDict_, &buf.get(), '=', ':');
            if (ret < 0)
            {
                throw MediaError("Dictionary: Could not get unused options", ret);
            }
        }
        return (buf ? std::string(buf.get()) : std::string());
    }

    Dictionary& Dictionary::operator=(const Dictionary& dict)
    {
        if (pDict_)
        {
            av_dict_free(&pDict_);  //clear previous entries
        }
        if (dict)
        {
            int ret = av_dict_copy(&pDict_, dict.get(), 0); //copy entries from dict
            if (ret < 0)
            {
                throw MediaError("Unable to clone dictionary.");
            }
            assert(pDict_);
        }
        return *this;
    }

    int Dictionary::size() const
    {
        return (pDict_ ? av_dict_count(pDict_) : 0);
    }

    bool Dictionary::empty() const
    {
        return (0 == size());
    }
    // -------------------------------------------------
    // AVCodecContext wrapper
    // -------------------------------------------------
    CodecContext::CodecContext(const AVCodec* pCodec):
    pCC_(avcodec_alloc_context3(pCodec))
    {
        if (!pCC_)
        {
            throw MediaError("CodecContext: Unable to allocate codec context.");
        }
    }

    CodecContext::CodecContext(const AVCodecContext* pCodecCtx):
    CodecContext(pCodecCtx ? pCodecCtx->codec : (AVCodec*) nullptr)
    {
        assert(pCC_);
        if (pCodecCtx)
        {
            CodecParameters param;
            int ret = avcodec_parameters_from_context(param.get(), pCodecCtx);
            if (ret < 0)
            {
                if (pCC_)
                {
                    avcodec_free_context(&pCC_);
                }
                throw MediaError("CodecContext: Unable to clone codec context", ret);
            }
            ret = avcodec_parameters_to_context(pCC_, param.get());
            if (ret < 0)
            {
                if (pCC_)
                {
                    avcodec_free_context(&pCC_);
                }
                throw MediaError("CodecContext: Unable to clone codec context", ret);
            }
        }
    }

    CodecContext::CodecContext(const CodecParameters& cp):
    CodecContext((AVCodec*)nullptr)
    {
        int ret = avcodec_parameters_to_context(pCC_, cp.get());
        if (ret < 0)
        {
            if (pCC_)
            {
                avcodec_free_context(&pCC_);
            }
            throw MediaError("CodecContext: Unable to clone codec parameters", ret);
        }
    }

    CodecContext::CodecContext(const CodecContext& cc):
    CodecContext(CodecParameters(cc))
    {
    }

    CodecContext::~CodecContext()
    {
        if (pCC_)
        {
            avcodec_free_context(&pCC_);
        }
    }

    std::string CodecContext::info(int indent/*=0*/) const
    {
        assert(pCC_);
        auto param = CodecParameters(*this);
        return param.info(indent);
    }

    bool CodecContext::isOpen() const
    {
        return avcodec_is_open(pCC_);
    }

    // -------------------------------------------------
    // AVCodecParameters wrapper
    // -------------------------------------------------
//    CodecParameters::CodecParameters(const AVCodecContext* pCC/*=nullptr*/):
//    pParam_(avcodec_parameters_alloc())
//    {
//        if (!pParam_)
//        {
//            throw MediaError("CodecParameters: Unable to allocate parameters.");
//        }
//        if (pCC)
//        {
//            int ret = avcodec_parameters_from_context(pParam_, pCC);
//            if (ret < 0)
//            {
//                if (pParam_)
//                {
//                    avcodec_parameters_free(&pParam_);
//                }
//                throw MediaError("CodecParameters: Unable to determine codec parameters from codec context", ret);
//            }
//        }
//    }

    CodecParameters::CodecParameters(const AVCodecParameters* pParam/*=nullptr*/):
    pParam_(avcodec_parameters_alloc())
    {
        if (!pParam_)
        {
            throw MediaError("CodecParameters: Unable to allocate parameters.");
        }
        if (pParam)
        {
            int ret = avcodec_parameters_copy(pParam_, pParam);
            if (ret < 0)
            {
                if (pParam_)
                {
                    avcodec_parameters_free(&pParam_);
                }
                throw MediaError("CodecParameters: Unable to clone codec parameters", ret);
            }
        }
    }


    CodecParameters::CodecParameters(const CodecContext& cc):
    CodecParameters()
    {
        assert(pParam_);
        int ret = avcodec_parameters_from_context(pParam_, cc.get());
        if (ret < 0)
        {
            if (pParam_)
            {
                avcodec_parameters_free(&pParam_);
            }
            throw MediaError("CodecParameters: Unable to clone codec parameters", ret);
        }

    }

    CodecParameters::CodecParameters(const CodecParameters& cp):
    CodecParameters(cp.get())
    {
    }

    CodecParameters::~CodecParameters()
    {
        if (pParam_)
        {
            avcodec_parameters_free(&pParam_);
        }
    }
    
    std::string CodecParameters::info(int indent/*=0*/) const
    {
        assert(pParam_);
        if (pParam_->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            const std::string filler(indent, '\t');
            std::stringstream ss;
            ss << filler << "Video codec info:" << std::endl;
            ss << filler << "\tCodec ID: " << pParam_->codec_id << std::endl;
            ss << filler << "\tFormat: " << (AVPixelFormat) pParam_->format << std::endl;
            ss << filler << "\tSize (wxh): " << pParam_->width << "x" << pParam_->height << std::endl;
            ss << filler << "\tPixel Aspect Ratio: " << pParam_->sample_aspect_ratio;
            return ss.str();
        }
        else
        {
            throw std::invalid_argument("Info re: " + std::to_string(pParam_->codec_type) + " codecs is not available.");
        }
    }
    
    // -------------------------------------------------
    // AVFormatContext wrapper
    // -------------------------------------------------
    FormatContext::FormatContext(Type t):
    pCtx_(avformat_alloc_context()),
    type(t)
    {
        if (!pCtx_)
        {
            throw MediaError("Unable to allocate format context.");
        }
    }

    FormatContext::~FormatContext()
    {
        if (pCtx_)
        {
            avformat_free_context(pCtx_);
        }
    }
    
    int FormatContext::nStreams() const noexcept
    {
        assert(pCtx_);
        return pCtx_->nb_streams;
    }
    
    AVStream* FormatContext::getStream(int n)
    {
        assert(pCtx_);
        if ( (n < 0) && (n >= pCtx_->nb_streams) )
        {
            throw std::out_of_range("Stream index " + std::to_string(n) + " is not found in the format context.");
        }
        return pCtx_->streams[n];
    }
    
    const AVStream* FormatContext::getStream(int n) const
    {
        assert(pCtx_);
        if ( (n < 0) && (n >= pCtx_->nb_streams) )
        {
            throw std::out_of_range("Stream index " + std::to_string(n) + " is not found in the format context.");
        }
        return pCtx_->streams[n];
    }

    void FormatContext::dumpStreamInfo(int n)
    {
        assert(pCtx_);
        if ( (n < 0) && (n >= pCtx_->nb_streams) )
        {
            throw std::out_of_range("Stream index " + std::to_string(n) + " is not found in the format context.");
        }
        const int level = av_log_get_level();
        av_log_set_level(AV_LOG_VERBOSE);
        av_dump_format(pCtx_, n, pCtx_->url, type);
        av_log_set_level(level);
    }
    
    void FormatContext::dumpContainerInfo()
    {
        assert(pCtx_);
        const int level = av_log_get_level();
        av_log_set_level(AV_LOG_VERBOSE);
        for (int n = 0; n < pCtx_->nb_streams; ++n)
        {
            av_dump_format(pCtx_, n, pCtx_->url, type);
        }
        av_log_set_level(level);
    }
    
    std::string FormatContext::getStreamInfo(int n, int indent/*=0*/, int isVerbose/*=false*/) const
    {
        const AVStream* pStr = getStream(n);
        assert(pStr->codecpar);
        const std::string filler(indent, '\t');
        std::stringstream ss;
        ss << filler << *pStr << std::endl;
        if (isVerbose)
        {
            ss << filler << "\tDuration:" << calculateStreamDuration(pStr) << " s" << std::endl;
            ss << filler << "\tTime Base: " << pStr->time_base << std::endl;
            ss << getCodecInfo(*pStr->codecpar, indent + 1);
        }
        return ss.str();
    }

    // -------------------------------------------------
    // SwsContext wrapper
    // -------------------------------------------------
    ImageConversionContext::ImageConversionContext(const AVCodecParameters& inParam, const AVCodecParameters& outParam):
    ImageConversionContext(inParam.width, inParam.height, (AVPixelFormat) inParam.format,
                           outParam.width, outParam.height, (AVPixelFormat) outParam.format)
    {
        assert((inParam.codec_type == AVMEDIA_TYPE_VIDEO) && (outParam.codec_type == AVMEDIA_TYPE_VIDEO));
    }

    ImageConversionContext::ImageConversionContext
    (int inW, int inH, AVPixelFormat inFmt, int outW, int outH, AVPixelFormat outFmt):
    pConvCtx_(sws_getContext(inW, inH, inFmt, outW, outH, outFmt, 0, nullptr, nullptr, nullptr))//flags = 0, srcfilter=dstfilter=nullptr, param=nullptr
    {
        assert (sws_isSupportedInput(inFmt) && sws_isSupportedOutput(outFmt));
        if (!pConvCtx_)
        {
            throw MediaError("ImageConversionContext: Unable to initialize scaling context.");
        }
    }

    ImageConversionContext::~ImageConversionContext()
    {
        if (pConvCtx_)
        {
            sws_freeContext(pConvCtx_);
        }
    }

    void ImageConversionContext::convert(const Frame& inFrame, Frame& outFrame)
    {
        assert(pConvCtx_ && inFrame && outFrame);
        int ret = sws_scale(pConvCtx_, inFrame->data, inFrame->linesize, 0, inFrame->height, outFrame->data, outFrame->linesize);
        if (ret < 0)
        {
            throw MediaError("Error converting frame to output format", ret);
        }
        ret = av_frame_copy_props(outFrame.get(), inFrame.get());
        if (ret < 0)
        {
            throw MediaError("Error copying frame properties to output frame", ret);
        }
    }

}
