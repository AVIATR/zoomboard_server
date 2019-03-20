//
//  Stream.hpp
//
//  Also see http://roxlu.com/2014/039/decoding-h264-and-yuv420p-playback
//  Created by Ender Tekin on 10/8/14.
//  Copyright (c) 2014 Smith-Kettlewell. All rights reserved.
//

#ifndef __Media_hpp__
#define __Media_hpp__

#include <functional>
#include <cstdint>
#include <memory>
#include <cassert>
#include <string>
#include <functional>
#include <iostream>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
}

#ifdef av_err2str
#undef av_err2str
#endif
#define av_err2str(errnum) av_make_error_string((char*)__builtin_alloca(AV_ERROR_MAX_STRING_SIZE),AV_ERROR_MAX_STRING_SIZE, errnum)

struct AVPacket;
struct AVFormatContext;
struct AVCodecParameters;
struct AVDictionary;

namespace avtools
{
    typedef std::int64_t    TimeType;           ///< type used to represent timestamps
    typedef AVRational      TimeBaseType;       ///< type used to represent time bases (e.g., actual time is timestamp * timeBase.num / timeBase.den

    class MediaError: public std::runtime_error
    {
    public:
        inline MediaError(const std::string& msg):
        std::runtime_error(msg)
        {}
        
        inline MediaError(const std::string& msg, int err):
        MediaError(msg + "\nAV_ERR: " + av_err2str(err))
        {}
        
        inline virtual ~MediaError() = default;
    };

    /// Converts a time stamp to a time
    /// @param[in] timeStamp time stamp
    /// @param[in] timebase timebase that the time stamp belongs to
    /// @return time in seconds that timeStamp corresponds to
    inline double calculateTime(TimeType timeStamp, TimeBaseType timebase)
    {
        return timeStamp * av_q2d(timebase);
    }
    
    inline TimeType convertTimestamp(TimeType ts, TimeBaseType inTimebase, TimeBaseType outTimebase)
    {
        return av_rescale_q(ts, inTimebase, outTimebase);
    }
    
    /// Calculates a stream duration (in seconds)
    /// @param[in] pStream input stream
    /// @return duration of *pStream in seconds.
    inline double calculateStreamDuration(const AVStream* pStream) noexcept
    {
        return calculateTime(pStream->duration, pStream->time_base);
    }
    
    /// Convert a time (in seconds) to a string
    /// @param[in] time time (in seconds)
    /// @return a string representation of time in the format hh:mm:ss.ss
    std::string getTimeString(double time);
    
    /// Convert a timestamp to a string
    /// @param[in] timestamp timestamp
    /// @param[in] timebase time base that the timestamp is in
    /// @return a string representation of time in the format hh:mm:ss.ss
    inline std::string getTimeString(TimeType timestamp, TimeBaseType timebase)
    {
        return getTimeString(calculateTime(timestamp, timebase));
    }
    

//    /// Clones a codec's parameters
//    /// @param[in] cPar original codec context
//    /// @return handle to a new copy of the codec context
//    CodecParametersHandle cloneCodecParameters(const AVCodecParameters& cPar);
//
    /// Prints codec context info
    /// @param[in] codecPar codec parameters
    /// @param[in] indent number of tabs to indent each line of text
    /// @return a string containing detailed info about the codec context
    std::string getCodecInfo(const AVCodecParameters& codecPar, int indent=0);

    /// Prints codec context info
    /// @param[in] pCC pointer to codec context
    /// @param[in] indent number of tabs to indent each line of text
    /// @return a string containing detailed info about the codec context
    std::string getCodecInfo(const AVCodecContext* pCC, int indent=0);

//    /// Prints stream info
//    /// @param[in] pStr pointer to stream
//    /// @param[in] indent number of tabs to indent each line of text
//    /// @return a string containing detailed info about the stream
//    std::string getBriefStreamInfo(const AVStream* pStr);

//    /// Returns a line of text which includes a summary of a stream
//    std::string getBriefStreamInfo(const AVStream* pStr);

    /// Return info for a given frame
    /// @param[in] pFrame input frame;
    /// @param[in] pStr stream that the frame came from
    /// @param[in] indent number of tabs to indent each line of text
    std::string getFrameInfo(const AVFrame* pFrame, const AVStream* pStr, int indent=0);
} //::avtools

// These functions are used for printing info re: various media components
inline std::ostream& operator<<(std::ostream& stream, AVMediaType mediaType)
{
    const char* p = av_get_media_type_string(mediaType);
    return (p ? (stream << p) : (stream << "Unknown media type") );
}

inline std::ostream& operator<<(std::ostream& stream, AVCodecID codecId)
{
    const char* p = avcodec_get_name(codecId);
    return (p ? (stream << p) : (stream << "Unknown codec") );
}

inline std::ostream& operator<<(std::ostream& stream, AVPixelFormat fmt)
{
    const char* p = av_get_pix_fmt_name(fmt);
    return (p ? (stream << p) : (stream << "Unknown pixel format") );
}

inline std::ostream& operator<<(std::ostream& stream, AVSampleFormat fmt)
{
    const char* p = av_get_sample_fmt_name(fmt);
    return (p ? (stream << p) : (stream << "Unknown sample format") );
}

inline std::ostream& operator<<(std::ostream& stream, AVColorSpace cs)
{
    const char* p = av_get_colorspace_name(cs);
    return (p ? (stream << p) : (stream << "Unknown color space") );
}

inline std::ostream& operator<<(std::ostream& stream, AVPictureType type)
{
    return ( stream << av_get_picture_type_char(type) );
}

inline std::ostream& operator<<(std::ostream& stream, AVRational ratio)
{
    return ( stream << ratio.num << "/" << ratio.den);
}

inline std::ostream& operator<<(std::ostream& stream, const AVStream& str)
{
    return (stream << "stream #" << str.index << ": " << str.codecpar->codec_type << " [" << str.codecpar->codec_id << "]");
}

inline std::ostream& operator<<(std::ostream& stream, const AVCodecParameters& param)
{
    return (stream << avtools::getCodecInfo(param));
}

namespace std
{
    inline std::string to_string(AVMediaType mediaType)
    {
        return std::string(av_get_media_type_string(mediaType));
    }
    
    inline std::string to_string(AVCodecID codecId)
    {
        return std::string(avcodec_get_name(codecId));
    }
    
    inline std::string to_string(AVPixelFormat fmt)
    {
        return std::string(av_get_pix_fmt_name(fmt));
    }
    
    inline std::string to_string(AVSampleFormat fmt)
    {
        return std::string(av_get_sample_fmt_name(fmt));
    }
    
    inline std::string to_string(AVColorSpace cs)
    {
        return std::string(av_get_colorspace_name(cs));
    }
    
    inline std::string to_string(AVPictureType type)
    {
        return std::string(1, av_get_picture_type_char(type));
    }
    
    inline std::string to_string(AVRational ratio)
    {
        return std::to_string(ratio.num) + "/" + std::to_string(ratio.den);
    }
    
}   //::std

#endif /* defined(__Media_hpp__) */
