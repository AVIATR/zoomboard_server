//
//  Stream.hpp
//
//  Also see http://roxlu.com/2014/039/decoding-h264-and-yuv420p-playback
//  Created by Ender Tekin on 10/8/14.
//  Copyright (c) 2014 Smith-Kettlewell. All rights reserved.
//

#ifndef __Stream_hpp__
#define __Stream_hpp__

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

    class StreamError: public std::runtime_error
    {
    public:
        inline StreamError(const std::string& msg):
        std::runtime_error(msg)
        {}
        
        inline StreamError(const std::string& msg, int err):
        StreamError(msg + "\nAV_ERR: " + av_err2str(err))
        {}
        
        inline virtual ~StreamError() = default;
    };

    /// @class Wrapper around AVDictionary
    class Dict
    {
    private:
        AVDictionary* pDict_;                                   ///< actual dictionary
    public:
        inline Dict(): pDict_(nullptr) {}                       ///< Ctor
        inline ~Dict() {if (pDict_) av_dict_free(&pDict_); }    ///< Dtor
        inline AVDictionary* get() {return pDict_;}             ///< @return pointer to the wrapepr dictionary
        inline const AVDictionary* get() const {return pDict_;} ///< @return pointer to the wrapepr dictionary
        /// Adds a key/value pair to the dictionary
        /// @param[in] key new key to add
        /// @param[in] value value of the key
        /// @throw std::runtime_error if there was an error adding the key
        void add(const std::string& key, const std::string& value);
        /// Adds a key/value pair to the dictionary
        /// @param[in] key new key to add
        /// @param[in] value value of the key
        /// @throw std::runtime_error if there was an error adding the key
        void add(const std::string& key, std::int64_t value);
        /// Returns the value of a key in the dictionary.
        /// @param[in] key key to search for
        /// @return value of the key
        /// @throw std::runtime_error if there was an error retrieving the value
        std::string at(const std::string& key) const;
        /// Returns the value of a key in the dictionary.
        /// @param[in] key key to search for
        /// @return value of the key
        /// @throw std::runtime_error if there was an error retrieving the value
        std::string operator[](const std::string& key) const;
        /// Clones a dictionary. Any entries in this dictionary are lost.
        /// @param[in] dict source dictionary
        Dict& operator=(const Dict& dict);
    };  //avtools::Dict
    
    /// Handle typedef to handle structures that require freeing up
    template<class T>
    using Handle = std::unique_ptr<T, std::function<void(T*)>>;
    
    /// Handle to deal with the lifespan of an AVFrame
    typedef Handle<AVFrame> FrameHandle;
    
    /// @return a newly initialized FrameHandle
    inline FrameHandle allocateFrame() noexcept
    {
        return FrameHandle(av_frame_alloc(), [](AVFrame* p) { if (p) av_frame_free(&p);});
    }
    
    /// Allocates and output frame for a given set of codec parameters
    /// @param[info] codecPar codec codec parameters
    /// @return a pointer to a newly alloated frame corresponding to codecPar, nullptr if a frame could not be allocated.
    FrameHandle allocateFrame(const AVCodecParameters& codecPar);
    
    /// Allocates a video frame with a given size, format and sample rate.
    /// Basically combines av_frame_alloc() and initVideoFrame()
    /// @param[in] width how wide the image is in pixels
    /// @param[in] height how high the image is in pixels
    /// @param[in] format pixel format
    /// @param[in] cs color space type
    /// @return pointer to a new frame, nullptr if a frame could not be allocated
    FrameHandle allocateVideoFrame(int width, int height, AVPixelFormat format, AVColorSpace cs=AVColorSpace::AVCOL_SPC_RGB);

    /// Clones a frame. If frame is reference counted, same data is referenced. otherwise, new data buffers are created
    /// @param[in] pF ptr to original frame
    /// @return handle to cloned frame
    inline FrameHandle cloneFrame(const AVFrame* pF) noexcept
    {
        return FrameHandle(av_frame_clone(pF), [](AVFrame* p) { if (p) av_frame_unref(p);});
    }

    /// Handle to deal with the lifespan of an AVFrame
    typedef Handle<AVCodecContext> CodecContextHandle;
    
    /// @return a newly initialized codec context
    /// @param[in] pCodec codec to use to initialize default values
    inline CodecContextHandle allocateCodecContext(const AVCodec* pCodec=nullptr) noexcept
    {
        return CodecContextHandle( avcodec_alloc_context3(pCodec), [](AVCodecContext* p) { if (p) avcodec_free_context(&p);});
    }

    /// Handle to deal with the lifespan of an AVFrame
    typedef Handle<AVCodecParameters> CodecParametersHandle;
    
    /// @return a newly initialized FrameHandle
    inline CodecParametersHandle allocateCodecParameters() noexcept
    {
        return CodecParametersHandle( avcodec_parameters_alloc(), [](AVCodecParameters* p) { if (p) avcodec_parameters_free(&p);});
    }
    /// Format context handle
    typedef Handle<AVFormatContext> FormatContextHandle;
    
    /// Packet handle
    typedef Handle<AVPacket> PacketHandle;
    
    /// allocate a new packet
    /// @return handle to newly allocated packet. nullptr on error
    inline PacketHandle allocatePacket() noexcept
    {
        return PacketHandle(av_packet_alloc(), [](AVPacket* p) {av_packet_free(&p); });
    }
    
    /// Initiaalize a packet
    /// @param[in] h handle to a previous allocated packet
    inline void initPacket(PacketHandle& h) noexcept
    {
        av_init_packet(h.get());
        h->data = nullptr;
        h->size = 0;
    }
    
    /// Unreference a ref-counted packet
    /// @param[in] h handle to a previous initialized packet
    inline void unrefPacket(PacketHandle& h) noexcept
    {
        av_packet_unref(h.get());
        h->stream_index = -1;
    }
    
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

    /// Converts a time stamp to a time
    /// @param[in] timeStamp time stamp
    /// @param[in] pStr stream that the time stamp belongs to
    /// @return time in seconds that timeStamp corresponds to
    [[deprecated]]
    inline double calculateTime(TimeType timeStamp, const AVStream* pStr)
    {
        return calculateTime(timeStamp, pStr->time_base);
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
    

    template<typename T>
    struct Result
    {
        typedef T       DataType;   ///< type of data
        DataType        data;       ///< actual result
        TimeType        timestamp;  ///< timestamp for the result
        TimeType        length;     ///< length of the result. Result then applies to [timestamp, timestamp+length) in units of timebase
        TimeBaseType    timebase;   ///< base rate that timestamp and duration are in
        
        inline double time() const {return calculateTime(timestamp, timebase); }
        inline double duration() const {return calculateTime(length, timebase); }
        
        Result(const Result&) = default;
        Result() = default;
    };
    
    /// Finds the first audio and video streams in a container of streams
    template<template<class ...> class Container>
    void findMediaStreams(const Container<const AVStream*>& streams, const AVStream*& pAuStr, const AVStream*& pVidStr)
    {
        pAuStr = pVidStr = nullptr;
        for (const auto pStr : streams)
        {
            if (!pAuStr && (pStr->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) )
            {
                pAuStr = pStr;
            }
            else if (!pVidStr && (pStr->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) )
            {
                pVidStr = pStr;
            }
            if (pVidStr && pAuStr)
            {
                return;
            }
        }
    }

    /// Clones a codec's parameters
    /// @param[in] cPar original codec context
    /// @return handle to a new copy of the codec context
    CodecParametersHandle cloneCodecParameters(const AVCodecParameters& cPar);
    
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
    
    /// Prints stream info
    /// @param[in] pStr pointer to stream
    /// @param[in] indent number of tabs to indent each line of text
    /// @return a string containing detailed info about the stream
    std::string getStreamInfo(const AVStream* pStr, int indent=0);

    /// Returns a line of text which includes a summary of a stream
    std::string getBriefStreamInfo(const AVStream* pStr);

    /// Return info for a given frame
    /// @param[in] pFrame input frame;
    /// @param[in] pStr stream that the frame came from
    /// @param[in] indent number of tabs to indent each line of text
    std::string getFrameInfo(const AVFrame* pFrame, const AVStream* pStr, int indent=0);

    /// Initializes a frame
    /// @param[in] pFrame frame to be initialized.
    /// @param[in] codecPar codec parameters containing details of the audio codec
    /// @return a libav error code if there is an error, 0 otherwise
//    int initFrame(AVFrame* pFrame, const AVCodecParameters& codecPar);

    /// Dumps info for.a particular stream to stderr
    inline void dumpStreamInfo(AVFormatContext* p, int n, bool isOutput)
    {
        assert(p);
        assert( (n >= 0) && (n < p->nb_streams) );
#ifndef NDEBUG
        const int level = av_log_get_level();
        av_log_set_level(AV_LOG_VERBOSE);
        av_dump_format(p, n, p->url, isOutput ? 1 : 0);
        av_log_set_level(level);
#endif
    }
    
    /// Dumps info for.a particular container to stderr
    inline void dumpContainerInfo(AVFormatContext* p, bool isOutput)
    {
        assert(p);
#ifndef NDEBUG
        const int level = av_log_get_level();
        av_log_set_level(AV_LOG_VERBOSE);
        int output = isOutput ? 1 : 0;
        for (int n = 0; n < p->nb_streams; ++n)
        {
            av_dump_format(p, n, p->url, output);
        }
        av_log_set_level(level);
#endif
    }
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
    return (stream << avtools::getBriefStreamInfo(&str));
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

#endif /* defined(__Stream_hpp__) */
