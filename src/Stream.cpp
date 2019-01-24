//
//  AudioUtilities.hpp
//  AP
//
//  Created by Ender Tekin on 5/4/15.
//
//

#include "Stream.hpp"
#include "log.hpp"
#include <sstream>

namespace
{
    /// returns a string of info re: audio codec
    /// @param[in] codecPar audio codec parameters
    /// Taken from http://www.gamedev.net/topic/624876-how-to-read-an-audio-file-with-ffmpeg-in-c/
    std::string getAudioCodecInfo(const AVCodecParameters& codecPar, int indent=0)
    {
        assert(codecPar.codec_type == AVMEDIA_TYPE_AUDIO);
        const std::string filler(indent, '\t');
        std::stringstream ss;
        // See the following to know what data type (unsigned char, short, float, etc) to use to access the audio data:
        // http://ffmpeg.org/doxygen/trunk/samplefmt_8h.html#af9a51ca15301871723577c730b5865c5
        
        ss << filler << "Audio codec info:" << std::endl;
        const AVSampleFormat fmt = (AVSampleFormat) codecPar.format;
        ss << filler << "\tFormat: " << fmt << std::endl;
        ss << filler << "\t\tPlanar: " << (bool) av_sample_fmt_is_planar(fmt);
        ss << filler << "\t\tBytes per sample: " << av_get_bytes_per_sample(fmt) << std::endl;
        ss << filler << "\tSampling Rate: " << codecPar.sample_rate << std::endl;
        ss << filler << "\tChannel count: " << codecPar.channels << std::endl;
//        char buf[64];
//        av_get_channel_layout_string(buf, 64, codecContext->channels, codecContext->channel_layout);
        ss << filler << "\tChannel layout: " << avtools::av_get_channel_layout_name(codecPar.channel_layout) << std::endl;
        //frame->linesize[0] tells you the size (in bytes) of each plane
        
//        if (codecContext->channels > AV_NUM_DATA_POINTERS && av_sample_fmt_is_planar(codecContext->sample_fmt))
//        {
//            //LOGE("The audio stream (and its frames) have too many channels to fit in frame->data.");
//            //Therefore, to access the audio data, you need to use frame->extended_data to access the audio data. It's planar, so each channel is in a different element. That is:
//            //frame->extended_data[0] has the data for channel 1
//            //frame->extended_data[1] has the data for channel 2, etc.
//        }
        //otherwise you can either use frame->data or frame->extended_data to access the audio data (they should just point to the same data).
        
        //If the frame is planar, each channel is in a different element, i.e.,
        //frame->data[0]/frame->extended_data[0] has the data for channel 1
        //frame->data[1]/frame->extended_data[1] has the data for channel 2, etc.
        //
        //If the frame is packed (not planar), then all the data is in
        //frame->data[0]/frame->extended_data[0] (kind of like how some
        //image formats have RGB pixels packed together, rather than storing
        //the red, green, and blue channels separately in different arrays.
        
        return ss.str();
    }
    
    /// Prints info re: video codec
    /// @param[in] codecPar video codec parameters
    std::string getVideoCodecInfo(const AVCodecParameters& codecPar, int indent=0)
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
    
    /// Prints info re: audio frames
    /// Taken from http://www.gamedev.net/topic/624876-how-to-read-an-audio-file-with-ffmpeg-in-c/
    /// @param[in] pFrame audio frame. This function does not check that this is an audio frame, and may cause problems if the frame does not actually contain audio data.
    /// @param[in] indent number of tabs to indent by
    /// @return a string containing info re: an audio frame
    /// Prints info re: an audio frame
    std::string getAudioFrameInfo(const AVFrame* pFrame, int indent)
    {
        assert(pFrame);
        const std::string filler(indent, '\t');
        std::stringstream ss;
        ss << filler << "Sample count:" << pFrame->nb_samples << std::endl;
        AVSampleFormat format = (AVSampleFormat) pFrame->format;
        ss << filler << "Sample format: " << format << std::endl;
        ss << filler << "Bytes/sample: " << av_get_bytes_per_sample(format) << std::endl;
        ss << filler << "Plane size (in bytes): " << pFrame->linesize[0] << std::endl;
        ss << filler << "#Channels: " << pFrame->channels << std::endl;
        //LOGD("\t#Channel layout: ",  av_get_channel_description(av_frame_get_channel_layout(pFrame)));
//        static const int BUFLEN = 32;
//        char charbuf[BUFLEN];
//        av_get_channel_layout_string(charbuf, BUFLEN, pFrame->channels, pFrame->channel_layout);
        ss << filler << "Channel layout: " << avtools::av_get_channel_layout_name(pFrame->channel_layout) << std::endl;;
        return ss.str();
        
        //        int linesizeNeeded = 0;
        //        int ret = av_samples_get_buffer_size(&linesizeNeeded, pFrame->channels, pFrame->nb_samples, (AVSampleFormat) pFrame->format, 0);
        //        if (ret < 0)
        //        {
        //            LOGE("Error calculating linesize: ", av_err2str(ret));
        //        }
        //        else
        //        {
        //            LOGV("Required buffer size / linesize = ", ret, " / ", linesizeNeeded);
        //        }
        //
        //        LOGV("\tPacket size / duration / position:", av_frame_get_pkt_size(pFrame), ", ",av_frame_get_pkt_duration(pFrame), ", ",av_frame_get_pkt_pos(pFrame));
        
        /*
         //frame->linesize[0] tells you the size (in bytes) of each plane
         
         if (codecContext->channels > AV_NUM_DATA_POINTERS && av_sample_fmt_is_planar(codecContext->sample_fmt))
         {
         LOGE("The audio stream (and its frames) have too many channels to fit in frame->data.");
         throw std::runtime_error("Handling this many channels not yet implemented");
         //Therefore, to access the audio data, you need to use frame->extended_data to access the audio data. It's planar, so each channel is in a different element. That is:
         //frame->extended_data[0] has the data for channel 1
         //frame->extended_data[1] has the data for channel 2, etc.
         }
         else
         {
         LOGV("Either the audio data is not planar, or there is enough room in frame->data to store all the channels");
         //so you can either use frame->data or frame->extended_data to access the audio data (they should just point to the same data).
         }
         //If the frame is planar, each channel is in a different element, i.e.,
         //frame->data[0]/frame->extended_data[0] has the data for channel 1
         //frame->data[1]/frame->extended_data[1] has the data for channel 2, etc.
         //
         //If the frame is packed (not planar), then all the data is in
         //frame->data[0]/frame->extended_data[0] (kind of like how some
         //image formats have RGB pixels packed together, rather than storing
         //the red, green, and blue channels separately in different arrays.
         */
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
        ss << filler << "Colorspace: " << av_frame_get_colorspace(pFrame) << std::endl;
        ss << filler << "Picture Type:" << pFrame->pict_type << std::endl;
        return ss.str();
    };

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
    
    CodecParametersHandle cloneCodecParameters(const AVCodecParameters& pF)
    {
        auto h = allocateCodecParameters();
        if (!h)
        {
            throw MMError("Unable to allocate codec parameters");
        }
        int ret = avcodec_parameters_copy(h.get(), &pF);
        if ( ret < 0 )
        {
            throw MMError("Unable to clone codec parameters", ret);
        }
        return h;
    }


    std::string getCodecInfo(const AVCodecParameters& codecPar, int indent)
    {
        switch (codecPar.codec_type)
        {
            case AVMEDIA_TYPE_VIDEO:
                return getVideoCodecInfo(codecPar, indent);
                break;
            case AVMEDIA_TYPE_AUDIO:
                return getAudioCodecInfo(codecPar, indent);
                break;
            default:
                const auto c = av_get_media_type_string(codecPar.codec_type);
                return std::string('\t', indent) + (c ? "Info re: " + std::string(c) + "codecs not available." : "Unknown codec type.");
        }
    }

    std::string getCodecInfo(const AVCodecContext* pCC, int indent)
    {
        assert(pCC);
        auto codecPar = allocateCodecParameters();
        int ret = avcodec_parameters_from_context(codecPar.get(), pCC);
        if (ret < 0)
        {
            throw MMError("Unable to determine codec parameters from codec context", ret);
        }
        return getCodecInfo(*codecPar, indent);
    }

    
    std::string getStreamInfo(const AVStream* pStr, int indent)
    {
        assert(pStr->codecpar);
        const std::string filler(indent, '\t');
        std::stringstream ss;
        ss << filler << getBriefStreamInfo(pStr) << std::endl;
        ss << filler << "\tDuration:" << calculateStreamDuration(pStr) << " s" << std::endl;
        ss << filler << "\tTime Base: " << pStr->time_base << std::endl;
        ss << getCodecInfo(*pStr->codecpar, indent + 1);
        return ss.str();
    }
    
    std::string getBriefStreamInfo(const AVStream* pStr)
    {
        std::stringstream ss;
        ss << "stream #" << pStr->index << ": " << pStr->codecpar->codec_type << " [" << pStr->codecpar->codec_id << "]";
        return ss.str();
    }
    
    std::string getFrameInfo(const AVFrame* pFrame, const AVStream* pStr, int indent/*=0*/)
    {
        assert(pFrame && pStr);
        const std::string filler(indent, '\t');
        std::stringstream ss;
        const AVMediaType type = pStr->codecpar->codec_type;
        ss << filler << "\tSource: " << getBriefStreamInfo(pStr) << std::endl;
        const std::int64_t pts = av_frame_get_best_effort_timestamp(pFrame);
        ss << filler << "\tpts: " << pts << "\t[" << calculateTime(pts, pStr->time_base) << " s]" << std::endl;
        switch(type)
        {
            case AVMEDIA_TYPE_AUDIO:
                ss << getAudioFrameInfo(pFrame, indent+1);
                break;
            case AVMEDIA_TYPE_VIDEO:
                ss << getVideoFrameInfo(pFrame, indent+1);
                break;
            default:
                ss << filler << "No further info available for this frame type.";
                break;
        }
        return ss.str();
    }
    
    FrameHandle allocateFrame(const AVCodecParameters& cPar)
    {
        switch(cPar.codec_type)
        {
            case AVMEDIA_TYPE_AUDIO:
                assert( cPar.channels == av_get_channel_layout_nb_channels(cPar.channel_layout) );
                return allocateAudioFrame(cPar.frame_size, (AVSampleFormat) cPar.format, cPar.sample_rate, cPar.channel_layout);
            case AVMEDIA_TYPE_VIDEO:
                return allocateVideoFrame(cPar.width, cPar.height, (AVPixelFormat) cPar.format, cPar.color_space);
            default:
                throw std::domain_error("allocateFrame currently does not handle non-audio-visual codecs");
        }
    }

}   //::avtools

