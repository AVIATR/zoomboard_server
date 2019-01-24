//
//  main.cpp
//  transcoder
//
//  Created by Ender Tekin on 10/8/14.
//  Copyright (c) 2014 Smith-Kettlewell. All rights reserved.
//

#include <cassert>
#include <string>
#include <vector>
#include "version.h"
#include "StreamReader.hpp"
#include "StreamWriter.hpp"
#include "Stream.hpp"
#include "log.hpp"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/avutil.h>
}

using avtools::StreamError;
namespace
{
    /// Creates output codec parameters for the transcoder
    /// @param[in] inParam input codec parameters
    /// @return handle to output codec parameters
    avtools::CodecParametersHandle getOutputAudioCodecParameters(const AVCodecParameters& inParam);
    
    /// @class Converts audio from one format to another
    class AudioTranscoder
    {
    private:
        avtools::CodecParametersHandle      hInCodecPar_;       ///< input codec context
        avtools::CodecParametersHandle      hOutCodecPar_;      ///< output codeec context
        avtools::ResamplingContextHandle    hResamplingCtx_;    ///< Resampling context
        avtools::FrameHandle                hOutFrame_;         ///< output frame
        avtools::TimeType                   timeStamp_;         ///< time stamp
        int                                 minBufferSize_;     ///< minimum buffer size
    public:
        
        /// Ctor
        /// @param[in] inCodecCtx input codec context
        /// @param[in] outCodecCtx output codec context
        AudioTranscoder(const AVCodecParameters& inCodecPar, const AVCodecParameters& outCodecPar);
        
        ///Dtor
        ~AudioTranscoder();
        
        void push(const AVFrame* pFrame);
        
        const AVFrame* pop();
    }; //<anon>::AudioTranscoder
} //::<anon>

int main(int argc, const char * argv[])
{
    if (argc != 2)
    {
        LOG("Incorrect number of arguments, must supply a json configuration file as input.");
        LOG("Usage: ", argv[0], " <config.json>");
        return EXIT_FAILURE;
    }
    try
    {
        const std::string infile = argv[1], outfile = argv[2];
        // Open the media reader
        avtools::MultiMediaReader mmReader(infile);
        
        //We will only process the first audio and video streams
        const AVStream* pAuStr = mmReader.getFirstAudioStream();
        if (!pAuStr)
        {
            throw MMError("Transcoder: Could not find any audio streams that can be processed.");
        }
        const int inAuStr = pAuStr->index;
        LOGD("Transcoder: found input audio stream: ", inAuStr);

        // -----------
        // Transcode
        // -------------
        avtools::MultiMediaWriter mmWriter(outfile);
        if ( !mmWriter.isOpen() )
        {
            throw MMError("Transcoder: Unable to open " + outfile + " for writing");
        }
        // Add output audio stream
        int outAuStr=-1, outVidStr=-1;
        auto hAuOutCodecPar = getOutputAudioCodecParameters(*pAuStr->codecpar);
        assert(hAuOutCodecPar && (hAuOutCodecPar->codec_type == AVMEDIA_TYPE_AUDIO));
        assert(hAuOutCodecPar->codec_id == pAuStr->codecpar->codec_id);
        const AVStream* pStrOut = mmWriter.addStream(*hAuOutCodecPar, {1, hAuOutCodecPar->sample_rate});
        if (!pStrOut)
        {
            throw MMError("Transcoder: Unable to add audio stream to output file.");
        }
        assert(pStrOut->codecpar->codec_type == AVMEDIA_TYPE_AUDIO);
        outAuStr = pStrOut->index;
        //Open audio transcoder
        auto hAudioTranscoder = std::make_unique<AudioTranscoder>(*pAuStr->codecpar, *pStrOut->codecpar);
        if (!hAudioTranscoder)
        {
            throw MMError("Transcoder: Unable to open audio transcoder.");
        }
        assert ( (inAuStr >= 0) && (outAuStr >= 0) );
        LOGD("Transcoder: added output audio stream: ", inAuStr, "->", outAuStr);
        
        // Add output video stream
        const AVStream* pVidStr = mmReader.getFirstVideoStream();
        int inVidStr = -1;
        if (pVidStr)
        {
            inVidStr = pVidStr->index;
            LOGD("Transcoder: found input video stream: ", inVidStr);
            const AVStream* pStrOut = mmWriter.addStream(*pVidStr->codecpar, pVidStr->time_base);
            if (!pStrOut)
            {
                throw MMError("Transcoder: Unable to add video stream to output file.");
            }
            outVidStr = pStrOut->index;
            assert ( (inVidStr >= 0) && (outVidStr >= 0) );
            LOGD("Transcoder: added output audio stream: ", inVidStr, "->", outVidStr);
        }

        // Start reading and writing frames
        AVFrame const * pF = nullptr;
        const AVStream* pS = nullptr;
        while ((pS = mmReader.read(pF)))
        {
            assert(pF);
            const int stream = pS->index;
            if (stream == inAuStr)  //audio frame -> transcode & write
            {
                assert(hAudioTranscoder);
                hAudioTranscoder->push(pF);
                while (const AVFrame* pFTranscoded = hAudioTranscoder->pop())
                {
                    mmWriter.write(pFTranscoded, outAuStr);
                }
            }
            else if (stream == inVidStr) //video frame -> just write
            {
                mmWriter.write(pF, outVidStr);
            }
        }
        // EOF. Cleanup & write buffered data
        // Flush buffered data in transcoder
        hAudioTranscoder->push(nullptr);
        while (const AVFrame* pF = hAudioTranscoder->pop())
        {
            mmWriter.write(pF, outAuStr);
        }
        hAudioTranscoder.reset(nullptr);
        //signal eof to writer
        mmWriter.write(nullptr, -1);
    }
    catch(std::exception& e)
    {
        logging::print_exception(e);
        return EXIT_FAILURE;
    }
    LOGD("Exiting successfully...");
    return EXIT_SUCCESS;
}

namespace
{
    
    // Constants
    static const int OUTPUT_SAMPLING_RATE                           = 8000;                         ///< Output sampling rate
    static const avtools::ChannelLayoutType OUTPUT_CHANNEL_LAYOUT   = AV_CH_LAYOUT_MONO;            ///< Output channel layout - mono
    static const AVSampleFormat OUTPUT_SAMPLE_FORMAT                = AV_SAMPLE_FMT_FLTP;           ///< Output sample format - planar floats
    static const int OUTPUT_BIT_RATE                                = 32000;

    avtools::CodecParametersHandle getOutputAudioCodecParameters(const AVCodecParameters& inParam)
    {
        avtools::CodecParametersHandle hOutParam = avtools::allocateCodecParameters();
        if ( !hOutParam )
        {
            throw MMError("Unable to allocate output audio codec parameters");
        }
        int ret = avcodec_parameters_copy(hOutParam.get(), &inParam);
        if (ret < 0)
        {
            throw MMError("Unable to copy audio codec parameters", ret);
        }
        const AVCodec* pEncoder = avcodec_find_encoder(inParam.codec_id);
        if (!pEncoder)
        {
            throw MMError("Unable to find an encoder for ", inParam.codec_id);
        }
        // Set the output to mono
        if (avtools::checkChannelLayout(pEncoder, OUTPUT_CHANNEL_LAYOUT) < 0)
        {
            throw MMError("Requested channel layout is not supported by encoder.");
        }
        hOutParam->channel_layout = OUTPUT_CHANNEL_LAYOUT;

        //pOutCodecCtx->sample_fmt = av_get_planar_sample_fmt(pInCodecCtx->sample_fmt) ;
        if (avtools::checkSampleRate(pEncoder, OUTPUT_SAMPLING_RATE) < 0)
        {
            throw MMError("Requested sampling rate is not supported by encoder.");
        }
        hOutParam->sample_rate = OUTPUT_SAMPLING_RATE;
        
        if (avtools::checkSampleFormat(pEncoder, OUTPUT_SAMPLE_FORMAT) < 0)
        {
            throw MMError("Requested sample format is not supported by encoder.");
        }
        hOutParam->format = OUTPUT_SAMPLE_FORMAT;
        
        hOutParam->bit_rate = OUTPUT_BIT_RATE;
        return hOutParam;
    }
    
    /// @class error thrown by the audio transcoder
    struct TranscoderError: public std::runtime_error
    {
        inline TranscoderError(const std::string& msg):
        std::runtime_error("AudioTranscoder Error: " + msg)
        {}
        
        inline TranscoderError(const std::string& msg, int err):
        TranscoderError(msg + ". AV_ERR: " + av_err2str(err))
        {}
        
        inline virtual ~TranscoderError() = default;
    };  //Transcoder error
    
    AudioTranscoder::AudioTranscoder(const AVCodecParameters& inParam, const AVCodecParameters& outParam):
    hInCodecPar_( avtools::cloneCodecParameters(inParam) ),
    hOutCodecPar_( avtools::cloneCodecParameters(outParam) ),
    hResamplingCtx_( avtools::allocateResamplingContext(inParam, outParam) ),
    hOutFrame_( avtools::allocateFrame(*hOutCodecPar_) ),
    timeStamp_(0L),
    minBufferSize_(outParam.frame_size + 50)
    {
        if ( !hInCodecPar_ )
        {
            throw TranscoderError("Unable to clone input audio codec parameters");
        }
        if ( !hOutCodecPar_ )
        {
            throw TranscoderError("Unable to initialize output audio codec parameters");
        }
        if ( hOutCodecPar_->channels > AV_NUM_DATA_POINTERS )
        {
            throw TranscoderError("Can currently only handle a maximum of " + std::to_string(AV_NUM_DATA_POINTERS) + " channels");
        }
        if ( !hResamplingCtx_ )
        {
            throw TranscoderError("Unable to allocate audio resampling context.");
        }
        
        assert(hOutFrame_);  //otherwise hOutFrame_ should have thrown
    }
    
    AudioTranscoder::~AudioTranscoder()
    {
        assert(hResamplingCtx_);
        std::int64_t n = 0;
        if ( (n = swr_get_delay(hResamplingCtx_.get(), 1000) ) )
        {
            LOGW("AudioTranscoder: closing, but there are ", n, "ms of output left in the resampling context.");
        }
    }
    
    void AudioTranscoder::push(const AVFrame* pFrame)
    {
        // Push input data to resampling buffer. Don't retrieve any data yet, as a full frame may not be available
        assert(hResamplingCtx_);
        assert(minBufferSize_ > 0); //this is set to zero upon null frame, shouldn't be pushing anything again afterwards
        int ret = swr_convert_frame(hResamplingCtx_.get(), nullptr, pFrame);
        if (ret < 0)
        {
            throw TranscoderError("Error converting audio to output format.", ret);
        }
        if (!pFrame)
        {
            minBufferSize_ = 0; //eof, flush remaining data in consecutive pops without waiting for full frame
        }
    }
    
    const AVFrame* AudioTranscoder::pop()
    {
        assert(hResamplingCtx_);
        int ret = swr_get_out_samples(hResamplingCtx_.get(), 0);
        if (ret < 0)
        {
            throw TranscoderError("Cannot determine number of buffered samples.");
        }
        else if (ret < minBufferSize_)
        {
            return nullptr; //no error just insufficient data for a full frame
        }
        hOutFrame_->nb_samples = hOutCodecPar_->frame_size;
        ret = swr_convert_frame(hResamplingCtx_.get(), hOutFrame_.get(), nullptr);
        if (ret < 0)
        {
            throw TranscoderError("Error converting audio to output format.", ret);
        }
        assert(hOutFrame_->nb_samples <= hOutCodecPar_->frame_size);
        if (0 == hOutFrame_->nb_samples)
        {
            assert(swr_get_out_samples(hResamplingCtx_.get(), 0) == 0);
            return nullptr;  //no data left
        }
        hOutFrame_->pts = timeStamp_;
        timeStamp_ += hOutFrame_->nb_samples;
        return hOutFrame_.get();
    }

}   //::<anon>
