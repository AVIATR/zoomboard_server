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
}   //::avtools

