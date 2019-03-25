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
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <sys/stat.h>
#include <log4cxx/logger.h>
#include <log4cxx/basicconfigurator.h>
#include <log4cxx/consoleappender.h>
//#include <log4cxx/fileappender.h>
#include <log4cxx/patternlayout.h>
#include <opencv2/opencv.hpp>
//#include "opencv2/core/core.hpp"
//#include "opencv2/imgproc/imgproc.hpp"
//#include "opencv2/highgui/highgui.hpp"
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/avutil.h>
}
#include "version.h"
#include "MediaReader.hpp"
#include "MediaWriter.hpp"
#include "Media.hpp"

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

    // Initialize logger
    log4cxx::LoggerPtr logger(log4cxx::Logger::getLogger("zoombrd"));

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

    /// @class Thread-safe frame wrapper
    /// See https://stackoverflow.com/questions/39516416/using-weak-ptr-to-implement-the-observer-pattern
    /// This frame employes a single writer/multiple reader paradigm
    /// Only one thread can write at a time, using update()
    /// Multiple threads can read at the same time, accessing the underlying frame via get()
    /// Threads subscribe to this frame via subscribe(). When a new frame is available (After an update()),
    /// all subscribers are called with notify() to give them access to the underlying frame.
    /// Also see https://en.cppreference.com/w/cpp/thread/shared_mutex for single writer/multiple reader example
    struct ThreadsafeFrame
    {
        avtools::Frame frame;                   ///Wrapped avtools::Frame
        mutable std::shared_timed_mutex mutex;  ///< Mutex used for single writer/multiple reader access TODO: Update to c++17 and std::shared_mutex
        std::condition_variable_any cv;         ///< Condition variable to let observers know when a new frame has arrived
        ThreadsafeFrame();
        ~ThreadsafeFrame();
        /// Called from the writer threa to update the frame
        /// @param[in] frame new frame that will replace the existing frame
        void update(avtools::Frame& frame);
        ///@return a chainable ptr to the AVFrame
        inline AVFrame* operator->() {return frame.get();}
        ///@return a chainable ptr to the AVFrame
        inline const AVFrame* operator->() const {return frame.get();}
        /// @return true if the underlying ptr is non-null
        explicit inline operator bool() const {return (bool) frame; }
    }; //::<anon>::ThreadsafeFrame


    /// Function that starts a stream reader that writes to a threaded frame
    /// @param[in] tFrame threadsafe frame to write to
    /// @param[in] opts input options
    /// @return a new thread that reads frames from the input stream and updates the threaded frame
    std::thread threadedRead(ThreadsafeFrame& tFrame, Options& opts)
    {
        return std::thread([&tFrame, &opts](){
            log4cxx::MDC::put("threadname", "reader");
            LOG4CXX_DEBUG(logger, "Input options are" << opts.streamOptions.as_string());
            avtools::MediaReader rdr(opts.url, opts.streamOptions, avtools::InputMediaType::CAPTURE_DEVICE);
            avtools::Frame frame(*rdr.getVideoStream()->codecpar);
            while (const AVStream* pS = rdr.read(frame))
            {
                frame->best_effort_timestamp -= pS->start_time;
                frame->pts -= pS->start_time;
                LOG4CXX_DEBUG(logger, "Frame read: \n-" << frame.info(1));
                tFrame.update(frame);
            }
        });
    }

    std::thread threadedWrite(ThreadsafeFrame& frame)
    {
        return std::thread([&frame](){
            log4cxx::MDC::put("threadname", "writer");
            thread_local avtools::TimeType pts = -1;
            while (frame)
            {
                {
                    std::shared_lock<std::shared_timed_mutex> lock(frame.mutex);
                    LOG4CXX_DEBUG(logger, "Received frame with pts: " << frame->best_effort_timestamp);
                    if (frame->best_effort_timestamp > pts)
                    {
                        pts = frame->best_effort_timestamp;
                    }
                    frame.cv.wait(lock, [&frame](){return frame->best_effort_timestamp > pts;});
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));    //to simulate long write process
                }
            }
        });
    }

    /// @class Transformer class that asks the user to choose the corners of the board and undoes the
    /// perspective transform. This can then be used with
    /// cv::warpPerspective to correct the perspective of the video.
    /// also @see https://docs.opencv.org/3.1.0/da/d54/group__imgproc__transform.html
    /// Writer can subscribe to this to be notified when a new perspective-corrected frame is available.
    class Transformer
    {
    private:
        std::vector<cv::Point2f> corners_;                              ///< corners to use for perspective transformation
        const std::string name_;                                        ///< name of window
        int draggedPt_;                                                 ///< currently dragged point. -1 if none
        std::mutex mutex_;                                              ///< mutex used for thread sync
        std::unique_ptr<avtools::ImageConversionContext> hConvCtx_;     ///< conversion context if input is not in BGR24 format
        avtools::Frame  convFrame_;                                     ///< frame to display
        cv::Mat img_;                                                   ///< wrapper around frame to display
        cv::Mat trfMatrix_;                                             ///< current transformation matrix

        /// @override cv::MouseCallback
        static void MouseCallback(int event, int x, int y, int flags, void* userdata);

    public:
        /// Ctor
        /// @param[in] name of window
        Transformer(const std::string& name);
        /// Dtor
        ~Transformer();
        /// Displays the latest acquired frame
        void display();
        /// Launches a window where the user can choose the coordinates of the board
        void getBoardCoords();
    };  //::<anon>::Transformer

} //::<anon>

// The command we are trying to implement for one output stream is
// sudo avconv -f video4linux2 -r 5 -s hd1080 -i /dev/video0 \
//  -vf "format=yuv420p,framerate=5" -c:v libx264 -profile:v:0 high -level 3.0 -flags +cgop -g 1 \
//  -hls_time 0.1 -hls_allow_cache 0 -an -preset ultrafast /mnt/hls/stream.m3u8
// For further help, see https://libav.org/avconv.html
int main(int argc, const char * argv[])
{
    // Set up logger. See https://logging.apache.org/log4cxx/latest_stable/usage.html for more info,
    //see https://logging.apache.org/log4cxx/latest_stable/apidocs/classlog4cxx_1_1_pattern_layout.html for patterns
    log4cxx::MDC::put("threadname", "main");    //add name of thread
    log4cxx::LayoutPtr layoutPtr(new log4cxx::PatternLayout("%d %-5p [%-8X{threadname} %.8t] %c{1} - %m%n")); 
    log4cxx::AppenderPtr consoleAppPtr(new log4cxx::ConsoleAppender(layoutPtr));
    log4cxx::BasicConfigurator::configure(consoleAppPtr);
    //Also add file appender - see https://stackoverflow.com/questions/13967382/how-to-set-log4cxx-properties-without-property-file
//    auto fileAppender = std::make_unique<log4cxx::FileAppender>(log4cxx::LayoutPtr(&layout), L"logfile", false);
//    log4cxx::BasicConfigurator::configure(log4cxx::AppenderPtr(fileAppender.get()));

    // Set log level.
    log4cxx::Logger::getRootLogger()->setLevel(log4cxx::Level::getInfo());
    logger->setLevel(log4cxx::Level::getDebug());
    LOG4CXX_INFO(logger,"Created ConsoleAppender appender");

    const std::string USAGE = "Usage: " + std::string(argv[0]) + " <config_file.json>";
    // Parse command line arguments & load the config file
    if (argc != 2)
    {
        LOG4CXX_FATAL(logger, "Incorrect number of arguments." << USAGE);
        return EXIT_FAILURE;
    }
    else
    {
        //Until we convert to C++17, we need to use stat to check for file. Afterwards, we can use std::filesystem
        struct stat buffer;
        if (stat(argv[1], &buffer) != 0)
        {
            LOG4CXX_FATAL(logger, "Configuration file " << argv[1] << " does not exist." << USAGE);
            return EXIT_FAILURE;
        }
    }

    const std::string configFile = argv[1];
    Options inOpts, outOptsLoRes, outOptsHiRes;
    try
    {
        getOptions(configFile, inOpts, outOptsLoRes, outOptsHiRes);
    }
    catch (std::exception& err)
    {
        std::throw_with_nested("Unable to parse command line options" + USAGE);
    }
    // -----------
    // Open the media reader
    // -----------
//    ThreadedReader reader;
    ThreadsafeFrame inFrame;
    std::thread readerThread = threadedRead(inFrame, inOpts);
    std::thread writerThread1 = threadedWrite(inFrame);
    std::thread writerThread2 = threadedWrite(inFrame);
    readerThread.join();
    writerThread1.join();
    writerThread2.join();

    // -----------
    // Add transformer
    // -----------
//    auto hTransformer = std::make_shared<Transformer>("Please choose the four corners");
//    reader.subscribe(hTransformer);
//    std::thread readerThread = reader.run(inOpts);
//    hTransformer->getBoardCoords();

    // -----------
    // Open the output stream writers - one for lo-res, one for hi-res
    // -----------
//    auto hWriterLR = std::make_shared<ThreadedWriter>(outOptsLoRes);
////    auto hWriterHR = std::make_shared<ThreadedWriter>(outOptsHiRes);
////    hTransformer->subscribe(hWriterLR);
//    inFrame.subscribe(hWriterLR);
//    std::thread lowResWriterThread = hWriterLR->run();
//    std::thread hiResWriterThread = writer.run();

    // -----------
    // Cleanup
    // -----------
    //    readerThread.join();
    //    writerThread.join();
    LOG4CXX_DEBUG(logger, "Exiting successfully...");
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
                LOG4CXX_INFO(logger, "Unknown options for " << n.name() << " found in config file for " << node.name());
            }
        }
        if (!isUrlFound)
        {
            throw std::runtime_error("URL not found in config file for " + node.name());
        }
        if (!isStreamOptsFound) //note that this can be skipped for stream defaults
        {
            LOG4CXX_INFO(logger, "Stream options not found in config file for " << node.name());
        }
        if (!isCodecOptsFound) //note that this can be skipped for codec defaults
        {
            LOG4CXX_INFO(logger, "Codec options not found in config file for " << node.name());
        }
    }

    void getOptions(const std::string& configFile, Options& inOpts, Options& outOptsLR, Options& outOptsHR)
    {
        cv::FileStorage fs(configFile, cv::FileStorage::READ);
        const auto rootNode = fs.root();
        // Read input options
        for (auto it = rootNode.begin(); it != rootNode.end(); ++it)
        {
            LOG4CXX_DEBUG(logger, "Node: " << (*it).name());
        }
        auto it = std::find_if(rootNode.begin(), rootNode.end(),
                               [](const cv::FileNode& node){return !node.name().compare("input");});
        if (it == rootNode.end())
        {
            throw std::runtime_error("Input options not found in " + configFile);
        }
        const auto inputsNode = *it;
        it = std::find_if(inputsNode.begin(), inputsNode.end(),
                          [](const cv::FileNode& node){return !node.name().compare(INPUT_DRIVER);});
        if (it == inputsNode.end())
        {
            throw std::runtime_error("Unable to find input options for " + INPUT_DRIVER + " in " + configFile);
        }
        readFileNodeIntoOpts(*it, inOpts);

        // Read output options
        it = std::find_if(rootNode.begin(), rootNode.end(),
                          [](const cv::FileNode& node){return !node.name().compare("output");});
        if (it == rootNode.end())
        {
            throw std::runtime_error("Output options not found in " + configFile);
        }
        const auto outputsNode = *it;
        it = std::find_if(outputsNode.begin(), outputsNode.end(),
                          [](const cv::FileNode& node){return !node.name().compare("lores");});
        if (it == outputsNode.end())
        {
            throw std::runtime_error("Unable to find lo-res output options for in " + configFile);
        }
        readFileNodeIntoOpts(*it, outOptsLR);
        it = std::find_if(outputsNode.begin(), outputsNode.end(),
                          [](const cv::FileNode& node){return !node.name().compare("hires");});
        if (it == outputsNode.end())
        {
            throw std::runtime_error("Unable to find hi-res output options for in " + configFile);
        }
        readFileNodeIntoOpts(*it, outOptsHR);
        fs.release();
    }

    /// Wraps a cv::mat around libav frames. Note that the matrix is just wrapped around the
    /// existing data, so data is not cloned. Make sure that the matrix is done being used before reading new
    /// data into the frame.
    /// @param[in] frame a decoded video frame
    /// @return a cv::mat wrapper around the frame data
    inline cv::Mat getImage(const avtools::Frame& frame)
    {
        return cv::Mat(frame->height, frame->width, CV_8UC3, frame->data[0], frame->linesize[0]);
    }
//    /// Converts a frame size string in WxH format to a size
//    /// @param[in] sizeStr string containing size info
//    /// @return a cv::Size containing the width and height parsed from sizeStr
//    /// @throw std::runtime_error if the frame size info could not be extracted
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
    // Transformer Definitions
    // ---------------------------

    Transformer::Transformer(const std::string& winName):
    corners_(),
    name_(winName),
    draggedPt_(-1),
    mutex_(),
    hConvCtx_(nullptr),
    convFrame_(),
    img_()
    {
        cv::namedWindow(name_);
        cv::setMouseCallback(name_, Transformer::MouseCallback, this );
    }

    Transformer::~Transformer()
    {
        LOG4CXX_DEBUG(logger, "Removing display window");
    }
    /// @override cv::MouseCallback
    void Transformer::MouseCallback(int event, int x, int y, int flags, void *userdata)
    {
        Transformer* pWin = static_cast<Transformer*>(userdata);
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
                    LOG4CXX_DEBUG(logger, "Received mouse event: " << event);
                    break;
            }
        }
    }

    void Transformer::getBoardCoords()
    {
        static const cv::Scalar FIXED_COLOR = cv::Scalar(0,0,255), DRAGGED_COLOR = cv::Scalar(255, 0, 0);
        while ( cv::waitKey(5) < 0 )
        {
            if (convFrame_->width > 0)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                // Display received frame
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
                // Display perspective-corrected frame
                if (corners_.size() == 4)
                {
                    const std::vector<cv::Point2f> TGT_CORNERS = {cv::Point2f(0,0), cv::Point2f(convFrame_->width, 0), cv::Point2f(convFrame_->width, convFrame_->height), cv::Point2f(0, convFrame_->height)};
                    trfMatrix_ = cv::getPerspectiveTransform(corners_, TGT_CORNERS);
                    cv::Mat warpedFrame_(convFrame_->height, convFrame_->width, CV_8UC3);
                    cv::warpPerspective(img_, warpedFrame_, trfMatrix_, img_.size());
                    cv::imshow(name_+"_warped", warpedFrame_);
                }
            }
        }
        if (corners_.size() == 4)
        {
            LOG4CXX_DEBUG(logger, "Current perspective transform is :" << trfMatrix_);
        }
        else
        {
            throw std::runtime_error("Perspective transform requires 4 points.");
        }
    }

    // ---------------------------
    // Threadsafe Frame Definitions
    // ---------------------------
    ThreadsafeFrame::ThreadsafeFrame():
    frame(),
    mutex(),
    cv()
    {}

    ThreadsafeFrame::~ThreadsafeFrame() = default;

    void ThreadsafeFrame::update(avtools::Frame &_frame)
    {
        std::lock_guard<std::shared_timed_mutex> lock(mutex);
        frame = _frame;
        LOG4CXX_DEBUG(logger, "Updated frame with ts: " << frame->best_effort_timestamp);
        cv.notify_all();
    }

}   //::<anon>
