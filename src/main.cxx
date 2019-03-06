//
//  main.cpp
//  transcoder
//
//  Created by Ender Tekin on 10/8/14.
//  Copyright (c) 2014 Smith-Kettlewell. All rights reserved.
//

#include <cassert>
#include <string>
#include <algorithm>
#include <vector>
#include <mutex>
#include <thread>
#include "version.h"
#include "MediaReader.hpp"
#include "MediaWriter.hpp"
#include "Media.hpp"
#include "log.hpp"
//#include "opencv2/core/core.hpp"
//#include "opencv2/imgproc/imgproc.hpp"
//#include "opencv2/objdetect/objdetect.hpp"
//#include "opencv2/highgui/highgui.hpp"
#include <opencv2/opencv.hpp>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/avutil.h>
}

using avtools::MediaError;
namespace
{
#ifdef __APPLE__ // MacOS
    static const std::string INPUT_DRIVER = "avfoundation";
#elif defined __gnu_linux__ // GNU/Linux
    static const std::string INPUT_DRIVER = "v4l2";
#elif defined _WIN32 //Windows
#error "Windows inputs not yet implemented."
#else
#error "Unknown operating system"
#endif

    /// @class A structure containing the pertinent ffmpeg options
    /// See https://www.ffmpeg.org/ffmpeg-devices.html for the list of codec & stream options
    struct Options
    {
        std::string url;                    ///< Url to open output stream
        avtools::Dictionary streamOptions;  ///< stream options, such as frame rate, video size, pixel format
        avtools::Dictionary codecOptions;   ///< codeco options
    };
    
    /// Parses a json file to retrieve the configuration to use
    /// @param[in] configFile name of configuration file to read
    /// @param[out] inputOptions a structure containing the input options to use
    /// @param[out] outputOptions a structure containing the output stream options to use
    /// @throw std::runtime_error if there is an issue parsing the configuration file.
    void getOptions(const std::string& configFile, Options& inputOptions, Options& outputOptions);

    /// Wraps a cv::mat around libav frames. Note that the matrix is just wrapped around the
    /// existing data, so data is not cloned. Make sure that the matrix is done being used before reading new
    /// data into the frame.
    /// @param[in] a decoded video frame
    /// @return a cv::mat wrapper around the frame data
    cv::Mat getImage(const avtools::Frame& frame);

    /// Converts a frame size string in WxH format to a size
    /// @param[in] sizeStr string containing size info
    /// @return a cv::Size containing the width and height parsed from sizeStr
    /// @throw std::runtime_error if the frame size info could not be extracted
    cv::Size getDims(const std::string& sizeStr);

    /// Structure that creates the window to display the frames and captures the four corners of the
    /// board or other planar object to rectify
    class DisplayWindow
    {
    private:
        std::vector<cv::Point2f> corners_;
        const std::string name_;
        int draggedPt_;
        std::mutex  mutex_;
        std::unique_ptr<avtools::ImageConversionContext> hConvCtx_;
        avtools::Frame  convFrame_;
        cv::Mat img_;

        /// @override cv::MouseCallback
        static void Callback(int event, int x, int y, int flags, void* userdata);
    public:
        DisplayWindow(const std::string& winName);
        void update(const avtools::Frame& frame);
        void display();
    };

    void threadedRead(Options& opts, DisplayWindow& win)
    {
        avtools::MediaReader reader(opts.url, opts.streamOptions, avtools::MediaReader::InputType::CAPTURE_DEVICE);
        avtools::Frame frame(*reader.getVideoStream()->codecpar);
        LOGD("Opened input stream.");
        while (const AVStream* pS = reader.read(frame))
        {
            LOGD("Input frame info:\n", frame.info(1));
            win.update(frame);
        }
    }


} //::<anon>

// The command we are trying to implement for one output stream is
// sudo avconv -f video4linux2 -r 5 -s hd1080 -i /dev/video0 \
//  -vf "format=yuv420p,framerate=5" -c:v libx264 -profile:v:0 high -level 3.0 -flags +cgop -g 1 \
//  -hls_time 0.1 -hls_allow_cache 0 -an -preset ultrafast /mnt/hls/stream.m3u8
// For further help, see https://libav.org/avconv.html
int main(int argc, const char * argv[])
{
    // Parse command line arguments & load the config file
    if (argc != 2)
    {
        LOG("Incorrect number of arguments, must supply a json configuration file as input.");
        LOG("Usage: ", argv[0], " <config.json>");
        return EXIT_FAILURE;
    }

    const std::string configFile = argv[1];
    Options inOpts, outOpts;
    getOptions(configFile, inOpts, outOpts);
    // -----------
    // Open the media reader
    // -----------
    DisplayWindow win("Camera image");
    std::thread readerThread(threadedRead, std::ref(inOpts), std::ref(win));
    while (cv::waitKey(10) < 0)
    {
        win.display();
    }
//    readerThread.join();

    // -----------
    // Get calibration matrix
    // -----------
    LOGX;
    
    // -----------
    // open transcoder & processor
    // -----------
    LOGX;
    
    // -----------
    // Open the output stream writers - one for lo-res, one for hi-res
    // -----------
    LOGX;
    
    // -----------
    // Start the read/write loop
    // -----------
    LOGX;
    
    // -----------
    // Cleanup
    // -----------
    LOGX;
    LOGD("Exiting successfully...");
    return EXIT_SUCCESS;


}

namespace
{
    /// Reads all the entries in a file node into a dictionary
    /// @param[in] node json file node to read from
    /// @param[out] dict dictionary to save entries into
    void readFileNodeIntoDict(const cv::FileNode& node, avtools::Dictionary& dict)
    {
        for (auto it = node.begin(); it != node.end(); ++it)
        {
            cv::FileNode n = *it;
            if (n.isInt())
            {
                dict.add(n.name(), (int) n);
            }
            else if (n.isString())
            {
                dict.add(n.name(), (std::string) n);
            }
            else
            {
                throw std::runtime_error("Parameters in " + node.name() + " can only be string or integer.");
            }
        }
    }

    /// Reads options from a json file node
    /// @param[in] node json file node to read from
    /// @param[out] opts options to read into
    void readFileNodeIntoOpts(const cv::FileNode& node, Options& opts)
    {
        bool isUrlFound = false, isStreamOptsFound = false, isCodecOptsFound = false;
        for (auto it = node.begin(); it != node.end(); ++it)
        {
            cv::FileNode n = *it;
            if (n.name() == "url")
            {
                opts.url = (std::string) n;
                if (isUrlFound)
                {
                    throw std::runtime_error("Multiple urls found in config file for " + node.name());
                }
                isUrlFound = true;
            }
            else if (n.name() == "stream_options")
            {
                readFileNodeIntoDict(n, opts.streamOptions);
                if (isStreamOptsFound)
                {
                    throw std::runtime_error("Multiple stream option entries in config file for " + node.name());
                }
                isStreamOptsFound = true;
            }
            else if (n.name() == "codec_options")
            {
                readFileNodeIntoDict(n, opts.codecOptions);
                if (isCodecOptsFound)
                {
                    throw std::runtime_error("Multiple codec option entries in config file for " + node.name());
                }
                isCodecOptsFound = true;
            }
            else
            {
                LOGW("Unknown options for ", n.name(), " found in config file for ", node.name());
            }
        }
        if (!isUrlFound)
        {
            throw std::runtime_error("URL not found in config file for " + node.name());
        }
        if (!isStreamOptsFound) //note that this can be skipped for stream defaults
        {
            LOGW("Stream options not found in config file for ", node.name());
        }
        if (!isCodecOptsFound) //note that this can be skipped for codec defaults
        {
            LOGW("Codec options not found in config file for ", node.name());
        }
    }

    void getOptions(const std::string& configFile, Options& inOpts, Options& outOpts)
    {
        cv::FileStorage fs(configFile, cv::FileStorage::READ);
        const auto rootNode = fs.root();
        // Read input options
        bool isInputOptsFound = false, isOutputOptsFound = false;
        for (auto it = rootNode.begin(); it != rootNode.end(); ++it)
        {
            cv::FileNode node = *it;
            if (node.name() == INPUT_DRIVER) //Found input options for this architecture
            {
                LOGD("Found input options for ", INPUT_DRIVER);
                if (isInputOptsFound)
                {
                    throw std::runtime_error("Found multiple options for " + INPUT_DRIVER);
                }
                readFileNodeIntoOpts(node, inOpts);
                isInputOptsFound = true;
            }
            else if (node.name() == "output")  //Found the output options
            {
                LOGD("Found output options.");
                if (isOutputOptsFound)
                {
                    throw std::runtime_error("Found multiple output options");
                }
                readFileNodeIntoOpts(node, outOpts);
                isOutputOptsFound = true;
            }
        }
        if (!isInputOptsFound)
        {
            throw std::runtime_error("Unable to find input options for " + INPUT_DRIVER + " in " + configFile);
        }
        if (!isOutputOptsFound)
        {
            throw std::runtime_error("Unable to find output options in " + configFile);
        }
        fs.release();
    }

    cv::Mat getImage(const avtools::Frame& frame)
    {
        return cv::Mat(frame->height, frame->width, CV_8UC3, frame->data[0], frame->linesize[0]);
    }

    cv::Size getDims(const std::string& sizeStr)
    {
        int sep = 0;
        bool isFound = false;
        for (auto it = sizeStr.begin(); it != sizeStr.end(); ++ it)
        {
            if ( ::isdigit(*it) )
            {
                if (!isFound)
                {
                    ++sep;
                }
            }
            else if (!isFound && ((*it == 'x') || (*it == 'X')) )
            {
                isFound = true;
            }
            else
            {
                throw std::runtime_error("Unable to parse " + sizeStr + " to extract size info.");
            }
        }
        return cv::Size( std::stoi(sizeStr.substr(0, sep)), std::stoi(sizeStr.substr(sep+1)) );
    }


    void readLoop(avtools::MediaReader& rdr, std::mutex& mutex, avtools::Frame& frame)
    {
        while (true)
        {
            {
                LOGD("Locking mutex");
                std::lock_guard<std::mutex> lock(mutex);
                const AVStream* pS = rdr.read(frame);
                LOGD("Video Frame, time: ", avtools::calculateTime(frame->best_effort_timestamp - pS->start_time, pS->time_base));
                LOGD("Input frame info:\n", frame.info(1));
                if (!pS)
                {
                    break;
                }
            }
        }
        LOGD("End of stream reached.");
    };

    DisplayWindow::DisplayWindow(const std::string& winName):
    corners_(),
    name_(winName),
    draggedPt_(-1),
    mutex_(),
    hConvCtx_(nullptr),
    convFrame_(),
    img_()
    {
        cv::namedWindow(name_);
        cv::setMouseCallback(name_, DisplayWindow::Callback, this );
    }

    void DisplayWindow::Callback(int event, int x, int y, int flags, void *userdata)
    {
        DisplayWindow* myWin = static_cast<DisplayWindow*>(userdata);
        auto& corners = myWin->corners_;
        switch(event)
        {
            case cv::MouseEventTypes::EVENT_LBUTTONDOWN:
                //See if we are near any existing markers
                assert(myWin->draggedPt_ < 0);
                for (int i = 0; i < corners.size(); ++i)
                {
                    const cv::Point& pt = corners[i];
                    if (cv::abs(x-pt.x) + cv::abs(y - pt.y) < 10)
                    {
                        myWin->draggedPt_ = i;  //start dragging
                        break;
                    }
                }
                break;
            case cv::MouseEventTypes::EVENT_MOUSEMOVE:
                assert(myWin->draggedPt_ < (int) corners.size());
                if ((flags & cv::MouseEventFlags::EVENT_FLAG_LBUTTON) && (myWin->draggedPt_ >= 0))
                {
                    corners[myWin->draggedPt_].x = x;
                    corners[myWin->draggedPt_].y = y;
                }
                break;
            case cv::MouseEventTypes::EVENT_LBUTTONUP:
                assert(myWin->draggedPt_ < (int) corners.size());
                if (myWin->draggedPt_ >= 0)
                {
                    corners[myWin->draggedPt_].x = x;
                    corners[myWin->draggedPt_].y = y;
                    myWin->draggedPt_ = -1; //end dragging
                }
                else if (corners.size() < 4)
                {
                    corners.emplace_back(x,y);
                }
                break;
            default:
                LOGD("Received mouse event: ", event);
                break;
        }
    }

    void DisplayWindow::display()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (img_.elemSize() == 0)
        {
            return;
        }
        if (corners_.size() == 4)
        {
            const std::vector<cv::Point2f> TGT_CORNERS = {cv::Point2f(0,0), cv::Point2f(convFrame_->height, 0), cv::Point2f(convFrame_->height, convFrame_->width), cv::Point2f(0, convFrame_->height)};
            cv::Mat trf = cv::getPerspectiveTransform(corners_, TGT_CORNERS);
            cv::Mat warpedFrame_(convFrame_->height, convFrame_->width, CV_8UC3);
            cv::warpPerspective(img_, warpedFrame_, trf, img_.size());
            cv::imshow(name_+"_warped", warpedFrame_);
        }

        cv::Mat imgToDisplay = img_.clone();
        for (int i = 0; i < corners_.size(); ++i)
        {
            cv::drawMarker(imgToDisplay, corners_[i], cv::Scalar(0,0,255), cv::MarkerTypes::MARKER_SQUARE, 5);
            cv::putText(imgToDisplay, std::to_string(i+1), corners_[i], cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0,0,255));
        }
        if (corners_.size() == 4)
        {
            for (int i = 0; i < 4; ++i)
            {
                cv::line(imgToDisplay, corners_[i], corners_[(i+1) % 4], cv::Scalar(0,0, 255));
                LOGD("Corner [", i+1, "] is at ", corners_[i]);
            }
        }
        cv::imshow(name_, imgToDisplay);
    }

    void DisplayWindow::update(const avtools::Frame &frame)
    {
        std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
        if (convFrame_->width == 0)
        {
            AVPixelFormat fmt = (AVPixelFormat) frame->format;
            if (!sws_isSupportedInput(fmt))
            {
                throw MediaError("Unsupported input pixel format " + std::to_string(fmt));
            }

            if (fmt != AV_PIX_FMT_BGR24)
            {
                hConvCtx_ = std::make_unique<avtools::ImageConversionContext>(frame->width, frame->height, fmt,
                                                                              frame->width, frame->height, AV_PIX_FMT_BGR24);
            }
            convFrame_ = avtools::Frame(frame->width, frame->height, AV_PIX_FMT_BGR24);
            img_ = getImage(convFrame_);
        }
        if (lock)
        {
            av_frame_copy_props(convFrame_.get(), frame.get());
            LOGD("Output frame info:\n", convFrame_.info(1));
            if (hConvCtx_)
            {
                hConvCtx_->convert(frame, convFrame_);
            }
            else
            {
                int ret = av_frame_copy(convFrame_.get(), frame.get());
                if (ret < 0)
                {
                    throw MediaError("Unable to copy frame from reader.");
                }
            }
        }
    }
}   //::<anon>
