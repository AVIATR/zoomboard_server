//
//  ThreadsafeFrame.cpp
//  zoomboard_server
//
//  Created by Ender Tekin on 4/12/19.
//

#include "ThreadsafeFrame.hpp"
#include "Media.hpp"
#include "log4cxx/logger.h"
extern "C" {
#include <libswscale/swscale.h>
#include <libavutil/avutil.h>
}

namespace
{
    log4cxx::LoggerPtr logger(log4cxx::Logger::getLogger("zoombrd"));
}

namespace avtools
{
    // ---------------------------
    // Threadsafe Frame Definitions
    // ---------------------------
    ThreadsafeFrame::ThreadsafeFrame(int width, int height, AVPixelFormat format, TimeBaseType tb):
    Frame(width, height, format, tb),
    pConvCtx_(nullptr),
    mutex(),
    cv()
    {
    }

    ThreadsafeFrame::~ThreadsafeFrame()
    {
        LOG4CXX_DEBUG(logger, "Releasing threadsafe frame from thread at " << (void*) pFrame_->data[0] );
        auto lk = getWriteLock();
        if (pConvCtx_)
        {
            sws_freeContext(pConvCtx_);
        }
    };

    void ThreadsafeFrame::update(const avtools::Frame &frm)
    {
        assert(pFrame_);
        assert(frm.type == AVMediaType::AVMEDIA_TYPE_VIDEO);
        assert( 0 == av_cmp_q(frm.timebase, timebase) );
        int ret;
        if (frm)
        {
            {
                auto lock = getWriteLock();
                assert(frm->data[0] && pFrame_->data[0]);
                if ( (pFrame_->width != frm->width) || (pFrame_->height != frm->height) || (pFrame_->format != frm->format) )
                {
                    LOG4CXX_DEBUG(logger, "Converting frame...");
                    pConvCtx_ = sws_getCachedContext(pConvCtx_, frm->width, frm->height, (AVPixelFormat) frm->format, pFrame_->width, pFrame_->height, (AVPixelFormat) pFrame_->format, SWS_LANCZOS | SWS_ACCURATE_RND, nullptr, nullptr, nullptr);
                    ret = sws_scale(pConvCtx_, frm->data, frm->linesize, 0, frm->height, pFrame_->data, pFrame_->linesize);
                    if (ret < 0)
                    {
                        throw avtools::MediaError("Error converting frame to output format.", ret);
                    }
                }
                else
                {
                    ret = av_frame_copy(pFrame_, frm.get());
                    if (ret < 0)
                    {
                        throw avtools::MediaError("Error copying frame.", ret);
                    }
                }
                ret = av_frame_copy_props(pFrame_, frm.get());
                if (ret < 0)
                {
                    throw avtools::MediaError("Error copying frame properties.", ret);
                }
            }
        }
        else
        {
            {
                auto lk = getWriteLock();
                // Put frame in uninitialized state
                if (pFrame_)
                {
                    av_freep(&pFrame_);
                }
                type = AVMediaType::AVMEDIA_TYPE_UNKNOWN;
            }
        }
        cv.notify_all();
    }

}   //::avtools
