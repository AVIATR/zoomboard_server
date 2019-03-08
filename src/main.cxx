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
    /// @param[out] outputOptionsLowRes a structure containing the output stream options to use for the low-res stream
    /// @param[out] outputOptionsHiRes a structure containing the output stream options to use for the hi-res stream
    /// @throw std::runtime_error if there is an issue parsing the configuration file.
    void getOptions(const std::string& configFile,
                    Options& inputOptions,
                    Options& outputOptionsLowRes,
                    Options& outputOptionsHiRes
                    );

    /// Wraps a cv::mat around libav frames. Note that the matrix is just wrapped around the
    /// existing data, so data is not cloned. Make sure that the matrix is done being used before reading new
    /// data into the frame.
    /// @param[in] a decoded video frame
    /// @return a cv::mat wrapper around the frame data
    cv::Mat getImage(const avtools::Frame& frame);

//    /// Converts a frame size string in WxH format to a size
//    /// @param[in] sizeStr string containing size info
//    /// @return a cv::Size containing the width and height parsed from sizeStr
//    /// @throw std::runtime_error if the frame size info could not be extracted
//    cv::Size getDims(const std::string& sizeStr);

    /// @class virtual observer class
    struct FrameObserver: public std::enable_shared_from_this<FrameObserver>
    {
        /// @param[in] new frame to provide when one is available
        virtual void notify(const avtools::Frame&) = 0;
        virtual ~FrameObserver() = default;
    };

    /// @class Threaded reader that uses a threaded observer pattern.
    /// See https://stackoverflow.com/questions/39516416/using-weak-ptr-to-implement-the-observer-pattern
    class ThreadedReader
    {
    private:
        std::mutex mutex_;  //mutex used to ensure that observers don't get removed mid-notification
        std::vector<std::weak_ptr<FrameObserver>> observers_;
        /// Thread function to use.
        void read(Options& opts);
    public:
        /// Ctor
        /// @param[in] opts options for the input stream
        ThreadedReader();
        /// Dtor
        ~ThreadedReader() = default;
        /// Subscribers an observer to the threaded reader. The observer gets notified about the new
        /// frame when one is available
        /// @param[in] obs new observer to subscribe
        void subscribe(std::shared_ptr<FrameObserver> obs);
        /// Removes a previously subscribed observer
        /// @param[in] obs observer to remove from subcsribers. If nullptr, all defunt observers are unsusbcribed
        void unsubscribe(std::shared_ptr<FrameObserver> obs=nullptr);

        /// Launches a new reader thread
        /// @param[in] opts stream options
        /// @return the new thread
        std::thread run(Options& opts);
    };

    /// Launches a window that allows the user to select the four corners of a plane
    /// This is then unwarped to get the required perspective
    /// @param[in] reader a threaded reader to subscribe to and receive read images from
    /// @return a perspective transformation matrix. This can then be used with
    /// cv::warpPerspective to correct the perspective of the video.
    /// also @see https://docs.opencv.org/3.1.0/da/d54/group__imgproc__transform.html
    const cv::Mat getPerspectiveTransform(ThreadedReader& reader);

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
    Options inOpts, outOptsLoRes, outOptsHiRes;
    getOptions(configFile, inOpts, outOptsLoRes, outOptsHiRes);
    // -----------
    // Open the media reader
    // -----------
    ThreadedReader reader;
    std::thread readerThread = reader.run(inOpts);

    // -----------
    // Get calibration matrix
    // -----------
    const cv::Mat trfMatrix = getPerspectiveTransform(reader);
    LOGD("Obtained perspective transform is: ", trfMatrix);
    // -----------
    // open transcoder & processor
    // -----------

    LOGX;
    
    // -----------
    // Open the output stream writers - one for lo-res, one for hi-res
    // -----------
//    std::thread lowResWriterThread = writer.run(outOptsLoRes);
//    std::thread hiResWriterThread = writer.run(outOptsHiRes);
    LOGX;
    
    // -----------
    // Start the read/write loop
    // -----------
    LOGX;
    
    // -----------
    // Cleanup
    // -----------
    //    readerThread.join();
    //    writerThread.join();
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

    void getOptions(const std::string& configFile, Options& inOpts, Options& outOptsLR, Options& outOptsHR)
    {
        cv::FileStorage fs(configFile, cv::FileStorage::READ);
        const auto rootNode = fs.root();
        // Read input options
        auto it = std::find_if(rootNode.begin(), rootNode.end(),
                               [](const cv::FileNode& node){return (node.name() == "input");});
        if (it == rootNode.end())
        {
            throw std::runtime_error("Input options not found in " + configFile);
        }
        const auto inputsNode = *it;
        it = std::find_if(inputsNode.begin(), inputsNode.end(),
                          [](const cv::FileNode& node){return (node.name() == INPUT_DRIVER);});
        if (it == inputsNode.end())
        {
            throw std::runtime_error("Unable to find input options for " + INPUT_DRIVER + " in " + configFile);
        }
        readFileNodeIntoOpts(*it, inOpts);

        // Read output options
        it = std::find_if(rootNode.begin(), rootNode.end(),
                          [](const cv::FileNode& node){return (node.name() == "output");});
        if (it == rootNode.end())
        {
            throw std::runtime_error("Output options not found in " + configFile);
        }
        const auto outputsNode = *it;
        it = std::find_if(outputsNode.begin(), outputsNode.end(),
                          [](const cv::FileNode& node){return (node.name() == "lores");});
        if (it == outputsNode.end())
        {
            throw std::runtime_error("Unable to find lo-res output options for in " + configFile);
        }
        readFileNodeIntoOpts(*it, outOptsLR);
        it = std::find_if(outputsNode.begin(), outputsNode.end(),
                          [](const cv::FileNode& node){return (node.name() == "hires");});
        if (it == outputsNode.end())
        {
            throw std::runtime_error("Unable to find hi-res output options for in " + configFile);
        }
        readFileNodeIntoOpts(*it, outOptsHR);
        fs.release();
    }

    inline cv::Mat getImage(const avtools::Frame& frame)
    {
        return cv::Mat(frame->height, frame->width, CV_8UC3, frame->data[0], frame->linesize[0]);
    }

//    cv::Size getDims(const std::string& sizeStr)
//    {
//        int sep = 0;
//        bool isFound = false;
//        for (auto it = sizeStr.begin(); it != sizeStr.end(); ++ it)
//        {
//            if ( ::isdigit(*it) )
//            {
//                if (!isFound)
//                {
//                    ++sep;
//                }
//            }
//            else if (!isFound && ((*it == 'x') || (*it == 'X')) )
//            {
//                isFound = true;
//            }
//            else
//            {
//                throw std::runtime_error("Unable to parse " + sizeStr + " to extract size info.");
//            }
//        }
//        return cv::Size( std::stoi(sizeStr.substr(0, sep)), std::stoi(sizeStr.substr(sep+1)) );
//    }
//


    // ---------------------------
    // Threaded Reader Definitions
    // ---------------------------
    void ThreadedReader::read(Options& opts)
    {
        avtools::MediaReader reader(opts.url, opts.streamOptions, avtools::MediaReader::InputType::CAPTURE_DEVICE);
        avtools::Frame frame(*reader.getVideoStream()->codecpar);
        LOGD("Opened input stream.");
        while (const AVStream* pS = reader.read(frame))
        {
            LOGD("Input frame info:\n", frame.info(1));
            {
                std::lock_guard<std::mutex> lock(this->mutex_);
                LOGD("There are ", this->observers_.size(), " observers.");
                for (auto& o: this->observers_)
                {
                    if (auto p = o.lock())
                    {
                        p->notify(frame);
                    }
                }
            }
            //remove invalid observers
            this->unsubscribe();
        }
    }

    ThreadedReader::ThreadedReader() = default;

    std::thread ThreadedReader::run(Options& opts)
    {
        return std::thread(&ThreadedReader::read, this, std::ref(opts));
    }

    void ThreadedReader::subscribe(std::shared_ptr<FrameObserver> obs)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        observers_.emplace_back(obs);
    }

    void ThreadedReader::unsubscribe(std::shared_ptr<FrameObserver> obs/*=nullptr*/)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        observers_.erase(
                         std::remove_if(
                                        observers_.begin(),
                                        observers_.end(),
                                        [&obs](const std::weak_ptr<FrameObserver>& o)
                                        {
                                            return o.expired() || o.lock() == obs;
                                        }),
                         observers_.end()
                         );
    }

    // ---------------------------
    // Display Window Definitions
    // ---------------------------

    /// Structure that creates the window to display the frames and captures the four corners of the
    /// board or other planar object to rectify
    class DisplayWindow: public FrameObserver
    {
    private:
        std::vector<cv::Point2f> corners_;                              ///< corners to use for perspective transformation
        const std::string name_;                                        ///< name of window
        int draggedPt_;                                                 ///< currently dragged point. -1 if none
        std::mutex  mutex_;                                             ///< mutex for updating image
        std::unique_ptr<avtools::ImageConversionContext> hConvCtx_;     ///< conversion context if input is not in BGR24 format
        avtools::Frame  convFrame_;                                     ///< frame to display
        cv::Mat img_;                                                   ///< wrapper around frame to display
        cv::Mat trfMatrix_;                                             ///< current transformation matrix

        /// @override cv::MouseCallback
        static void MouseCallback(int event, int x, int y, int flags, void* userdata);
    public:
        /// Ctor
        /// @param[in] winName name of the window
        DisplayWindow(const std::string& winName);

        /// Dtor
        ~DisplayWindow();

        /// @param[in] new frame data
        void notify(const avtools::Frame& frame) override;
        /// Displays the latest acquired frame
        void display();
        /// Returns the current perspective transformation matrix
        const cv::Mat& getPerspectiveTransformationMatrix() const;
    };  //<anon>::DisplayWindow

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
        cv::setMouseCallback(name_, DisplayWindow::MouseCallback, this );
    }

    DisplayWindow::~DisplayWindow()
    {
        LOGD("Removing display window");
    }
    /// @override cv::MouseCallback
    void DisplayWindow::MouseCallback(int event, int x, int y, int flags, void *userdata)
    {
        DisplayWindow* pWin = static_cast<DisplayWindow*>(userdata);
        std::unique_lock<std::mutex> lock(pWin->mutex_, std::try_to_lock);
        if (lock)
        {
            auto& corners = pWin->corners_;
            switch(event)
            {
                case cv::MouseEventTypes::EVENT_LBUTTONDOWN:
                    //See if we are near any existing markers
                    assert(pWin->draggedPt_ < 0);
                    for (int i = 0; i < corners.size(); ++i)
                    {
                        const cv::Point& pt = corners[i];
                        if (cv::abs(x-pt.x) + cv::abs(y - pt.y) < 10)
                        {
                            pWin->draggedPt_ = i;  //start dragging
                            break;
                        }
                    }
                    break;
                case cv::MouseEventTypes::EVENT_MOUSEMOVE:
                    assert(pWin->draggedPt_ < (int) corners.size());
                    if ((flags & cv::MouseEventFlags::EVENT_FLAG_LBUTTON) && (pWin->draggedPt_ >= 0))
                    {
                        corners[pWin->draggedPt_].x = x;
                        corners[pWin->draggedPt_].y = y;
                    }
                    break;
                case cv::MouseEventTypes::EVENT_LBUTTONUP:
                    assert(pWin->draggedPt_ < (int) corners.size());
                    if (pWin->draggedPt_ >= 0)
                    {
                        corners[pWin->draggedPt_].x = x;
                        corners[pWin->draggedPt_].y = y;
                        pWin->draggedPt_ = -1; //end dragging
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
    }

    void DisplayWindow::display()
    {
        static const cv::Scalar FIXED_COLOR = cv::Scalar(0,0,255), DRAGGED_COLOR = cv::Scalar(255, 0, 0);
        std::lock_guard<std::mutex> lock(mutex_);
        if (img_.elemSize() == 0)
        {
            return;
        }
        if (corners_.size() == 4)
        {
            const std::vector<cv::Point2f> TGT_CORNERS = {cv::Point2f(0,0), cv::Point2f(convFrame_->width, 0), cv::Point2f(convFrame_->width, convFrame_->height), cv::Point2f(0, convFrame_->height)};
            trfMatrix_ = cv::getPerspectiveTransform(corners_, TGT_CORNERS);
            cv::Mat warpedFrame_(convFrame_->height, convFrame_->width, CV_8UC3);
            cv::warpPerspective(img_, warpedFrame_, trfMatrix_, img_.size());
            cv::imshow(name_+"_warped", warpedFrame_);
        }

        cv::Mat imgToDisplay = img_.clone();
        for (int i = 0; i < corners_.size(); ++i)
        {
            auto color = (draggedPt_ == i ? DRAGGED_COLOR : FIXED_COLOR);
            cv::drawMarker(imgToDisplay, corners_[i], color, cv::MarkerTypes::MARKER_SQUARE, 5);
            cv::putText(imgToDisplay, std::to_string(i+1), corners_[i], cv::FONT_HERSHEY_SIMPLEX, 0.5, color);
        }
        if (corners_.size() == 4)
        {
            for (int i = 0; i < 4; ++i)
            {
                cv::line(imgToDisplay, corners_[i], corners_[(i+1) % 4], FIXED_COLOR);
            }
        }
        cv::imshow(name_, imgToDisplay);
    }

    void DisplayWindow::notify(const avtools::Frame &frame)
    {
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
        std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
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
                assert(frame->format == AV_PIX_FMT_BGR24);
                int ret = av_frame_copy(convFrame_.get(), frame.get());
                if (ret < 0)
                {
                    throw MediaError("Unable to copy frame from reader.");
                }
            }
        }
    }

    const cv::Mat& DisplayWindow::getPerspectiveTransformationMatrix() const
    {
        return trfMatrix_;
    }

    const cv::Mat getPerspectiveTransform(ThreadedReader& reader)
    {
        auto hWin = std::make_shared<DisplayWindow>("Please choose the four corners of the board");
        reader.subscribe(hWin);

        while (cv::waitKey(5) < 0)
        {
            hWin->display();
        }
        reader.unsubscribe(hWin);
        return hWin->getPerspectiveTransformationMatrix();
    }
}   //::<anon>
