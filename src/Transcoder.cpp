//
//  StreamReader.cpp
//  StreamReader
//
//  Created by Ender Tekin on 10/8/14.
//  Copyright (c) 2014 Smith-Kettlewell. All rights reserved.
//

#include "Transcoder.hpp"
#include "Media.hpp"
#include "LibAVWrappers.hpp"
#include <stdexcept>
#include "log.hpp"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

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
        ImageConversionContext      convCtx_;       ///< Conversion context
        Frame                       frame_;         ///< Frame holding transcoded frame data
//        AVCodecParameters           inParam_;       ///< Input codec parameters
//        AVCodecParameters           outParam_;      ///< Output codec parameters
    public:
        Implementation(const AVCodecParameters& inParam, const AVCodecParameters& outParam):
        convCtx_(inParam, outParam),
        frame_(outParam)
//        inParam_( inParam ),
//        outParam_( outParam )
        {
            assert ((frame_->width == outParam.width) && (frame_->height == outParam.height));
            assert (frame_->format == outParam.format);
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
            assert(convCtx_ && frame_ && pFIn);
            int ret = sws_scale(convCtx_.get(), pFIn->data, pFIn->linesize, 0, pFIn->height, frame_->data, frame_->linesize);
            if (ret < 0)
            {
                throw MediaError("Error converting frame to output format.", ret);
            }
            return frame_.get();
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
        assert(pImpl_ && pF);
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
