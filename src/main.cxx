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

    static const AVPixelFormat PIX_FMT = AVPixelFormat::AV_PIX_FMT_BGR24;

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
    struct ThreadsafeFrame: public avtools::Frame
    {
        typedef std::shared_lock<std::shared_timed_mutex> read_lock_t;      ///< Read lock type, allows for multi-threaded read
        typedef std::unique_lock<std::shared_timed_mutex> write_lock_t;     ///< Write lock type, only one thread can write

        mutable std::shared_timed_mutex mutex;                              ///< Mutex used for single writer/multiple reader access TODO: Update to c++17 and std::shared_mutex
        mutable std::condition_variable_any cv;                             ///< Condition variable to let observers know when a new frame has arrived
        /// Ctor
        /// @param[in] width width of the frame
        /// @param[in] height of the frame
        /// @param[in] format frame format
        ThreadsafeFrame(int width, int height, AVPixelFormat format);
        /// Default ctor
        ThreadsafeFrame();
        /// Dtor
        virtual ~ThreadsafeFrame();
        /// Called from the writer threa to update the frame
        /// @param[in] frame new frame that will replace the existing frame
        void update(const avtools::Frame& frame);

        /// @return a read lock, blocks until it is acquired
        inline read_lock_t getReadLock() const { return read_lock_t(mutex); }
        /// @return an attempted read lock. The receiver must test to see if the returned lock is acquired
        inline read_lock_t tryReadLock() const { return read_lock_t(mutex, std::try_to_lock); }
        /// @return a write lock, blocks until it is acquired
        inline write_lock_t getWriteLock() { return write_lock_t(mutex); }
        /// @return an attempted write lock. The receiver must test to see if the returned lock is acquired
        inline write_lock_t tryWriteLock() { return write_lock_t(mutex, std::try_to_lock); }
    }; //::<anon>::ThreadsafeFrame


    /// Function that starts a stream reader that reads from a stream int to a threaded frame
    /// @param[in,out] tFrame threadsafe frame to write to
    /// @param[in] rdr an opened media reader
    /// @return a new thread that reads frames from the input stream and updates the threaded frame
    std::thread threadedRead(ThreadsafeFrame& tFrame, avtools::MediaReader& rdr);

    /// Function that starts a stream writer that writes to a stream from a threaded frame
    /// @param[in] tFrame threadsafe frame to read from
    /// @param[in] opts output options
    /// @return a new thread that reads frames from the input frame and writes to an output file
    std::thread threadedWrite(const ThreadsafeFrame& frame, Options& opts);

    /// Asks the user to choose the corners of the board and undoes the
    /// perspective transform. This can then be used with
    /// cv::warpPerspective to correct the perspective of the video.
    /// also @see https://docs.opencv.org/3.1.0/da/d54/group__imgproc__transform.html
    /// @param[in] frame input frames that will be populated by the reader thread
    /// @return a perspective transformation matrix
    cv::Mat getPerspectiveTransformationMatrix(const ThreadsafeFrame& frame);

    /// Launches a thread that creates a warped matrix of the input frame according to the given transform matrix
    /// @param[in] inFrame input frame
    /// @param[in, out] warpedFrame transformed output frame
    /// @param[in] trfMatrix transform matrix
    /// @return a new thread that runs in the background, updates the warpedFrame when a new inFrame is available.
    std::thread threadedWarp(const ThreadsafeFrame& inFrame, ThreadsafeFrame& warpedFrame, const cv::Mat& trfMatrix);

    /// Wraps a cv::mat around libav frames. Note that the matrix is just wrapped around the
    /// existing data, so data is not cloned. Make sure that the matrix is done being used before reading new
    /// data into the frame.
    /// @param[in] frame a decoded video frame
    /// @return a cv::mat wrapper around the frame data
    inline cv::Mat getImage(const avtools::Frame& frame)
    {
        return cv::Mat(frame->height, frame->width, CV_8UC3, frame->data[0], frame->linesize[0]);
    }

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
    // Open the reader and start the thread to read frames
    // -----------

    LOG4CXX_DEBUG(logger, "Input options are:\n" << inOpts.streamOptions.as_string() << "\nOpening reader.");
    avtools::MediaReader rdr(inOpts.url, inOpts.streamOptions, avtools::InputMediaType::CAPTURE_DEVICE);
    const AVStream* pVidStr = rdr.getVideoStream();
    ThreadsafeFrame inFrame(pVidStr->codecpar->width, pVidStr->codecpar->height, (AVPixelFormat) pVidStr->codecpar->format);
    std::thread readerThread = threadedRead(inFrame, rdr);

    // -----------
    // Add transformer
    // -----------
    LOG4CXX_DEBUG(logger, "Opening transformer");
    ThreadsafeFrame trfFrame(pVidStr->codecpar->width, pVidStr->codecpar->height, PIX_FMT);
    assert( (AV_NOPTS_VALUE == trfFrame->best_effort_timestamp) && (AV_NOPTS_VALUE == trfFrame->pts) );
    // Get corners of the board
    cv::Mat trfMatrix = getPerspectiveTransformationMatrix(inFrame);
    return EXIT_FAILURE;
    // Start the warper thread
    std::thread warperThread = threadedWarp(inFrame, trfFrame, trfMatrix);
    // -----------
    // Open the output stream writers - one for lo-res, one for hi-res
    // -----------

    while (cv::waitKey(50) < 0)
    {
        {
            auto lock = trfFrame.getReadLock();
            auto img = getImage(trfFrame);
            cv::imshow("Warped image to write", img);
        }
    }
    std::thread writerThread1 = threadedWrite(trfFrame, outOptsLoRes);
    std::thread writerThread2 = threadedWrite(trfFrame, outOptsHiRes);

    // -----------
    // Cleanup
    // -----------
    readerThread.join();
    warperThread.join();
    writerThread1.join();
    writerThread2.join();
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

    /// @class structure that contains coordinates of a board.
    struct Board
    {
        std::vector<cv::Point2f> corners;   ///< Currently selected coordinates of the corners
        int draggedPt;                      ///< index of the point currently being dragged. -1 if none
        std::string name;                   ///< Name of the window

        /// Mouse callback for the window, replacing cv::MouseCallback
        static void OnMouse(int event, int x, int y, int flags, void *userdata)
        {
            Board* pC = static_cast<Board*>(userdata);
            assert(pC->draggedPt < (int) pC->corners.size());
            switch(event)
            {
                case cv::MouseEventTypes::EVENT_LBUTTONDOWN:
                    //See if we are near any existing markers
                    assert(pC->draggedPt < 0);
                    for (int i = 0; i < pC->corners.size(); ++i)
                    {
                        const cv::Point& pt = pC->corners[i];
                        if (cv::abs(x-pt.x) + cv::abs(y - pt.y) < 10)
                        {
                            pC->draggedPt = i;  //start dragging
                            break;
                        }
                    }
                    break;
                case cv::MouseEventTypes::EVENT_MOUSEMOVE:
                    if ((flags & cv::MouseEventFlags::EVENT_FLAG_LBUTTON) && (pC->draggedPt >= 0))
                    {
                        pC->corners[pC->draggedPt].x = x;
                        pC->corners[pC->draggedPt].y = y;
                    }
                    break;
                case cv::MouseEventTypes::EVENT_LBUTTONUP:
                    if (pC->draggedPt >= 0)
                    {
                        pC->corners[pC->draggedPt].x = x;
                        pC->corners[pC->draggedPt].y = y;
                        pC->draggedPt = -1; //end dragging
                    }
                    else if (pC->corners.size() < 4)
                    {
                        pC->corners.emplace_back(x,y);
                    }
                    break;
                default:
                    LOG4CXX_DEBUG(logger, "Received mouse event: " << event);
                    break;
            }
        }

        /// Ctpr
        /// @param[in] name of the board
        Board(const std::string& _name):
        draggedPt(-1),
        name(_name)
        {
            assert(!name.empty());
            cv::namedWindow(name);
            cv::setMouseCallback(name, Board::OnMouse, this);
        }

        ~Board()
        {
            cv::setMouseCallback(name, nullptr);
            cv::destroyWindow(name);
        }
    };  //<anon>::Board

    /// Calculates the aspect ratio (width / height) of the board from the four corners
    /// Reference:
    /// Zhengyou Zhang & Li-wei He, "Whiteboard Scanning and Image Enhancement", Digital Signal Processing, April 2007
    /// https://www.microsoft.com/en-us/research/publication/whiteboard-scanning-image-enhancement
    /// http://dx.doi.org/10.1016/j.dsp.2006.05.006
    /// Implementation examples:
    /// https://stackoverflow.com/questions/1194352/proportions-of-a-perspective-deformed-rectangle/1222855#1222855
    /// https://stackoverflow.com/questions/38285229/calculating-aspect-ratio-of-perspective-transform-destination-image
    /// @param[in] corners the four corners of the board
    /// @param[in] imSize size of the input images (where the corners belong)
    /// @return the estimated aspect ration
    float getAspectRatio(std::vector<cv::Point2f>& corners, const cv::Size& imSize)
    {
        // We will assume that corners contains the corners in the order top-left, top-right, bottom-right, bottom-left
        assert(corners.size() == 4);
        cv::Point2f center(imSize.width / 2.f, imSize.height / 2.f);
        cv::Point2f m1 = corners[0] - center;   //tl
        cv::Point2f m2 = corners[1] - center;   //tr
        cv::Point2f m3 = corners[3] - center;   //bl
        cv::Point2f m4 = corners[2] - center;   //br

        //TODO: Check k2 & k3 for numeric stability
        float k2 = ((m1.y-m4.y) * m3.x - (m1.x - m4.x) * m3.y + m1.x * m4.y - m1.y * m4.x) \
            / ((m2.y - m4.y) * m3.x - (m2.x - m4.x) * m3.y + m2.x * m4.y - m2.y * m4.x);

        float k3 = ((m1.y-m4.y) * m2.x - (m1.x - m4.x) * m2.y + m1.x * m4.y - m1.y * m4.x) \
            / ((m3.y - m4.y) * m2.x - (m3.x - m4.x) * m2.y + m3.x * m4.y - m3.y * m4.x);

        if ( (std::abs(k2 - 1.f) < 1E-4) || (std::abs(k3 - 1.f) < 1E-4) )
        {
            return std::sqrt( ( std::pow(m2.y - m1.y, 2) + std::pow(m2.x - m1.x, 2) )
                / (std::pow(m3.y - m1.y, 2) + std::pow(m3.x - m1.x, 2)) );
        }
        else
        {
            float fSqr = std::abs( ((k3 * m3.y - m1.y) * (k2 * m2.y - m1.y) + (k3 * m3.x - m1.x) * (k2 * m2.x - m1.x)) \
                / ((k3 - 1) * (k2 - 1)) );
            return std::sqrt( ( std::pow(k2 - 1.f, 2) + (std::pow(k2 * m2.y - m1.y, 2) + std::pow(k2 * m2.x - m1.x, 2)) / fSqr )
                / (std::pow(k3 - 1.f, 2) + (std::pow(k3 * m3.y - m1.y, 2) + std::pow(k3 * m3.x - m1.x, 2)) / fSqr ) );
        }
    }

    cv::Mat getPerspectiveTransformationMatrix(const ThreadsafeFrame& frame)
    {
        static const cv::Scalar FIXED_COLOR = cv::Scalar(0,0,255), DRAGGED_COLOR = cv::Scalar(255, 0, 0);
        static const std::string INPUT_WINDOW_NAME = "Input", OUTPUT_WINDOW_NAME = "Warped";
        cv::namedWindow(OUTPUT_WINDOW_NAME);
        Board board(INPUT_WINDOW_NAME);
        avtools::Frame intermediateFrame, warpedFrame;
        cv::Mat trfMatrix, inputImg, warpedImg;
        std::unique_ptr<avtools::ImageConversionContext> hConvCtx;
        std::vector<cv::Point2f> TGT_CORNERS;

        //Initialize
        {
            auto lock = frame.getReadLock();
            assert( (frame->width > 0) && (frame->height > 0) );
            if ( frame->format != PIX_FMT )
            {
                assert (!hConvCtx);
                // Initialize conversion context
                hConvCtx = std::make_unique<avtools::ImageConversionContext>(frame->width, frame->height, (AVPixelFormat) frame->format, frame->width, frame->height, PIX_FMT);
                intermediateFrame = avtools::Frame(frame->width, frame->height, PIX_FMT);
            }
            warpedImg = cv::Mat(frame->height, frame->width, CV_8UC3);
        }
        //Start the loop
        while ( cv::waitKey(50) < 0 )    //wait for key press
        {
            cv::Rect2f roi(cv::Point2f(), warpedImg.size());
            // See if a new input image is available, and copy to intermediate frame if so
            {
                auto lock = frame.tryReadLock(); //non-blocking attempt to lock
                if ( lock && (frame->best_effort_timestamp != AV_NOPTS_VALUE) )
                {
                    LOG4CXX_DEBUG(logger, "Transformer received frame with pts: " << frame->best_effort_timestamp);
                    //Convert frame to BGR24 to display if need be
                    if (hConvCtx)
                    {
                        hConvCtx->convert(frame, intermediateFrame);
                    }
                    else
                    {
                        assert( (intermediateFrame->width == frame->width) && (intermediateFrame->height == frame->height) && (intermediateFrame->format == frame->format) );
                        intermediateFrame = frame.clone();
                    }
                }
            }
            // Display intermediateFrame and warpedFrame if four corners have been chosen
            inputImg = getImage(intermediateFrame);
            if (inputImg.total() > 0)
            {
                const float IMG_ASPECT = (float) inputImg.cols / (float) inputImg.rows;
                int nCorners = (int) board.corners.size();
                if (nCorners == 4)
                {
                    //Calculate aspect ratio:
                    float aspect = getAspectRatio(board.corners, inputImg.size());
                    LOG4CXX_DEBUG(logger, "Calculated aspect ratio is: " << aspect);
                    if (aspect > IMG_ASPECT)
                    {
                        roi.height = (float) warpedImg.cols / aspect;
                        roi.y = (warpedImg.rows - roi.height) / 2.f;
                        assert(roi.y >= 0.f);
                    }
                    else
                    {
                        roi.width = (float) warpedImg.rows * aspect;
                        roi.x = (warpedImg.cols - roi.width) / 2.f;
                        assert(roi.x >= 0.f);
                    }
                    TGT_CORNERS = {roi.tl(), cv::Point2f(roi.x + roi.width, roi.y), roi.br(), cv::Point2f(roi.x, roi.y + roi.height)};

                    LOG4CXX_DEBUG(logger, "Will transform \n" << board.corners << " to \n" << TGT_CORNERS);
                    trfMatrix = cv::getPerspectiveTransform(board.corners, TGT_CORNERS);

                    cv::Mat_<double> trfBr = trfMatrix * cv::Mat_<double>({board.corners[2].x, board.corners[2].y, 1.f});
                    cv::Point2f br(trfBr[0][0], trfBr[1][0]);

                    //Calculate the constant in the transformation
                    float lambda = std::sqrt( (std::pow(TGT_CORNERS[2].x, 2) + std::pow(TGT_CORNERS[2].y, 2))
                                             / (std::pow(br.x, 2) + std::pow(br.y, 2)) );
                    trfMatrix = trfMatrix * lambda;
                    LOG4CXX_DEBUG(logger, "Calculated matrix " << trfMatrix << " will transform " << board.corners[2] << " to " << br);
                    warpedImg.setTo(cv::Scalar{0,0,0});
                    auto tgtImg = warpedImg(roi);
                    cv::warpPerspective(inputImg, tgtImg, trfMatrix, roi.size(), cv::InterpolationFlags::INTER_CUBIC);
                    for (int i = 0; i < 4; ++i)
                    {
                        cv::line(inputImg, board.corners[i], board.corners[(i+1) % 4], FIXED_COLOR);
                    }
                    cv::imshow(OUTPUT_WINDOW_NAME, warpedImg);
                }
                for (int i = 0; i < nCorners; ++i)
                {
                    auto color = (board.draggedPt == i ? DRAGGED_COLOR : FIXED_COLOR);
                    cv::drawMarker(inputImg, board.corners[i], color, cv::MarkerTypes::MARKER_SQUARE, 5);
                    cv::putText(inputImg, std::to_string(i+1), board.corners[i], cv::FONT_HERSHEY_SIMPLEX, 0.5, color);
                }
                cv::imshow(INPUT_WINDOW_NAME, inputImg);
            }
        }
        cv::destroyWindow(OUTPUT_WINDOW_NAME);
        if (board.corners.size() == 4)
        {
            assert(trfMatrix.total() > 0);
            LOG4CXX_DEBUG(logger, "Current perspective transform is :" << trfMatrix);
        }
        else
        {
            throw std::runtime_error("Perspective transform requires 4 points.");
        }
        return trfMatrix;
    }

    // ---------------------------
    // Threadsafe Frame Definitions
    // ---------------------------
    ThreadsafeFrame::ThreadsafeFrame():
    Frame(),
    mutex(),
    cv()
    {
    }

    ThreadsafeFrame::ThreadsafeFrame(int width, int height, AVPixelFormat format):
    Frame(width, height, format),
    mutex(),
    cv()
    {
    }

    ThreadsafeFrame::~ThreadsafeFrame() = default;

    void ThreadsafeFrame::update(const avtools::Frame &frm)
    {
        assert(pFrame_);
        if (frm)
        {
            LOG4CXX_DEBUG(logger, "Updating threadsafe frame with " << frm.info(1));
            {
                auto lock = getWriteLock();
                assert(lock.owns_lock());
                if ( (pFrame_->width != frm->width) || (pFrame_->height != frm->height) || (pFrame_->format != frm->format) || (pFrame_->colorspace != frm->colorspace) )
                {
                    LOG4CXX_DEBUG(logger, "Updating frame requires reinitialization");
                    av_frame_unref(pFrame_);
                    avtools::initVideoFrame(pFrame_, frm->width, frm->height, (AVPixelFormat) frm->format, frm->colorspace);
                }
                assert( (pFrame_->width == frm->width) && (pFrame_->height == frm->height) && (pFrame_->format == frm->format) && (pFrame_->colorspace == frm->colorspace) );
                assert(frm->data[0] && pFrame_->data[0]);
                av_frame_copy_props(pFrame_, frm.get());
                av_frame_copy(pFrame_, frm.get());
                type = frm.type;
                LOG4CXX_DEBUG(logger, "Updated frame info: " << info(1));
            }
        }
        else
        {
            // Put frame in uninitialized state
            av_freep(&pFrame_);
            type = AVMediaType::AVMEDIA_TYPE_UNKNOWN;
        }
        cv.notify_all();
    }

    // -------------------------
    // Threaded reader & writer functions
    // -------------------------
    std::thread threadedRead(ThreadsafeFrame& tFrame, avtools::MediaReader& rdr)
    {
        return std::thread([&tFrame, &rdr](){
            try
            {
                log4cxx::MDC::put("threadname", "reader");
                avtools::Frame frame(*rdr.getVideoStream()->codecpar);
                while (const AVStream* pS = rdr.read(frame))
                {
                    frame->best_effort_timestamp -= pS->start_time;
                    frame->pts -= pS->start_time;
                    LOG4CXX_DEBUG(logger, "Frame read: \n" << frame.info(1));
                    tFrame.update(frame);
                }
            }
            catch (std::exception& err)
            {
                LOG4CXX_ERROR(logger, err.what() << "\nExiting reader thread.");
            }
            tFrame.update(nullptr); //signal end
        });
    }

    std::thread threadedWrite(const ThreadsafeFrame& frame, Options& opts)
    {
        return std::thread([&frame](){
            log4cxx::MDC::put("threadname", "writer");
            thread_local avtools::TimeType pts = -1;
            try
            {
                while (frame)
                {
                    {
                        auto lock = frame.getReadLock();
                        LOG4CXX_DEBUG(logger, "Received frame with pts: " << frame->best_effort_timestamp);
                        if (frame->best_effort_timestamp > pts)
                        {
                            pts = frame->best_effort_timestamp;
                        }
                        frame.cv.wait(lock, [&frame](){return frame->best_effort_timestamp > pts;});
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));    //to simulate long write process
                    }
                }
            }
            catch (std::exception& err)
            {
                LOG4CXX_ERROR(logger, err.what() << "\nExiting writer thread.");
            }
            //Cleanup writer
            LOG4CXX_DEBUG(logger, "Closing writer.");
        });
    }

    std::thread threadedWarp(const ThreadsafeFrame& inFrame, ThreadsafeFrame& warpedFrame, const cv::Mat& trfMatrix)
    {
        return std::thread([&inFrame, &warpedFrame, trfMatrix](){
            log4cxx::MDC::put("threadname", "warper");
            std::unique_ptr<avtools::ImageConversionContext> hCtx = nullptr;
            {
                auto lk1 = inFrame.getReadLock();
                auto lk2 = warpedFrame.getReadLock();
                if ((inFrame->width != warpedFrame->width) || (inFrame->height != warpedFrame->height) || (inFrame->format != warpedFrame->format) )
                {
                    hCtx = std::make_unique<avtools::ImageConversionContext>(inFrame->width, inFrame->height, (AVPixelFormat) inFrame->format, warpedFrame->width, warpedFrame->height, (AVPixelFormat) warpedFrame->format);
                }
            }
            try
            {
                while (inFrame)
                {
                    {
                        auto wLock = warpedFrame.getWriteLock();
                        assert(warpedFrame);
                        auto rLock = inFrame.getReadLock();
                        if ( (inFrame->best_effort_timestamp > warpedFrame->best_effort_timestamp) || (warpedFrame->best_effort_timestamp == AV_NOPTS_VALUE) )
                        {
                            const cv::Mat inImg = getImage(inFrame);
                            //TODO: ADD ImageConvertor to change image pixel format, or adjust transform accordingly
                            // Results will be different, so this should be tested for quality
                            cv::Mat outImg = getImage(warpedFrame);
                            cv::warpPerspective(inImg, outImg, trfMatrix, inImg.size());
                            av_frame_copy_props(warpedFrame.get(), inFrame.get());
                            LOG4CXX_DEBUG(logger, "Warped frame info: " << warpedFrame.info(1));
                        }
                    }
                }
            }
            catch (std::exception& err)
            {
                LOG4CXX_ERROR(logger, err.what() << "\nExiting warper thread.");
            }
            warpedFrame.update(nullptr);
        });
    }
}   //::<anon>
