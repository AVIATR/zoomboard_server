//
//  Media.cpp
//
//  Created by Ender Tekin on 5/4/15.
//
//

#include "Media.hpp"
#include "log4cxx/logger.h"
#include <sstream>
namespace
{
    log4cxx::LoggerPtr logger(log4cxx::Logger::getLogger("zoombrd.Media"));
}

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
            LOG4CXX_WARN(logger, "Duration exceeds 99 hours.");
            ret = std::snprintf(buf, STRBUFLEN, "99+h");
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


    std::string getFrameInfo(const AVFrame* pFrame, const AVStream* pStr, int indent/*=0*/)
    {
        assert(pFrame && pStr);
        const std::string filler(indent, '\t');
        std::stringstream ss;
        AVMediaType type = pStr->codecpar->codec_type;
        ss << filler << "Source: " << *pStr << std::endl;
        ss << getFrameInfo(pFrame, type, indent);
        ss << filler << "time: " << calculateTime(pFrame->best_effort_timestamp, pStr->time_base) << "s [timebase=" \
            << pStr->time_base << "]" << std::endl;
        return ss.str();
    }

    std::string getFrameInfo(const AVFrame* pFrame, AVMediaType type, int indent/*=0*/)
    {
        assert(pFrame);
        const std::string filler(indent, '\t');
        std::stringstream ss;
        ss << filler << "Type: " << type << std::endl;
#ifndef NDEBUG
        ss << filler << "Data allocated at " << static_cast<void*>(pFrame->data[0]) << std::endl;
        ss << filler << "Stride: ";
        for (int i = 0; (i < AV_NUM_DATA_POINTERS) && (pFrame->linesize[i]); ++i)
        {
            ss << pFrame->linesize[i] << " ";
        }
        ss << std::endl;
#endif
        if (type == AVMEDIA_TYPE_VIDEO)
        {
            ss << filler << "Picture Format: " << (AVPixelFormat) pFrame->format << std::endl;;
            ss << filler << "Size (w x h): " << pFrame->width << "x" << pFrame->height << std::endl;
            ss << filler << "Aspect Ratio: " << pFrame->sample_aspect_ratio.num << "/" <<pFrame->sample_aspect_ratio.den << std::endl;
            ss << filler << "Frame Type:" << pFrame->pict_type << std::endl;
        }
        else
        {
            ss << filler << "-No media info is available-" << std::endl;
        }
        ss << filler << "Timestamp:" << pFrame->best_effort_timestamp << std::endl;
        ss << filler << "pts:" << pFrame->pts << std::endl;
        ss << filler << "pkt_dts:" << pFrame->pkt_dts << std::endl;
        return ss.str();
    }

    std::string getStreamInfo(const AVStream* pStr, int indent/*=0*/)
    {
        assert(pStr);
        const std::string filler(indent, '\t');
        std::stringstream ss;
        const AVMediaType type = pStr->codecpar->codec_type;
        ss << filler << "\tType: " << type << std::endl;
        ss << filler << "\tTimebase: " << pStr->time_base << std::endl;
        ss << filler << "\tStart ime: " << calculateTime(pStr->start_time, pStr->time_base) << std::endl;
        ss << filler << "\tAvg. frame rate: " << pStr->avg_frame_rate << std::endl;
        return ss.str();
    }

}   //::avtools

