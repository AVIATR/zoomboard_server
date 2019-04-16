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
//#include <future>
#include <sys/stat.h>
#include <log4cxx/logger.h>
#include <log4cxx/basicconfigurator.h>
#include <log4cxx/consoleappender.h>
//#include <log4cxx/fileappender.h>
#include <log4cxx/patternlayout.h>
#include <opencv2/opencv.hpp>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
//#include <libavutil/opt.h>
#include <libavutil/avutil.h>
}
#include "version.h"
#include "MediaReader.hpp"
#include "MediaWriter.hpp"
#include "Media.hpp"
#include "ThreadsafeFrame.hpp"

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
        avtools::Dictionary options;        ///< stream & codec options, such as frame rate, video size, pixel format
    };

    class ProgramStatus
    {
    private:
        mutable std::mutex mutex_;                      ///< mutex to use when adding exceptions
        std::atomic_bool doEnd_;                        ///< set to true to signal program should end
        std::vector<std::exception_ptr> exceptions_;    ///< list of stored exceptions
    public:
        /// Ctor
        ProgramStatus();
        /// Dtor
        ~ProgramStatus();
        /// Used to signal the program to end.
        void end();
        /// True if the program has been signaled to end
        bool isEnded() const;
        /// Used to log an exception from a thread. This also signals program end
        /// @param[in] an exception pointer. This will be stored and later logged at program end
        void addException(std::exception_ptr errPtr);
        /// @return true if there are logged exceptions
        bool hasExceptions() const;
        /// Logs all the exceptions. Once logged, all exceptions are cleared.
        void logExceptions();
    };  //::<anon>::ProgramStatus

    /// Parses a json file to retrieve the configuration to use
    /// @param[in] configFile name of configuration file to read
    /// @param[out] inputOptions a structure containing the input options to use
    /// @param[out] outputOptionsLowRes a structure containing the output stream options to use for the low-res stream
    /// @param[out] outputOptionsHiRes a structure containing the output stream options to use for the hi-res stream
    /// @throw std::runtime_error if there is an issue parsing the configuration file.
    void getOptions(
        const std::string& configFile,
        Options& inputOptions,
        Options& outputOptionsLowRes,
        Options& outputOptionsHiRes
    );

    /// Function that starts a stream reader that reads from a stream int to a threaded frame
    /// @param[in,out] pFrame threadsafe frame to write to
    /// @param[in] rdr an opened media reader
    /// @return a new thread that reads frames from the input stream and updates the threaded frame
    std::thread threadedRead(std::weak_ptr<avtools::ThreadsafeFrame> pFrame, avtools::MediaReader& rdr);

    /// Function that starts a stream writer that writes to a stream from a threaded frame
    /// @param[in] pFrame threadsafe frame to read from
    /// @param[in] writer media writer instance
    /// @param[in] threadname name of writer thread (for debugging purposes)
    /// @return a new thread that reads frames from the input frame and writes to an output file
    std::thread threadedWrite(std::weak_ptr<const avtools::ThreadsafeFrame> pFrame, avtools::MediaWriter& writer, const std::string& threadname="writer");

    /// Asks the user to choose the corners of the board and undoes the
    /// perspective transform. This can then be used with
    /// cv::warpPerspective to correct the perspective of the video.
    /// also @see https://docs.opencv.org/3.1.0/da/d54/group__imgproc__transform.html
    /// @param[in] pFrame input frame that will be updated by the reader thread
    /// @return a perspective transformation matrix
    /// TODO: Should also return the roi so we send a smaller framesize if need be (no need to send extra data)
    cv::Mat_<double> getPerspectiveTransformationMatrix(std::weak_ptr<const avtools::ThreadsafeFrame> pFrame);

    /// Launches a thread that creates a warped matrix of the input frame according to the given transform matrix
    /// @param[in] pInFrame input frame
    /// @param[in, out] pWarpedFrame transformed output frame
    /// @param[in] trfMatrix transform matrix
    /// @return a new thread that runs in the background, updates the warpedFrame when a new inFrame is available.
    std::thread threadedWarp(std::weak_ptr<const avtools::ThreadsafeFrame> pInFrame, std::weak_ptr<avtools::ThreadsafeFrame> pWarpedFrame, const cv::Mat& trfMatrix);

    /// Wraps a cv::mat around libav frames. Note that the matrix is just wrapped around the
    /// existing data, so data is not cloned. Make sure that the matrix is done being used before reading new
    /// data into the frame.
    /// @param[in] frame a decoded video frame
    /// @return a cv::mat wrapper around the frame data
    inline const cv::Mat getImage(const avtools::Frame& frame)
    {
        return cv::Mat(frame->height, frame->width, CV_8UC3, frame->data[0], frame->linesize[0]);
    }

    /// Wraps a cv::mat around libav frames. Note that the matrix is just wrapped around the
    /// existing data, so data is not cloned. Make sure that the matrix is done being used before reading new
    /// data into the frame.
    /// @param[in] frame a decoded video frame
    /// @return a cv::mat wrapper around the frame data
    inline cv::Mat getImage(avtools::Frame& frame)
    {
        return cv::Mat(frame->height, frame->width, CV_8UC3, frame->data[0], frame->linesize[0]);
    }

    /// Maintains communication between threads re: exceptions & program end
    static ProgramStatus g_Status;
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
    LOG4CXX_DEBUG(logger, "Input options are:\n" << inOpts.options.as_string() << "\nOpening reader.");
    avtools::MediaReader rdr(inOpts.url, inOpts.options, avtools::InputMediaType::CAPTURE_DEVICE);
    const AVStream* pVidStr = rdr.getVideoStream();
    auto pInFrame = avtools::ThreadsafeFrame::Get(pVidStr->codecpar->width, pVidStr->codecpar->height, PIX_FMT);
    std::thread readerThread = threadedRead(pInFrame, rdr);

    // -----------
    // Add perspective transformer
    // -----------
    LOG4CXX_DEBUG(logger, "Opening transformer");
    // Get corners of the board
    cv::Mat trfMatrix = getPerspectiveTransformationMatrix(pInFrame);
    // Start the warper thread
    auto pTrfFrame = avtools::ThreadsafeFrame::Get(pVidStr->codecpar->width, pVidStr->codecpar->height, PIX_FMT);
    assert( (AV_NOPTS_VALUE == (*pTrfFrame)->best_effort_timestamp) && (AV_NOPTS_VALUE == (*pTrfFrame)->pts) );
    std::thread warperThread = threadedWarp(pInFrame, pTrfFrame, trfMatrix);

    // -----------
    // Open the output stream writers - one for lo-res, one for hi-res
    // -----------
    avtools::CodecParameters codecPar(pVidStr->codecpar);
    codecPar->codec_id = AVCodecID::AV_CODEC_ID_H264;
    avtools::MediaWriter lrWriter(outOptsLoRes.url, codecPar, pVidStr->time_base, outOptsLoRes.options);
    //    avtools::MediaWriter hrWriter(outOptsHiRes.url, codecPar, pVidStr->time_base, outOptsHiRes.options);
    std::thread writerThread1 = threadedWrite(pTrfFrame, lrWriter, "writer_lo");
//    std::thread writerThread2 = threadedWrite(pTrfFrame, hrWriter, "writer_hi");

    // display the image and continue until someone hits a key
    const std::string OUTPUT_WINDOW = "Warped image";
    cv::namedWindow(OUTPUT_WINDOW);
    while ( !g_Status.isEnded() )
    {
        {
            auto lock = pTrfFrame->tryReadLock();
            if (lock)
            {
                cv::imshow(OUTPUT_WINDOW, getImage(*pTrfFrame));
            }
        }
        if (cv::waitKey(20) >= 0)
        {
            LOG4CXX_DEBUG(logger, "Received key press");
            g_Status.end();
            cv::destroyWindow(OUTPUT_WINDOW);
        }
    }

    // -----------
    // Cleanup
    // -----------
    assert(g_Status.isEnded());
    readerThread.join();    //wait for reader thread to finish
    warperThread.join();    //wait for warper frame to finish
    writerThread1.join();   //wait for writer frame to finish
//    writerThread2.join();
    if (g_Status.hasExceptions())
    {
        g_Status.logExceptions();
        return EXIT_FAILURE;
    }
    LOG4CXX_DEBUG(logger, "Exiting successfully...");
    return EXIT_SUCCESS;
}

namespace
{
    template<typename T>
    std::string to_string(const cv::Point_<T>& pt)
    {
        return "(" + std::to_string(pt.x) + "," + std::to_string(pt.y) + ")";
    }

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
        bool isUrlFound = false, isOptsFound = false;
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
            else if (n.name() == "options")
            {
                readFileNodeIntoDict(n, opts.options);
                if (isOptsFound)
                {
                    throw std::runtime_error("Multiple option entries in config file for " + node.name());
                }
                isOptsFound = true;
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
        if (!isOptsFound) //note that this can be skipped for stream defaults
        {
            LOG4CXX_INFO(logger, "Stream options not found in config file for " << node.name());
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

    // ---------------------------
    // Program Status Definitions
    // ---------------------------

    /// recursively print exception whats:
    /// @param[in] An exception. If there are nested exceptions, they are also logged.
    void logNestedExceptions(std::exception& e)
    {
        LOG4CXX_ERROR(logger, e.what() << ":");
        try
        {
            std::rethrow_if_nested(e);
        }
        catch (std::exception& nested)
        {
            logNestedExceptions(nested);
        }
    }

    ProgramStatus::ProgramStatus():
    mutex_(),
    doEnd_(false),
    exceptions_()
    {}

    ProgramStatus::~ProgramStatus()
    {
        logExceptions();
    }

    void ProgramStatus::end()
    {
        doEnd_.store(true);
    }

    bool ProgramStatus::isEnded() const
    {
        return doEnd_.load();
    }

    void ProgramStatus::addException(std::exception_ptr errPtr)
    {
        std::lock_guard<std::mutex> lk(mutex_);
        exceptions_.push_back(errPtr);
    }

    bool ProgramStatus::hasExceptions() const
    {
        std::lock_guard<std::mutex> lk(mutex_);
        return !exceptions_.empty();
    }

    void ProgramStatus::logExceptions()
    {
        std::lock_guard<std::mutex> lk(mutex_);
        for (auto const& e : exceptions_)
        {
            try
            {
                std::rethrow_exception(e);
            }
            catch (std::exception& err)
            {
                logNestedExceptions(err);
            }
        }
        exceptions_.clear();
    }



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
    /// @return the estimated aspect ratio
    /// @throw runtime_error if the corners do not have sufficient separation to calculate the aspect ratio
    float getAspectRatio(std::vector<cv::Point2f>& corners, const cv::Size& imSize)
    {
        // We will assume that corners contains the corners in the order top-left, top-right, bottom-right, bottom-left
        assert(corners.size() == 4);
        for (int i = 0; i < 4; ++i)
        {
            if (cv::norm(corners[i] - corners[(i+1)%4]) < 1)
            {
                throw std::runtime_error("The provided corners " + to_string(corners[i]) + " and " + to_string(corners[(i+1)%4]) + "are too close.");
            }
        }
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

        if ( (std::abs(k2 - 1.f) < 1E-8) || (std::abs(k3 - 1.f) < 1E-8) )
        {
            LOG4CXX_DEBUG(logger, "parallel? k2 = " << k2 << ", k3 = " << k3);
            return std::sqrt( ( std::pow(m2.y - m1.y, 2) + std::pow(m2.x - m1.x, 2) )
                / (std::pow(m3.y - m1.y, 2) + std::pow(m3.x - m1.x, 2)) );
        }
        else
        {
            float fSqr = std::abs( ((k3 * m3.y - m1.y) * (k2 * m2.y - m1.y) + (k3 * m3.x - m1.x) * (k2 * m2.x - m1.x)) \
                / ((k3 - 1) * (k2 - 1)) );
            LOG4CXX_DEBUG(logger, "Calculated f^2 = " << fSqr << "[k2 = " << k2 << ", k3=" << k3 <<"]");
            return std::sqrt( ( std::pow(k2 - 1.f, 2) + (std::pow(k2 * m2.y - m1.y, 2) + std::pow(k2 * m2.x - m1.x, 2)) / fSqr )
                / (std::pow(k3 - 1.f, 2) + (std::pow(k3 * m3.y - m1.y, 2) + std::pow(k3 * m3.x - m1.x, 2)) / fSqr ) );
        }
    }

    cv::Mat_<double> getPerspectiveTransformationMatrix(std::weak_ptr<const avtools::ThreadsafeFrame> pFrame)
    {
        static const cv::Scalar FIXED_COLOR = cv::Scalar(0,0,255), DRAGGED_COLOR = cv::Scalar(255, 0, 0);
        static const std::string INPUT_WINDOW_NAME = "Input", OUTPUT_WINDOW_NAME = "Warped";
        cv::namedWindow(OUTPUT_WINDOW_NAME);
        Board board(INPUT_WINDOW_NAME);
        cv::Mat_<double> trfMatrix;
        cv::Mat inputImg, warpedImg;
        std::vector<cv::Point2f> TGT_CORNERS;
        avtools::Frame intermediateFrame;
        cv::Mat warpedImage;

        //Initialize
        {
            auto ppFrame = pFrame.lock();
            if (!ppFrame)
            {
                throw std::runtime_error("Invalid frame provided to calculate perspective transform");
            }
            auto lock = ppFrame->getReadLock();
            intermediateFrame = avtools::Frame((*ppFrame)->width, (*ppFrame)->height, PIX_FMT);
            warpedImg = cv::Mat((*ppFrame)->height, (*ppFrame)->width, CV_8UC3);
        }
        //Start the loop
        while ( cv::waitKey(50) < 0 )    //wait for key press
        {
            auto ppFrame = pFrame.lock();
            if (!ppFrame)
            {
                break;
            }
            cv::Rect2f roi(cv::Point2f(), warpedImg.size());
            // See if a new input image is available, and copy to intermediate frame if so
            {
                auto lock = ppFrame->tryReadLock(); //non-blocking attempt to lock
                assert((*ppFrame)->format == PIX_FMT);
                if ( lock && ((*ppFrame)->best_effort_timestamp != AV_NOPTS_VALUE) )
                {
                    LOG4CXX_DEBUG(logger, "Transformer received frame with pts: " << (*ppFrame)->best_effort_timestamp);
                }
                intermediateFrame = ppFrame->clone();
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

                    LOG4CXX_DEBUG(logger, "Need to transform \n" << board.corners << " to \n" << TGT_CORNERS);
                    trfMatrix = cv::getPerspectiveTransform(board.corners, TGT_CORNERS);

                    //Calculate the constant in the transformation
                    cv::Point2f br(
                            trfMatrix[0][0] * board.corners[2].x + trfMatrix[0][1] * board.corners[2].y + trfMatrix[0][2],
                            trfMatrix[1][0] * board.corners[2].x + trfMatrix[1][1] * board.corners[2].y + trfMatrix[1][2]);
                    LOG4CXX_DEBUG(logger, "Calculated matrix \n" << trfMatrix << " will transform \n" << board.corners[2] << " to \n" << br);
                    double lambda = cv::norm(TGT_CORNERS[2]) / cv::norm(br);
                    trfMatrix = trfMatrix * lambda;
                    br.x = trfMatrix[0][0] * board.corners[2].x + trfMatrix[0][1] * board.corners[2].y + trfMatrix[0][2];
                    br.y = trfMatrix[1][0] * board.corners[2].x + trfMatrix[1][1] * board.corners[2].y + trfMatrix[1][2];

                    LOG4CXX_DEBUG(logger, "Scaled matrix (lambda=" << lambda << ")\n" << trfMatrix << " will transform \n" << board.corners[2] << " to \n" << br);
                    auto tgtImg = warpedImg(roi);
                    cv::warpPerspective(inputImg, tgtImg, trfMatrix, tgtImg.size(), cv::InterpolationFlags::INTER_LINEAR);
                    cv::imshow(OUTPUT_WINDOW_NAME, tgtImg);
                    for (int i = 0; i < 4; ++i)
                    {
                        cv::line(inputImg, board.corners[i], board.corners[(i+1) % 4], FIXED_COLOR);
                    }
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

    // -------------------------
    // Threaded reader & writer functions
    // -------------------------
    std::thread threadedRead(std::weak_ptr<avtools::ThreadsafeFrame> pFrame, avtools::MediaReader& rdr)
    {
        return std::thread([pFrame, &rdr](){
            try
            {
                log4cxx::MDC::put("threadname", "reader");
                avtools::Frame frame(*rdr.getVideoStream()->codecpar);
                while (!g_Status.isEnded())
                {
                    const AVStream* pS = rdr.read(frame);
                    if (!pS)
                    {
                        break;
                    }
                    frame->best_effort_timestamp -= pS->start_time;
                    frame->pts -= pS->start_time;
                    LOG4CXX_DEBUG(logger, "Frame read: \n" << frame.info(1));
                    auto ppFrame = pFrame.lock();
                    if (!ppFrame)
                    {
                        throw std::runtime_error("Threaded input frame is null.");
                    }
                    ppFrame->update(frame);
                }
            }
            catch (std::exception& err)
            {
                LOG4CXX_DEBUG(logger, "Caught exception in reader thread" << err.what());
                try
                {
                    std::throw_with_nested( std::runtime_error("Reader thread error") );
                }
                catch (...)
                {
                    g_Status.addException(std::current_exception());
                    g_Status.end();
                }
            }
            LOG4CXX_DEBUG(logger, "Exiting reader thread: isEnded=" << std::boolalpha << g_Status.isEnded());
        });
    }

    std::thread threadedWrite(std::weak_ptr<const avtools::ThreadsafeFrame> pFrame, avtools::MediaWriter& writer, const std::string& threadname/*="writer"*/)
    {
        return std::thread([pFrame, &writer, threadname](){
            try
            {
                log4cxx::MDC::put("threadname", threadname);
                avtools::TimeType ts = AV_NOPTS_VALUE;
                std::unique_ptr<avtools::ImageConversionContext> hConvCtx = nullptr;
                avtools::Frame convFrame;
                //Initialization
                {
                    auto ppFrame = pFrame.lock();
                    if (!ppFrame)
                    {
                        LOG4CXX_ERROR(logger, "Received null frame, closing without writing.");
                        return;
                    }
                    const auto& frame = *ppFrame;
                    const auto pCPar = writer.getStream()->codecpar;
                    auto lock = frame.getReadLock();
                    if ( (frame->width != pCPar->width) || (frame->height != pCPar->height) || (frame->format != pCPar->format))
                    {
                        hConvCtx = std::make_unique<avtools::ImageConversionContext>(frame->width, frame->height, (AVPixelFormat) frame->format, pCPar->width, pCPar->height, (AVPixelFormat) pCPar->format );
                        if (!hConvCtx)
                        {
                            throw std::runtime_error("Unable to allocate conversion context for writer.");
                        }
                        convFrame = avtools::Frame(pCPar->width, pCPar->height, (AVPixelFormat) pCPar->format);
                    }
                }
                while (!g_Status.isEnded())
                {
                    auto ppFrame = pFrame.lock();
                    if (!ppFrame)
                    {
                        LOG4CXX_DEBUG(logger, "Writer received null frame - closing.");
                        writer.write(nullptr);
                        break;
                    }
                    const auto& inFrame = *ppFrame;
                    {
                        auto lock = inFrame.getReadLock();
                        LOG4CXX_DEBUG(logger, "Received frame with pts: " << inFrame->best_effort_timestamp);
                        LOG4CXX_DEBUG(logger, "Last written timestamp:" << ts);
//                        inFrame.cv.wait_for(lock, std::chrono::milliseconds(10), [&inFrame, ts](){return g_Status.isEnded() || (inFrame->best_effort_timestamp > ts);});
                        inFrame.cv.wait(lock, [&inFrame, ts](){return g_Status.isEnded() || (inFrame->best_effort_timestamp > ts);});
                        if (g_Status.isEnded())
                        {
                            break;
                        }
                        if (inFrame->best_effort_timestamp <= ts)
                        {
                            continue;
                        }
                        ts = inFrame->best_effort_timestamp;
                        if (hConvCtx)
                        {
                            hConvCtx->convert(inFrame, convFrame);
                            LOG4CXX_DEBUG(logger, "Writing frame: \n" << convFrame.info(1));
                            writer.write(convFrame.get());
                        }
                        else
                        {
                            LOG4CXX_DEBUG(logger, "Writing frame: \n" << inFrame.info(1));
                            writer.write(inFrame.get());
                        }
                    }
                }
            }
            catch (std::exception& err)
            {
                LOG4CXX_DEBUG(logger, "Caught exception in thread:" << threadname << ":" << err.what());
                try
                {
                    std::throw_with_nested( std::runtime_error(threadname + " thread error") );
                }
                catch (...)
                {
                    g_Status.addException(std::current_exception());
                    g_Status.end();
                }
            }
            //Cleanup writer
            LOG4CXX_DEBUG(logger, "Closing writer and exiting writer thread.");
        });
    }

    std::thread threadedWarp(std::weak_ptr<const avtools::ThreadsafeFrame> pInFrame, std::weak_ptr<avtools::ThreadsafeFrame> pWarpedFrame, const cv::Mat& trfMatrix)
    {
        return std::thread([pInFrame, pWarpedFrame, trfMatrix](){
            try
            {
                log4cxx::MDC::put("threadname", "warper");
                avtools::TimeType ts = AV_NOPTS_VALUE;
                while (!g_Status.isEnded())
                {
                    auto ppInFrame = pInFrame.lock();
                    if (!ppInFrame)
                    {
                        throw std::runtime_error("Warper received null frame.");
                    }
                    const auto& inFrame = *ppInFrame;
                    {
                        auto rLock = inFrame.getReadLock();
                        inFrame.cv.wait(rLock, [&inFrame, ts](){return g_Status.isEnded() ||  (inFrame->best_effort_timestamp > ts);});    //wait until fresh frame is available
                        if (g_Status.isEnded())
                        {
                            break;
                        }
                        ts = inFrame->best_effort_timestamp;
                        auto ppWarpedFrame = pWarpedFrame.lock();
                        if (!ppWarpedFrame )
                        {
                            throw std::runtime_error("Warper output frame is null");
                        }
                        auto& warpedFrame = *ppWarpedFrame;
                        {
                            auto wLock = warpedFrame.getWriteLock();
                            assert(warpedFrame->best_effort_timestamp < ts);
                            cv::Mat inImg = getImage(inFrame);
                            cv::Mat outImg = getImage(warpedFrame);
                            cv::warpPerspective(inImg, outImg, trfMatrix, inImg.size(), cv::InterpolationFlags::INTER_LANCZOS4);
                            int ret = av_frame_copy_props(warpedFrame.get(), inFrame.get());
                            if (ret < 0)
                            {
                                throw avtools::MediaError("Unable to copy frame properties", ret);
                            }
                            LOG4CXX_DEBUG(logger, "Warped frame info: \n" << warpedFrame.info(1));
                        }
                        warpedFrame.cv.notify_all();
                    }
                }
            }
            catch (std::exception& err)
            {
                LOG4CXX_DEBUG(logger, "Caught exception in warper thread" << err.what());
                try
                {
                    std::throw_with_nested( std::runtime_error("Warper thread error") );
                }
                catch (...)
                {
                    g_Status.addException(std::current_exception());
                    g_Status.end();
                }
            }
            LOG4CXX_DEBUG(logger, "Exiting warper thread: isEnded=" << std::boolalpha << g_Status.isEnded());
        });
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
}   //::<anon>
