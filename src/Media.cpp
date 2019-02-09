//
//  AudioUtilities.hpp
//  AP
//
//  Created by Ender Tekin on 5/4/15.
//
//

#include "Media.hpp"
#include "log.hpp"
#include <sstream>

namespace
{

//
//    /// Prints info re: a video frame
//    /// @param[in] pFrame a video frame. If the frame is not a video frame, the results will be inaccurate
//    /// @param[in] indent number of tabs to indent by
//    /// @return a string containing info re: an audio frame
//    std::string getVideoFrameInfo(const AVFrame* pFrame, int indent)
//    {
//        assert(pFrame);
//        const std::string filler(indent, '\t');
//        std::stringstream ss;
//        ss << filler << "Picture Format: " << (AVPixelFormat) pFrame->format << std::endl;;
//        ss << filler << "Size (w x h): " << pFrame->width << "x" << pFrame->height << std::endl;
//        ss << filler << "Aspect Ratio: " << pFrame->sample_aspect_ratio.num << "/" <<pFrame->sample_aspect_ratio.den << std::endl;
//        ss << filler << "Colorspace: " << pFrame->colorspace << std::endl;
//        ss << filler << "Picture Type:" << pFrame->pict_type << std::endl;
//        return ss.str();
//    };
//
}   //::<anon>

namespace avtools
{
    std::string getTimeString(double duration)
    {
        int minutes = duration / 60;
        int hours = minutes / 60;
        minutes = minutes % 60;
        double seconds = duration - 60* (hours * 60 + minutes);
        static const int STRBUFLEN = 15;
        char buf[STRBUFLEN+1];
        int ret = -1;
        if (hours > 99)
        {
            LOGW("Duration exceeds 99 hours.");
            ret = std::snprintf(buf, STRBUFLEN, "%s99+h%s", logging::TerminalFormatting::BOLD, logging::TerminalFormatting::RESET);
        }
        else if (hours > 0)
        {
            ret = std::snprintf(buf, STRBUFLEN, "%dh%dm%2.3fs", hours, (int) minutes, seconds);
        }
        else if (minutes > 0)
        {
            ret = std::snprintf(buf, STRBUFLEN, "%dm%2.3fs", (int) minutes, seconds);
        }
        else
        {
            ret = std::snprintf(buf, STRBUFLEN, "%2.3fs", seconds);
        }
        if (ret < 0)
        {
            throw std::runtime_error("String encoding error");
        }
        else if (ret >= STRBUFLEN)
        {
            throw std::overflow_error("Unable to encode full time into string representation");
        }
        return std::string(buf);
    }
    
//    CodecParametersHandle cloneCodecParameters(const AVCodecParameters& pF)
//    {
//        auto h = allocateCodecParameters();
//        if (!h)
//        {
//            throw StreamError("Unable to allocate codec parameters");
//        }
//        int ret = avcodec_parameters_copy(h.get(), &pF);
//        if ( ret < 0 )
//        {
//            throw StreamError("Unable to clone codec parameters", ret);
//        }
//        return h;
//    }


    std::string getCodecInfo(const AVCodecParameters& codecPar, int indent)
    {
        if (codecPar.codec_type == AVMEDIA_TYPE_VIDEO)
        {
            assert(codecPar.codec_type == AVMEDIA_TYPE_VIDEO);
            const std::string filler(indent, '\t');
            std::stringstream ss;
            ss << filler << "Video codec info:" << std::endl;
            ss << filler << "\tCodec ID: " << codecPar.codec_id << std::endl;
            ss << filler << "\tFormat: " << (AVPixelFormat) codecPar.format << std::endl;
            ss << filler << "\tSize (wxh): " << codecPar.width << "x" << codecPar.height << std::endl;
            ss << filler << "\tPixel Aspect Ratio: " << codecPar.sample_aspect_ratio;
            return ss.str();
        }
        else
        {
            throw std::invalid_argument("Info re: " + std::to_string(codecPar.codec_type) + " codecs is not available.");
        }
    }

//    std::string getCodecInfo(const AVCodecContext* pCC, int indent)
//    {
//        assert(pCC);
//        auto codecPar = allocateCodecParameters();
//        int ret = avcodec_parameters_from_context(codecPar.get(), pCC);
//        if (ret < 0)
//        {
//            throw StreamError("Unable to determine codec parameters from codec context", ret);
//        }
//        return getCodecInfo(*codecPar, indent);
//    }

    
//    std::string getStreamInfo(const AVStream* pStr, int indent)
//    {
//        assert(pStr->codecpar);
//        const std::string filler(indent, '\t');
//        std::stringstream ss;
//        ss << filler << getBriefStreamInfo(pStr) << std::endl;
//        ss << filler << "\tDuration:" << calculateStreamDuration(pStr) << " s" << std::endl;
//        ss << filler << "\tTime Base: " << pStr->time_base << std::endl;
//        ss << getCodecInfo(*pStr->codecpar, indent + 1);
//        return ss.str();
//    }
    
//    std::string getBriefStreamInfo(const AVStream* pStr)
//    {
//        std::stringstream ss;
//        ss << "stream #" << pStr->index << ": " << pStr->codecpar->codec_type << " [" << pStr->codecpar->codec_id << "]";
//        return ss.str();
//    }
    
    std::string getFrameInfo(const AVFrame* pFrame, const AVStream* pStr, int indent/*=0*/)
    {
        assert(pFrame && pStr);
        const std::string filler(indent, '\t');
        std::stringstream ss;
        const AVMediaType type = pStr->codecpar->codec_type;
        ss << filler << "\tType: " << type << std::endl;
        ss << filler << "\tSource: " << *pStr << std::endl;
        const std::int64_t pts = pFrame->best_effort_timestamp;
        ss << filler << "\tPTS: " << pts << "\t[" << calculateTime(pts, pStr->time_base) << " s]" << std::endl;
        if (type == AVMEDIA_TYPE_VIDEO)
        {
            ss << filler << "\tPicture Format: " << (AVPixelFormat) pFrame->format << std::endl;;
            ss << filler << "\tSize (w x h): " << pFrame->width << "x" << pFrame->height << std::endl;
            ss << filler << "\tAspect Ratio: " << pFrame->sample_aspect_ratio.num << "/" <<pFrame->sample_aspect_ratio.den << std::endl;
            ss << filler << "\tColorspace: " << pFrame->colorspace << std::endl;
            ss << filler << "\tPicture Type:" << pFrame->pict_type << std::endl;
            return ss.str();
        }
        else
        {
            ss << filler << "No further info is available" << std::endl;
        }
        return ss.str();
    }
    
//    FrameHandle allocateVideoFrame(int width, int height, AVPixelFormat format, AVColorSpace cs/*=AVColorSpace::AVCOL_SPC_RGB*/)
//    {
//        //allocate frame structure
//        auto hFrameOut = avtools::allocateFrame();
//        if ( !hFrameOut )
//        {
//            throw StreamError("Unable to allocate video frame");
//        }
//        //set metadata
//        hFrameOut->colorspace = cs;
//        hFrameOut->width = width;
//        hFrameOut->height = height;
//        hFrameOut->format = format;
//        //set buffers
//        //        int ret = av_image_alloc(pFrameOut->data, pFrameOut->linesize, width, height, format, ALIGNMENT);
//        int ret = av_frame_get_buffer(hFrameOut.get(), 0);
//        if (ret < 0)
//        {
//            throw StreamError("Error allocating data buffers for video frame", ret);
//        }
//        LOGD("Allocated frame data at ", static_cast<void*>(hFrameOut->data), " with linesize = ", hFrameOut->linesize[0]);
//        return hFrameOut;
//    }
//
//    FrameHandle allocateFrame(const AVCodecParameters& cPar)
//    {
//        assert(cPar.codec_type == AVMEDIA_TYPE_VIDEO);
//        return allocateVideoFrame(cPar.width, cPar.height, (AVPixelFormat) cPar.format, cPar.color_space);
//    }


}   //::avtools

