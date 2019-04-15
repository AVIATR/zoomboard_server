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
    ThreadsafeFrame::ThreadsafeFrame(int width, int height, AVPixelFormat format):
    Frame(width, height, format),
    pConvCtx_(nullptr),
    mutex(),
    cv()
    {
    }

    ThreadsafeFrame::~ThreadsafeFrame()
    {
        auto lk = getWriteLock();
        if (pConvCtx_)
        {
            sws_freeContext(pConvCtx_);
        }
    };

    void ThreadsafeFrame::update(const avtools::Frame &frm)
    {
        assert(pFrame_);
        if (frm)
        {
            LOG4CXX_DEBUG(logger, "Updating threadsafe frame with \n" << frm.info(1));
            {
                auto lock = getWriteLock();
                assert(frm->data[0] && pFrame_->data[0]);
                if ( (pFrame_->width != frm->width) || (pFrame_->height != frm->height) || (pFrame_->format != frm->format) )
                {
                    LOG4CXX_DEBUG(logger, "Converting frame...");
                    pConvCtx_ = sws_getCachedContext(pConvCtx_, frm->width, frm->height, (AVPixelFormat) frm->format, pFrame_->width, pFrame_->height, (AVPixelFormat) pFrame_->format, SWS_LANCZOS | SWS_ACCURATE_RND, nullptr, nullptr, nullptr);
                    int ret = sws_scale(pConvCtx_, frm->data, frm->linesize, 0, frm->height, pFrame_->data, pFrame_->linesize);
                    if (ret < 0)
                    {
                        throw avtools::MediaError("Error converting frame to output format.", ret);
                    }
                }
                else
                {
                    av_frame_copy(pFrame_, frm.get());
                }

                av_frame_copy_props(pFrame_, frm.get());
                LOG4CXX_DEBUG(logger, "Updated frame info: \n" << info(1));
            }
        }
        else
        {
            // Put frame in uninitialized state
            av_freep(&pFrame_);
            type = AVMediaType::AVMEDIA_TYPE_UNKNOWN;
            assert( !this->operator bool() );
        }
        cv.notify_all();
    }

}   //::avtools
