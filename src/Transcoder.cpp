//
//  StreamReader.cpp
//  StreamReader
//
//  Created by Ender Tekin on 10/8/14.
//  Copyright (c) 2014 Smith-Kettlewell. All rights reserved.
//

#include "Transcoder.hpp"
#include "Stream.hpp"
#include <stdexcept>
#include "log.hpp"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>
#include <libavutil/frame.h>
}

namespace
{
    typedef avtools::Handle<SwsConversionContext> ConversionContextHandle;
    ConversionContextHandle allocateConversionContext(const AVCodecParameters& inParam, const AVCodecParameters& outParam)
    {
        LOGX;
    }
}   //::<anon>

namespace avtools
{
    //==========================================
    //
    // Transcoder Implementation
    //
    //==========================================
    
    /// @class Transcoder Implementation
    class Transcoder::Implementation
    {
    private:
        CodecParametersHandle   hInParam_;      ///< Input codec parameters
        CodecParametersHandle   hOutParam_;     ///< Output codec parameters
        ConversionContextHandle hConvCtx_;      ///< Conversion context handle
        FrameHandle             hFrame_;        ///< Handle to frame holding transcoded frame data
    public:
        Implementation(const AVCodecParameters& inParam, const AVCodecParameters& outParam):
            hInParam_( cloneCodecParameters(inParam) ),
            hOutParam_( cloneCodecParameters(outParam) ),
            hConvCtx_( allocateConversionContext(inParam, outParam) ),
            hFrame_( allocateFrame(outParam) )
        {
            if ( !hInParam_ || !hOutParam_)
            {
                throw StreamError("Unable to clone codec parameters");
            }
            if ( !hConvCtx_ )
            {
                throw StreamError("Unable to allocate conversion context");
            }
            if ( !hFrame_ )
            {
                throw StreamError("Unable to allocate frame");
            }
        }
        
        ~Implementation()
        {
            //Close stuff
            LOGX;
        }
        
        void push(const AVFrame* pFrame)
        {
            // Push input data to resampling buffer. Don't retrieve any data yet, as a full frame may not be available
            assert(hConvCtx_);
            int ret = swr_convert_frame(hResamplingCtx_.get(), nullptr, pFrame);
            if (ret < 0)
            {
                throw StreamError("Error converting frame to output format.", ret);
            }
        }
        
        const AVFrame* pop()
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

    };  //::avtools::Transcoder::Implementation
    
    //==========================================
    //
    // Transcoder Interface Definitions
    //
    //==========================================

    Transcoder::Transcoder(const AVCodecParameters& inParam, const AVCodecParameters& outParam) try:
    pImpl_(new Implementation(inParam, outParam))
    {
        assert(pImpl_);
    }
    catch (std::exception& err)
    {
        std::throw_with_nested( StreamError("Transcoder: Unable to open transcoder.") );
    }

    Transcoder::~Transcoder() = default;
    
    void Transcoder::push(const AVFrame* pFrame)
    {
        assert(pImpl_);
        try
        {
            pImpl_->push(pFrame);
        }
        catch(std::exception& err)
        {
            std::throw_with_nested( StreamError("Transcoder: Error pushing frame to transcoder."));
        }
    }
    
    const AVFrame* pop()
    {
        assert(pImpl_);
        try
        {
            return pImpl_->pop();
        }
        catch(std::exception& err)
        {
            std::throw_with_nested( StreamError("Transcoder: Error receiving frames from transcoder."));
        }
    }
}   //::avtools
