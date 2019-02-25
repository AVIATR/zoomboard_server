//
//  LibAVWrappers.cpp
//  rtmp_server
//
//  Created by Ender Tekin on 2/8/19.
//

#include "LibAVWrappers.hpp"
#include "Media.hpp"
#include "log.hpp"
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
    
    void initVideoFrame(AVFrame* pFrame, int width, int height, AVPixelFormat format, AVColorSpace cs)
    {
        assert(pFrame);
        //set metadata
        pFrame->colorspace = cs;
        pFrame->width = width;
        pFrame->height = height;
        pFrame->format = format;
        //set buffers
        //        int ret = av_image_alloc(pFrameOut->data, pFrameOut->linesize, width, height, format, ALIGNMENT);
        int ret = av_frame_get_buffer(pFrame, 0);
        if (ret < 0)
        {
            throw MediaError("Error allocating data buffers for video frame", ret);
        }
        LOGD("Allocated frame data at ", static_cast<void*>(pFrame->data), " with linesize = ", pFrame->linesize[0]);
    }
    
    
    /// Prints info re: a video frame
    /// @param[in] pFrame a video frame. If the frame is not a video frame, the results will be inaccurate
    /// @param[in] indent number of tabs to indent by
    /// @return a string containing info re: an audio frame
    std::string getVideoFrameInfo(const AVFrame* pFrame, int indent)
    {
        assert(pFrame);
        const std::string filler(indent, '\t');
        std::stringstream ss;
        ss << filler << "Picture Format: " << (AVPixelFormat) pFrame->format << std::endl;;
        ss << filler << "Size (w x h): " << pFrame->width << "x" << pFrame->height << std::endl;
        ss << filler << "Aspect Ratio: " << pFrame->sample_aspect_ratio.num << "/" <<pFrame->sample_aspect_ratio.den << std::endl;
        ss << filler << "Colorspace: " << pFrame->colorspace << std::endl;
        ss << filler << "Picture Type:" << pFrame->pict_type << std::endl;
        return ss.str();
    };
    

} //<anon>

namespace avtools
{
    // -------------------------------------------------
    // AVFrame wrapper
    // -------------------------------------------------

    Frame::Frame(AVFrame* pFrame, AVMediaType type):
    pFrame_(pFrame ? pFrame : av_frame_alloc()),
    type_(type)
    {
        if (!pFrame_)
        {
            throw MediaError("Frame: Unable to allocate frame data.");
        }
    }

    Frame::Frame(int width, int height, AVPixelFormat format, AVColorSpace cs/*=AVColorSpace::AVCOL_SPC_RGB*/):
    Frame(nullptr, AVMediaType::AVMEDIA_TYPE_VIDEO)
    {
        assert(pFrame_);
        try
        {
            initVideoFrame(pFrame_, width, height, format, cs);
        }
        catch (std::exception& err)
        {
            av_frame_free(&pFrame_);
            std::throw_with_nested(MediaError("Frame: Unable to initialize video frame."));
        }
    }
    
    Frame::Frame(const AVCodecParameters& cPar):
    Frame()
    {
        try
        {
            switch (cPar.codec_type)
            {
                case AVMEDIA_TYPE_VIDEO:
                    initVideoFrame(pFrame_, cPar.width, cPar.height, (AVPixelFormat) cPar.format, cPar.color_space);
                    type_ = AVMediaType::AVMEDIA_TYPE_VIDEO;
                    break;
                default:    //may implement other types in the future
                    throw std::invalid_argument("Frames of type " + std::to_string(cPar.codec_type) + " are not implemented.");
            }
        }
        catch (std::exception& err)
        {
            av_frame_free(&pFrame_);
            std::throw_with_nested(MediaError("Frame: Unable to initialize frame."));
        }
    }

    Frame::Frame(const Frame& frame):
    pFrame_( av_frame_clone(frame.get()) ),
    type_(frame.type())
    {
        if (!pFrame_)
        {
            type_ = AVMediaType::AVMEDIA_TYPE_UNKNOWN;
            throw MediaError("Frame: Unable to clone frame.");
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
        type_ = AVMediaType::AVMEDIA_TYPE_UNKNOWN;
        int ret = av_frame_ref(pFrame_, frame.get());
        if (ret < 0)
        {
            throw MediaError("Frame: Unable to add reference to frame.", ret);
        }
        type_ = frame.type();
        return *this;
    }

    std::string Frame::info(int indent/*=0*/) const
    {
        switch (type_)
        {
            case AVMEDIA_TYPE_VIDEO:
                return getVideoFrameInfo(pFrame_, indent);
                break;
            default:    //may implement other typews in the future
                throw std::invalid_argument("Frames of type " + std::to_string(type_) + " are not implemented.");
        }
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
            pPkt_->data = nullptr;
            pPkt_->size = 0;
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
    
    // -------------------------------------------------
    // AVDictionary wrapper
    // -------------------------------------------------

    Dictionary::~Dictionary()
    {
        if (pDict_)
        {
            av_dict_free(&pDict_);
        }
    }
    
    void Dictionary::add(const std::string& key, const std::string& value)
    {
        assert(pDict_);
        int ret = av_dict_set(&pDict_, key.c_str(), value.c_str(), 0);
        if (ret < 0)
        {
            throw MediaError("Unable to add key " + key + " to dictionary", ret);
        }
    }
    
    void Dictionary::add(const std::string& key, std::int64_t value)
    {
        assert(pDict_);
        int ret = av_dict_set_int(&pDict_, key.c_str(), value, 0);
        if (ret < 0)
        {
            throw MediaError("Unable to add key " + key + " to dictionary", ret);
        }
    }
    
    std::string Dictionary::at(const std::string& key) const
    {
        assert(pDict_);
        AVDictionaryEntry *pEntry = av_dict_get(pDict_, key.c_str(), nullptr, 0);
        if (!pEntry)
        {
            throw std::out_of_range("Key " + key + " not found in dictionary.");
        }
        return pEntry->value;
    }
    
    std::string Dictionary::operator[](const std::string& key) const
    {
        return at(key);
    }
    
    bool Dictionary::has(const std::string &key) const
    {
        assert(pDict_);
        return av_dict_get(pDict_, key.c_str(), nullptr, 0);
    }
    
    /// Clones a dictionary. Any entries in this dictionary are lost.
    /// @param[in] dict source dictionary
    Dictionary& Dictionary::operator=(const Dictionary& dict)
    {
        if (pDict_)
        {
            av_dict_free(&pDict_);
            pDict_ = nullptr;
        }
        int ret = av_dict_copy(&pDict_, dict.get(), 0);
        if (ret < 0)
        {
            throw MediaError("Unable to clone dictionary.");
        }
        return *this;
    }

    // -------------------------------------------------
    // AVCodecContext wrapper
    // -------------------------------------------------
    CodecContext::CodecContext(const AVCodec* pCodec/*=nullptr*/):
    pCC_(avcodec_alloc_context3(pCodec))
    {
        if (!pCC_)
        {
            throw MediaError("CodecContext: Unable to allocate codec context.");
        }
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
    CodecParameters::CodecParameters():
    pParam_(avcodec_parameters_alloc())
    {
        if (!pParam_)
        {
            throw MediaError("CodecParameters: Unable to allocate parameters.");
        }
    }
    
    CodecParameters::CodecParameters(const AVCodecContext* pCC)
    {
        int ret = avcodec_parameters_from_context(pParam_, pCC);
        if (ret < 0)
        {
            throw MediaError("Unable to determine codec parameters from codec context", ret);
        }
    }

    CodecParameters::CodecParameters(const CodecContext& cc):
    CodecParameters(cc.get())
    {
    }

    CodecParameters::CodecParameters(const CodecParameters& cp)
    {
        assert(pParam_);
        int ret = avcodec_parameters_copy(pParam_, cp.get());
        if ( ret < 0 )
        {
            throw MediaError("Unable to clone codec parameters", ret);
        }
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

}
