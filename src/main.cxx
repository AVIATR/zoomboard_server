//
//  main.cpp
//  transcoder
//
//  Created by Ender Tekin on 10/8/14.
//  Copyright (c) 2014 Smith-Kettlewell. All rights reserved.
//

#include <cassert>
#include <csignal>
#include <string>
#include <algorithm>
#include <vector>
#include <thread>
#include <mutex>
#include <functional>
#include <iostream>
#include <log4cxx/logger.h>
#include <log4cxx/basicconfigurator.h>
#include <log4cxx/consoleappender.h>
#include <log4cxx/fileappender.h>
#include <log4cxx/patternlayout.h>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include "opencv2/core.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/highgui.hpp"
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}
#include "version.h"
#include "MediaReader.hpp"
#include "MediaWriter.hpp"
#include "Media.hpp"
#include "ThreadsafeFrame.hpp"

using avtools::MediaError;
namespace
{
    namespace bfs = ::boost::filesystem;
    namespace bpo = ::boost::program_options;
    /// @class A structure containing the pertinent ffmpeg options
    /// See https://www.ffmpeg.org/ffmpeg-devices.html for the list of codec & stream options
    struct Options
    {
        std::string url;                    ///< Url to open output stream
        avtools::Dictionary codecOpts;      ///< codec options
        avtools::Dictionary muxerOpts;      ///< muxer options
    };

    inline std::ostream& operator<<(std::ostream& stream, const Options& opts)
    {
        return ( stream << "url:" << opts.url << "\n"
                << "codec options:\n" << opts.codecOpts.as_string()
                << "muxer options:\n" << opts.muxerOpts.as_string() );
    }

    /// Parses a json file to retrieve the output configuration to use
    /// @param[in] configFile name of configuration file to read
    /// @return output options to use for the writer
    /// @throw std::runtime_error if there is an issue parsing the configuration file.
    Options getOptions(const std::string& configFile, const std::string& type);

    /// Removes all files with a given prefix
    /// @param[in] prefix file prefix
    /// @param[in] path path to search
    /// @param[in] assumeYes if true, do not confirm erasure
    void removeFiles(const std::string& prefix, const bfs::path& path=".", bool assumeYes=false);

    /// @class Synchronization thread to signal end and capture exceptions from threads
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
        inline void end();
        /// @return true if the program has been signaled to end
        inline bool isEnded() const;
        /// Used to log an exception from a thread. This also signals program end
        /// @param[in] an exception pointer. This will be stored and later logged at program end
        void addException(std::exception_ptr errPtr);
        /// @return true if there are logged exceptions
        bool hasExceptions() const;
        /// Logs all the exceptions. Once logged, all exceptions are cleared.
        void logExceptions();
    };  //::<anon>::ProgramStatus

    /// Function that starts a stream reader that reads from a stream int to a threaded frame
    /// @param[in,out] pFrame threadsafe frame to write to
    /// @param[in] rdr an opened media reader
    /// @return a new thread that reads frames from the input stream and updates the threaded frame
    std::thread threadedRead(std::weak_ptr<avtools::ThreadsafeFrame> pFrame, avtools::MediaReader& rdr);

    /// Function that starts a stream writer that writes to a stream from a threaded frame
    /// @param[in] pFrame threadsafe frame to read from
    /// @param[in] writer media writer instance
    /// @return a new thread that reads frames from the input frame and writes to an output file
    std::thread threadedWrite(std::weak_ptr<const avtools::ThreadsafeFrame> pFrame, avtools::MediaWriter& writer);

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

    /// Used to signal that the reader thread has frames ready, so writed can wait
//    static ReadySignal g_ReaderReady;
    // Initialize logger
    log4cxx::LoggerPtr logger(log4cxx::Logger::getLogger("zoombrd"));

    static const std::string INPUT_DRIVER = "avfoundation";
    static const AVPixelFormat PIX_FMT = AVPixelFormat::AV_PIX_FMT_BGR24;
} //::<anon>

// The command we are trying to implement for one output stream is
// sudo avconv -f video4linux2 -r 5 -s hd1080 -i /dev/video0 \
//  -vf "format=yuv420p,framerate=5" -c:v libx264 -profile:v:0 high -level 3.0 -flags +cgop -g 1 \
//  -hls_time 0.1 -hls_allow_cache 0 -an -preset ultrafast /mnt/hls/stream.m3u8
// For further help, see https://libav.org/avconv.html
int main(int argc, const char * argv[])
{
    // Set up signal handler to end program
    std::signal( SIGINT, [](int){g_Status.end();} );
    std::cout << "press Ctrl+C to exit..." << std::endl;

    // Set up logger. See https://logging.apache.org/log4cxx/latest_stable/usage.html for more info,
    //see https://logging.apache.org/log4cxx/latest_stable/apidocs/classlog4cxx_1_1_pattern_layout.html for patterns
    log4cxx::MDC::put("threadname", "main");    //add name of thread
    log4cxx::LayoutPtr layoutPtr(new log4cxx::PatternLayout("%d %-5p [%-8X{threadname} %.8t] %c{1} - %m%n"));
    log4cxx::AppenderPtr consoleAppPtr(new log4cxx::ConsoleAppender(layoutPtr));
    log4cxx::BasicConfigurator::configure(consoleAppPtr);
    //Also add file appender - see https://stackoverflow.com/questions/13967382/how-to-set-log4cxx-properties-without-property-file
    log4cxx::AppenderPtr fileAppenderPtr(new log4cxx::FileAppender(layoutPtr, "logfile.txt", false));
    log4cxx::BasicConfigurator::configure(fileAppenderPtr);

    // Set log level.
#ifndef NDEBUG
    const auto debugLevel = log4cxx::Level::getDebug();
#else
    const auto debugLevel = log4cxx::Level::getWarn();
#endif
    log4cxx::Logger::getRootLogger()->setLevel(debugLevel);
    logger->setLevel(debugLevel);
    LOG4CXX_DEBUG(logger,"Created ConsoleAppender appender");

    //Parse command line options
    static const std::string PROGRAM_NAME = bfs::path(argv[0]).filename().string() + " v" + std::to_string(RTMP_SERVER_VERSION_MAJOR) + "." + std::to_string(RTMP_SERVER_VERSION_MINOR);

    bpo::options_description programDesc(PROGRAM_NAME + " options");
    bpo::positional_options_description posDesc;
    bpo::variables_map vm;
    posDesc.add("config_file", -1);
    programDesc.add_options()
    ("help,h", "produce help message")
    ("version,v", "program version")
    ("config_file", bpo::value<std::string>(), "path of configuration file to use for video options")
    ("yes,y", "answer 'yes' to every prompt'")
    ;

    try
    {
        bpo::store(bpo::command_line_parser(argc, argv).options(programDesc).positional(posDesc).run(), vm);
        bpo::notify(vm);
    }
    catch (std::exception& err)
    {
        LOG4CXX_FATAL(logger, "Error parsing command line arguments:" << err.what() << programDesc);
        return EXIT_FAILURE;
    }
    if (vm.count("help"))
    {
        std::cout << programDesc << std::endl;
        return EXIT_SUCCESS;
    }
    else if (vm.count("version"))
    {
        std::cout << PROGRAM_NAME << std::endl;
        return EXIT_SUCCESS;
    }

    if (!vm.count("config_file"))
    {
        LOG4CXX_FATAL(logger, "No configuration file provided!\n" << programDesc)
        return EXIT_FAILURE;
    }
    const std::string configFile = vm["config_file"].as<std::string>();
    LOG4CXX_INFO(logger, "Provided configuration file: " << configFile);
//    const std::string USAGE = "Usage: " + std::string(argv[0]) + " <config_file.json>";
//    // Parse command line arguments & load the config file
//    if (argc != 2)
//    {
//        LOG4CXX_FATAL(logger, "Incorrect number of arguments." << USAGE);
//        return EXIT_FAILURE;
//    }
//    const std::string configFile = argv[1];
    //Until we convert to C++17, we need to use boost::filesystem to check for file. Afterwards, we can use std::filesystem
    if ( !bfs::exists( configFile ) )
    {
        LOG4CXX_FATAL(logger, "Could not find configuration file " << configFile);
        return EXIT_FAILURE;
    }

    Options inOpts = getOptions(configFile, "input");
    Options outOptsLR = getOptions(configFile, "output_lr");
    Options outOptsHR = getOptions(configFile, "output_hr");
    // Remove old stream files if they're around
    removeFiles(bfs::path(outOptsLR.url).stem().string(), bfs::path(outOptsLR.url).root_path(), vm.count("yes"));
    removeFiles(bfs::path(outOptsHR.url).stem().string(), bfs::path(outOptsHR.url).root_path(), vm.count("yes"));


    // -----------
    // Open the reader and start the thread to read frames
    // -----------
    LOG4CXX_DEBUG(logger, "Input options are:\n" << inOpts << "\nOpening reader.");
    avtools::MediaReader rdr(inOpts.url, inOpts.muxerOpts);
    const AVStream* pVidStr = rdr.getVideoStream();
    LOG4CXX_DEBUG(logger, "Input stream info:\n" << avtools::getStreamInfo(pVidStr) );
    auto pInFrame = avtools::ThreadsafeFrame::Get(pVidStr->codecpar->width, pVidStr->codecpar->height, PIX_FMT, pVidStr->time_base);
    std::thread readerThread = threadedRead(pInFrame, rdr);

    // -----------
    // Add perspective transformer
    // -----------
    LOG4CXX_DEBUG(logger, "Opening transformer");
    // Get corners of the board
    cv::Mat trfMatrix = getPerspectiveTransformationMatrix(pInFrame);
    cv::destroyAllWindows();

    // -----------
    // Open the writer
    // -----------
    LOG4CXX_DEBUG(logger, "LR Output options are:\n" << outOptsLR << "\nOpening low-res writer.");
    avtools::MediaWriter writerLR(outOptsLR.url, outOptsLR.codecOpts, outOptsLR.muxerOpts);
    const AVStream* pOutStrLR = writerLR.getStream();

    LOG4CXX_DEBUG(logger, "HR Output options are:\n" << outOptsHR << "\nOpening hi-res writer.");
    avtools::MediaWriter writerHR(outOptsHR.url, outOptsHR.codecOpts, outOptsHR.muxerOpts);
    const AVStream* pOutStrHR = writerHR.getStream();

    // -----------
    // Start warping & writing
    // -----------
    // Start the warper thread
    auto pTrfFrame = avtools::ThreadsafeFrame::Get(pVidStr->codecpar->width, pVidStr->codecpar->height, PIX_FMT, pVidStr->time_base);
    assert( (AV_NOPTS_VALUE == (*pTrfFrame)->best_effort_timestamp) && (AV_NOPTS_VALUE == (*pTrfFrame)->pts) );
    std::thread warperThread = threadedWarp(pInFrame, pTrfFrame, trfMatrix);

    // Start the writer thread
    LOG4CXX_DEBUG(logger, "Lo-res output stream info:\n" << avtools::getStreamInfo(pOutStrLR));
    std::thread writerThreadLR = threadedWrite(pTrfFrame, writerLR);
    LOG4CXX_DEBUG(logger, "Hi-res output stream info:\n" << avtools::getStreamInfo(pOutStrHR));
    std::thread writerThreadHR = threadedWrite(pTrfFrame, writerHR);

    // -----------
    // Cleanup
    // -----------
    readerThread.join();    //wait for reader thread to finish
    warperThread.join();    //wait for the warper thread to finish
    writerThreadLR.join();    //wait for writer to finish
    writerThreadHR.join();    //wait for writer to finish

    if (g_Status.hasExceptions())
    {
        LOG4CXX_ERROR(logger, "Exiting with errors: ");
        g_Status.logExceptions();
        return EXIT_FAILURE;
    }
    LOG4CXX_DEBUG(logger, "Exiting successfully...");
    return EXIT_SUCCESS;
}

namespace
{
    /// Compare two strings in a case-insensitive manner
    /// @return true if two strings are the same
    bool strequals(const std::string& a, const std::string& b)
    {
        return std::equal(a.begin(), a.end(),
                          b.begin(), b.end(),
                          [](char a, char b) {
                              return tolower(a) == tolower(b);
                          });
    }

    /// Parses a property tree and fills the options structure
    void getOptsFromTree(const boost::property_tree::ptree& tree, Options& opts)
    {
        for (auto& branch : tree)
        {
            if ( strequals(branch.first, "url") )
            {
                opts.url = branch.second.get_value<std::string>();
            }
            if ( strequals(branch.first, "codec_options") )
            {
                for (auto &leaf : branch.second)
                {
                    opts.codecOpts.add(leaf.first, leaf.second.get_value<std::string>());
                }
            }
            if ( strequals(branch.first, "muxer_options") )
            {
                for (auto &leaf : branch.second)
                {
                    opts.muxerOpts.add(leaf.first, leaf.second.get_value<std::string>());
                }
            }
        }
    }

    Options getOptions(const std::string& configFile, const std::string& type)
    {
        boost::property_tree::ptree tree;
        boost::property_tree::read_json(configFile, tree);
        Options opts;
        bool isOptsFound=false;
        // Read input options
        for (auto& subtree: tree)
        {
            if ( strequals(subtree.first, type) )
            {
                isOptsFound = true;
                getOptsFromTree(subtree.second, opts);
            }
        }
        if (!isOptsFound)
        {
            throw std::runtime_error("Couldn't find " + type + " options in " + configFile);
        }
        return opts;
    }

    void removeFiles(const std::string& prefix, const bfs::path& path/*="."*/, bool assumeYes/*=false*/)
    {
        bfs::path pathParsed = (path.empty() ? "." : path);
        LOG4CXX_DEBUG(logger, "Will remove " << prefix << "* from " << pathParsed);
        if( !bfs::exists(pathParsed) || !bfs::is_directory(pathParsed))
        {
            throw std::runtime_error(pathParsed.string() + " is not a valid path.");
        }
        std::vector<bfs::path> filesToRemove;
        for (bfs::recursive_directory_iterator it(pathParsed), itEnd; it != itEnd; ++it)
        {
            if( bfs::is_regular_file(*it)
               && (it->path().filename().string().find(prefix) == 0)
               && ((it->path().extension().string() == ".ts") || (it->path().extension().string() == ".m3u8") )
               )
            {
                filesToRemove.push_back(it->path());
            }
        }
        if ( (not assumeYes) && (not filesToRemove.empty()) )
        {
            char answer;
            do
            {
                std::cout << "Found " << filesToRemove.size() << " files starting with " << prefix << ". Remove them? [y/n]" << std::endl;
                std::cin >> answer;
            }
            while( !std::cin.fail() && (answer != 'y') && (answer != 'Y')&& (answer != 'n') && (answer != 'N') );

            if ((answer == 'n') || (answer == 'N'))
            {
                return;
            }
        }
#ifndef NDEBUG
        int nFilesRemoved = 0;
        std::for_each(filesToRemove.begin(), filesToRemove.end(),
                      [&nFilesRemoved](const bfs::path& p)
                      {
                          if (bfs::remove(p))
                          {
                              LOG4CXX_DEBUG(logger, "Removed: " << p);
                              ++nFilesRemoved;
                          }
                          else
                          {
                              LOG4CXX_DEBUG(logger, "Could not remove: " << p);
                          }
                      });
        LOG4CXX_DEBUG(logger, "Removed " << nFilesRemoved << " of " << filesToRemove.size() << " files.");
#else
        std::for_each(filesToRemove.begin(), filesToRemove.end(), [](const bfs::path& p){bfs::remove(p);});
#endif
    }

    // ---------------------------
    // Program Status Definitions
    // ---------------------------
    ProgramStatus::ProgramStatus():
    mutex_(),
    doEnd_(false),
    exceptions_()
    {
    }

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
        std::function<void(const std::exception&)> logNestedException =
            [&logNestedException](const std::exception& e)
            {
                LOG4CXX_ERROR(logger, e.what() << ":");
                try
                {
                    std::rethrow_if_nested(e);
                }
                catch (const std::exception& nested)
                {
                    logNestedException(nested);
                }
            };

        std::lock_guard<std::mutex> lk(mutex_);
        for (auto const& e : exceptions_)
        {
            try
            {
                std::rethrow_exception(e);
            }
            catch (const std::exception& err)
            {
                logNestedException(err);
            }
        }
        exceptions_.clear();
    }

//    // -------------------------
//    // ReadySignal definitions
//    // -------------------------
//    ReadySignal::ReadySignal():
//    mutex_(),
//    isReady_(false)
//    {}
//
//    ReadySignal::~ReadySignal() = default;
//
//    void ReadySignal::ready()
//    {
//        isReady_.store(true);
//    }
//
//    bool ReadySignal::isReady() const
//    {
//        return isReady_.load();
//    }

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
                    assert( av_cmp_q(frame.timebase, pS->time_base) == 0);
                    LOG4CXX_DEBUG(logger, "Frame read: \n" << avtools::getFrameInfo(frame.get(), pS, 1));
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
            LOG4CXX_DEBUG(logger, "Exiting reader thread.");
        });
    }

    std::thread threadedWrite(std::weak_ptr<const avtools::ThreadsafeFrame> pFrame, avtools::MediaWriter& writer)
    {
        return std::thread([pFrame, &writer](){
            try
            {
                log4cxx::MDC::put("threadname", "writer");
                avtools::TimeType ts = AV_NOPTS_VALUE;
                while (!g_Status.isEnded())
                {
                    auto ppFrame = pFrame.lock();
                    if (!ppFrame)
                    {
                        LOG4CXX_DEBUG(logger, "Writer received null frame - closing.");
                        writer.write(nullptr, avtools::TimeBaseType{});
                        break;
                    }
                    const auto& inFrame = *ppFrame;
                    {
                        auto lock = inFrame.getReadLock();
                        inFrame.cv.wait(lock, [&inFrame, ts](){return g_Status.isEnded() || (inFrame->best_effort_timestamp > ts);});   //note that we assume the timebase of the incoming frames do not change
                        if (g_Status.isEnded())
                        {
                            writer.write(nullptr, avtools::TimeBaseType{});
                            break;
                        }
                        assert(inFrame->best_effort_timestamp > ts);
                        ts = inFrame->best_effort_timestamp;
                        LOG4CXX_DEBUG(logger, "Writer received frame:\n" << inFrame.info(1));
                        //Push frame to filtergraph
                        writer.write(inFrame);
                    }
                }
            }
            catch (std::exception& err)
            {
                try
                {
                    std::throw_with_nested( std::runtime_error("writer thread error") );
                }
                catch (...)
                {
                    g_Status.addException(std::current_exception());
                    g_Status.end();
                }
            }
            //Cleanup writer
            LOG4CXX_DEBUG(logger, "Exiting writer thread");
        });
    }

    // ---------------------------
    // Transformer Definitions
    // ---------------------------
    /// Converts a cv::point to a string representation
    /// @param[in] pt the point to represent
    /// @return a string representation of the point in the form (pt.x, pt.y)
    template<typename T>
    std::string to_string(const cv::Point_<T>& pt)
    {
        return "(" + std::to_string(pt.x) + "," + std::to_string(pt.y) + ")";
    }

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

    /// @class structure that contains coordinates of a board.
    struct Board
    {
        std::vector<cv::Point2f> corners;   ///< Currently selected coordinates of the corners
        int draggedPt;                      ///< index of the point currently being dragged. -1 if none
    };  //<anon>::Board

    /// Mouse callback for the window, replacing cv::MouseCallback
    static void OnMouse(int event, int x, int y, int flags, void *userdata)
    {
        Board* pC = static_cast<Board*>(userdata);
        assert(pC->draggedPt < (int) pC->corners.size());
        cv::Point2f curPt(x,y);
        switch(event)
        {
            case cv::MouseEventTypes::EVENT_LBUTTONDOWN:
                //See if we are near any existing markers
                assert(pC->draggedPt < 0);
                for (int i = 0; i < pC->corners.size(); ++i)
                {
                    if (cv::norm(curPt - pC->corners[i]) < 10.f)
                    {
                        pC->draggedPt = i;  //start dragging
                        break;
                    }
                }
                break;
            case cv::MouseEventTypes::EVENT_MOUSEMOVE:
                if ((flags & cv::MouseEventFlags::EVENT_FLAG_LBUTTON) && (pC->draggedPt >= 0))
                {
                    pC->corners[pC->draggedPt] = curPt;
                }
                break;
            case cv::MouseEventTypes::EVENT_LBUTTONUP:
                if (pC->draggedPt >= 0)
                {
                    pC->corners[pC->draggedPt] = curPt;
                    pC->draggedPt = -1; //end dragging
                }
                else if (pC->corners.size() < 4)
                {
                    pC->corners.emplace_back(x,y);
                }
                break;
            default:
                break;
        }
    }

    cv::Mat_<double> getPerspectiveTransformationMatrix(std::weak_ptr<const avtools::ThreadsafeFrame> pFrame)
    {
        static const cv::Scalar FIXED_COLOR = cv::Scalar(0,0,255), DRAGGED_COLOR = cv::Scalar(255, 0, 0);
        static const std::string INPUT_WINDOW_NAME = "Input", OUTPUT_WINDOW_NAME = "Warped";
        cv::startWindowThread();
        Board board;
        cv::Mat_<double> trfMatrix;
        cv::Mat inputImg, warpedImg;
        std::vector<cv::Point2f> TGT_CORNERS;
        avtools::Frame intermediateFrame;
        cv::Mat warpedImage;
        board.draggedPt = -1;
        cv::namedWindow(INPUT_WINDOW_NAME);
        cv::namedWindow(OUTPUT_WINDOW_NAME);
        cv::setMouseCallback(INPUT_WINDOW_NAME, OnMouse, &board);

        //Initialize
        {
            auto ppFrame = pFrame.lock();
            if (!ppFrame)
            {
                throw std::runtime_error("Invalid frame provided to calculate perspective transform");
            }
            auto lock = ppFrame->getReadLock();
            intermediateFrame = ppFrame->clone();
            warpedImg = cv::Mat((*ppFrame)->height, (*ppFrame)->width, CV_8UC3);
        }
        //Start the loop
        while ( !g_Status.isEnded() && (cv::waitKey(20) < 0) )    //wait for key press
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
                ppFrame->clone(intermediateFrame);
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
                        assert(roi.width == (float) warpedImg.cols);
                        assert(roi.x == 0.f);
                        roi.y = (warpedImg.rows - roi.height) / 2.f;
                        assert(roi.y >= 0.f);
                    }
                    else
                    {
                        roi.width = (float) warpedImg.rows * aspect;
                        assert(roi.height == (float) warpedImg.rows);
                        roi.x = (warpedImg.cols - roi.width) / 2.f;
                        assert(roi.x >= 0.f);
                        assert(roi.y == 0.f);
                    }
                    TGT_CORNERS = {roi.tl(), cv::Point2f(roi.x + roi.width, roi.y), roi.br(), cv::Point2f(roi.x, roi.y + roi.height)};

                    LOG4CXX_DEBUG(logger, "Need to transform \n" << board.corners << " to \n" << TGT_CORNERS);
                    trfMatrix = cv::getPerspectiveTransform(board.corners, TGT_CORNERS);

                    //Calculate the constant in the transformation
                    cv::Point2f& bbr = board.corners[2];
                    auto warpPt = [](const cv::Point2f& pt, cv::Mat_<float> trfMat)
                    {
                        return cv::Point2f(
                                           trfMat[0][0] * pt.x + trfMat[0][1] * pt.y + trfMat[0][2],
                                           trfMat[1][0] * pt.x + trfMat[1][1] * pt.y + trfMat[1][2]
                                           );
                    };
                    cv::Point2f br = warpPt(bbr, trfMatrix);
                    LOG4CXX_DEBUG(logger, "Calculated matrix \n" << trfMatrix << " will transform \n"
                                  << board.corners[0] << " -> " << warpPt(board.corners[0], trfMatrix)
                                  << board.corners[1] << " -> " << warpPt(board.corners[1], trfMatrix)
                                  << board.corners[2] << " -> " << warpPt(board.corners[2], trfMatrix)
                                  << board.corners[3] << " -> " << warpPt(board.corners[3], trfMatrix)
                                  );
                    double lambda = cv::norm(roi.br()) / cv::norm(br);
                    trfMatrix = trfMatrix * lambda;
                    br = warpPt(bbr, trfMatrix);

                    LOG4CXX_DEBUG(logger, "Scaled matrix \n" << trfMatrix << " will transform \n"
                                  << board.corners[0] << " -> " << warpPt(board.corners[0], trfMatrix)
                                  << board.corners[1] << " -> " << warpPt(board.corners[1], trfMatrix)
                                  << board.corners[2] << " -> " << warpPt(board.corners[2], trfMatrix)
                                  << board.corners[3] << " -> " << warpPt(board.corners[3], trfMatrix)
                                  );
                    warpedImg = 0;
                    auto tgtImg = warpedImg(roi);
                    cv::warpPerspective(inputImg, tgtImg, trfMatrix, roi.size(), cv::InterpolationFlags::INTER_LINEAR);
                    cv::imshow(OUTPUT_WINDOW_NAME, warpedImg);
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
        if (g_Status.isEnded())
        {
            return cv::Mat();
        }
        if (board.corners.size() == 4)
        {
            assert(trfMatrix.total() > 0);
            LOG4CXX_DEBUG(logger, "Current perspective transform is :" << trfMatrix);
        }
        else
        {
            throw std::runtime_error("Perspective transform requires 4 points.");
        }
        cv::setMouseCallback(INPUT_WINDOW_NAME, nullptr, nullptr);
        cv::destroyAllWindows();
        cv::waitKey(10);
        return trfMatrix;
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
                            assert( (av_cmp_q(warpedFrame.timebase, inFrame.timebase) == 0) && (warpedFrame.type == AVMediaType::AVMEDIA_TYPE_VIDEO) && (inFrame.type == AVMediaType::AVMEDIA_TYPE_VIDEO) );
                            cv::Mat inImg = getImage(inFrame);  //should we instead copy this data to this thread?
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

}   //::<anon>
