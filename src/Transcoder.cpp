//
//  StreamReader.cpp
//  StreamReader
//
//  Created by Ender Tekin on 10/8/14.
//  Copyright (c) 2014 Smith-Kettlewell. All rights reserved.
//

#include "Transcoder.hpp"
#include "Media.hpp"
#include <stdexcept>
#include "log.hpp"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
}

namespace
{
    typedef avtools::Handle<SwsContext> ConversionContextHandle;
    ConversionContextHandle allocateConversionContext(const AVCodecParameters& inParam, const AVCodecParameters& outParam)
    {
        return ConversionContextHandle
        (
         sws_getContext(inParam.width, inParam.height, (AVPixelFormat) inParam.format,
                        outParam.width, outParam.height, (AVPixelFormat) outParam.format,
                        0, nullptr, nullptr, nullptr),  //flags = 0, srcfilter=dstfilter=nullptr, param=nullptr
         [](SwsContext* p){sws_freeContext(p);}
        );
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
        FrameHandle             hFrameOut_;     ///< Handle to frame holding transcoded frame data
    public:
        Implementation(const AVCodecParameters& inParam, const AVCodecParameters& outParam):
            hInParam_( cloneCodecParameters(inParam) ),
            hOutParam_( cloneCodecParameters(outParam) ),
            hConvCtx_( allocateConversionContext(inParam, outParam) ),
            hFrameOut_( allocateFrame(outParam) )
        {
            if ( !hInParam_ || !hOutParam_)
            {
                throw MediaError("Unable to clone codec parameters");
            }
            if ( !hConvCtx_ )
            {
                throw MediaError("Unable to allocate conversion context");
            }
            if ( !hFrameOut_ )
            {
                throw MediaError("Unable to allocate frame");
            }
            assert ((hFrameOut_->width == outParam.width) && (hFrameOut_->height == outParam.height));
            assert (hFrameOut_->format == outParam.format);
            if (0 == sws_isSupportedInput((AVPixelFormat) inParam.format))
            {
                throw MediaError("Unsupported input pixel format " + std::to_string(inParam.format));
            }
            if (0 == sws_isSupportedOutput((AVPixelFormat) outParam.format))
            {
                throw MediaError("Unsupported output pixel format " + std::to_string(outParam.format));
            }
        }
        
        ~Implementation() = default;
        
        const AVFrame* convert(const AVFrame* pFIn)
        {
            assert(hConvCtx_ && hFrameOut_);
            int ret = sws_scale(hConvCtx_.get(), pFIn->data, pFIn->linesize, 0, pFIn->height, hFrameOut_->data, hFrameOut_->linesize);
            if (ret < 0)
            {
                throw MediaError("Error converting frame to output format.", ret);
            }
            return hFrameOut_.get();
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
        std::throw_with_nested( MediaError("Transcoder: Unable to open transcoder.") );
    }

    Transcoder::~Transcoder() = default;
    
    const AVFrame* Transcoder::convert(const AVFrame* pF)
    {
        assert(pImpl_);
        try
        {
            return pImpl_->convert(pF);
        }
        catch (std::exception& err)
        {
            std::throw_with_nested( MediaError("Transcoder: Error converting frame.") );
        }
    }
}   //::avtools
