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
#ifndef NDEBUG
#include <log4cxx/fileappender.h>
#endif
#include <log4cxx/patternlayout.h>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/program_options.hpp>
#include "opencv2/core.hpp"
#include "opencv2/imgproc.hpp"
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}
#include "common.hpp"
#include "MediaReader.hpp"
#include "MediaWriter.hpp"
#include "Media.hpp"
#include "ThreadsafeFrame.hpp"
#include "ProgramStatus.hpp"
#include "correct_perspective.hpp"
#include "libav2opencv.hpp"

using avtools::MediaError;
namespace
{
    namespace bpo = ::boost::program_options;
    /// @class A structure containing the pertinent ffmpeg options
    /// See https://www.ffmpeg.org/ffmpeg-devices.html for the list of codec & stream options
    struct Options
    {
        std::string url;                    ///< Url to open output stream
        avtools::Dictionary codecOpts;      ///< codec options
        avtools::Dictionary muxerOpts;      ///< muxer options
    };

    /// Prints options stream
    /// @param[in] stream output stream
    /// @param[in] opts options structure
    /// @return a reference to the output stream
    inline std::ostream& operator<<(std::ostream& stream, const Options& opts)
    {
        return ( stream << "url:" << opts.url << "\n"
                << "codec options:\n" << opts.codecOpts.as_string()
                << "muxer options:\n" << opts.muxerOpts.as_string() );
    }

    /// Parses a json file to retrieve the output configuration to use
    /// @param[in] configFile name of configuration file to read
    /// @return set of output options to use for the reader & writer(s)
    /// @throw std::runtime_error if there is an issue parsing the configuration file.
    std::map<std::string, Options> getOptions(const std::string& coonfigFile);

    /// Sets up the output location, creates the folder if it doesn ot exist, asks to remove pre-existing stream files etc.
    /// @param[in] url output url
    /// @param[in] doAssumeYes if true, assume yes to all questions and do not prompy
    void setUpOutputLocations(const fs::path& url, bool doAssumeYes);

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

    /// Launches a thread that creates a warped matrix of the input frame according to the given transform matrix
    /// @param[in] pInFrame input frame
    /// @param[in, out] pWarpedFrame transformed output frame
    /// @param[in] trfMatrix transform matrix
    /// @return a new thread that runs in the background, updates the warpedFrame when a new inFrame is available.
    std::thread threadedWarp(std::weak_ptr<const avtools::ThreadsafeFrame> pInFrame, std::weak_ptr<avtools::ThreadsafeFrame> pWarpedFrame, const cv::Mat& trfMatrix);

    // Initialize logger
    log4cxx::LoggerPtr logger(log4cxx::Logger::getLogger("zoombrd"));

} //::<anon>

/// Maintains communication between threads re: exceptions & program end
ProgramStatus g_Status(logger);

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
    log4cxx::AppenderPtr fileAppenderPtr(new log4cxx::FileAppender(layoutPtr, fs::path(argv[0]).filename().string()+".log", false));
    log4cxx::BasicConfigurator::configure(fileAppenderPtr);

    // Set log level.
#ifndef NDEBUG
    const auto debugLevel = log4cxx::Level::getDebug();
    av_log_set_level(AV_LOG_VERBOSE);
#else
    const auto debugLevel = log4cxx::Level::getWarn();
    av_log_set_level(AV_LOG_ERROR);
#endif
    log4cxx::Logger::getRootLogger()->setLevel(debugLevel);
    logger->setLevel(debugLevel);

    //Parse command line options
    static const std::string PROGRAM_NAME = fs::path(argv[0]).filename().string() + " v" + std::to_string(ZOOMBOARD_SERVER_VERSION_MAJOR) + "." + std::to_string(ZOOMBOARD_SERVER_VERSION_MINOR);

    bpo::options_description programDesc(PROGRAM_NAME + " options");
    bpo::positional_options_description posDesc;
    bpo::variables_map vm;
    posDesc.add("config_file", -1);
    programDesc.add_options()
    ("help,h", "produce help message")
    ("version,v", "program version")
    ("yes,y", "answer 'yes' to every prompt'")
    ("adjust,a", "adjust perspective")
    ("output_folder,o", bpo::value<std::string>()->default_value("."), "output folder to write the streams")
    ("calibration_file,c", "calibration file to use if using aruco markers, create by calibrate_camera. If none is provided, and the adjust option is also passed, then a window is provided for the user to click on the corners of the board.")
    ("config_file", bpo::value<std::string>()->default_value("config.json"), "path of configuration file to use for video options")
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

    assert(vm.count("config_file"));
    assert(vm.count("output_folder"));

    const std::string configFile = vm["config_file"].as<std::string>();
    //Until we convert to C++17, we need to use boost::filesystem to check for file. Afterwards, we can use std::filesystem
    if ( !fs::exists( configFile ) )
    {
        throw std::runtime_error("Could not find configuration file " + configFile);
    }
    LOG4CXX_INFO(logger, "Using configuration file: " << configFile);

    std::map<std::string, Options> opts = getOptions(configFile);
    auto pInputOpts = opts.find("input");
    if (pInputOpts == opts.end())
    {
        throw std::runtime_error("Could not find input options in the configuration file.");
    }
    Options& inOpts = pInputOpts->second;
    LOG4CXX_DEBUG(logger, "Input options are:\n" << inOpts << "\nOpening reader.");
    avtools::MediaReader rdr(inOpts.url, inOpts.muxerOpts);
    const AVStream* pVidStr = rdr.getVideoStream();
    LOG4CXX_DEBUG(logger, "Input stream info:\n" << avtools::getStreamInfo(pVidStr) );

    //Done with input options, treat all remaining options as output options
    opts.erase(pInputOpts);

    // Create output folders if they do not exixt
    const fs::path outputFolder(vm["output_folder"].as<std::string>());
    for (auto& opt: opts)
    {
        opt.second.url = (outputFolder / fs::path(opt.second.url)).string();
        setUpOutputLocations(opt.second.url, vm.count("yes"));
    }

    // -----------
    // Open the reader and start the thread to read frames
    // -----------
    std::vector<std::thread> threads;
    auto pInFrame = avtools::ThreadsafeFrame::Get(pVidStr->codecpar->width, pVidStr->codecpar->height, PIX_FMT, pVidStr->time_base);
    threads.emplace_back(threadedRead(pInFrame, rdr));

    // -----------
    // Start writing
    // -----------
    // Open the writer(s)
    std::vector<avtools::MediaWriter> writers;
    for (auto& opt: opts)
    {
        Options& outOpt = opt.second;
        LOG4CXX_DEBUG(logger, "Output options for url " << outOpt.url << " are:\n" << outOpt << "\nOpening writer.");
        writers.emplace_back(outOpt.url, outOpt.codecOpts, outOpt.muxerOpts);
        LOG4CXX_DEBUG(logger, "Output stream info:\n" << avtools::getStreamInfo(writers.back().getStream()));
    }

    // Start writing (and correct perspective if requested)
    if (vm.count("adjust") > 0) //perspective adjustment requested
    {
        // Get corners of the board
        cv::Mat trfMatrix;
        if (vm.count("calibration_file"))
        {
            LOG4CXX_DEBUG(logger, "Calibration file found, will use Aruco markers for perspective adjustment.");
            trfMatrix = getPerspectiveTransformationMatrixFromMarkers(pInFrame, vm["calibration_file"].as<std::string>());
        }
        else
        {
            LOG4CXX_DEBUG(logger, "Please click on the four corners of the board for perspective adjustment.");
            trfMatrix = getPerspectiveTransformationMatrixFromUser(pInFrame);
        }
        assert(!trfMatrix.empty());
        // Start the warper thread if need be
        auto pTrfFrame = avtools::ThreadsafeFrame::Get(pVidStr->codecpar->width, pVidStr->codecpar->height, PIX_FMT, pVidStr->time_base);
        assert( (AV_NOPTS_VALUE == (*pTrfFrame)->best_effort_timestamp) && (AV_NOPTS_VALUE == (*pTrfFrame)->pts) );
        threads.emplace_back(threadedWarp(pInFrame, pTrfFrame, trfMatrix));
        // add writers to writer perspective transformed frames
        for (auto &writer : writers)
        {
            threads.emplace_back( threadedWrite(pTrfFrame, writer) );
        }
    }
    else
    {
        // add writers to writer input frames
        for (auto &writer : writers)
        {
            threads.emplace_back( threadedWrite(pInFrame, writer) );
        }
    }

    // -----------
    // Cleanup
    // -----------
    for (auto& thread: threads)
    {
        if (thread.joinable())
        {
            thread.join();  //wait for all threads to finish
        }
    }

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
    Options getOptsFromTree(const boost::property_tree::ptree& tree)
    {
        Options opts;
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
        return opts;
    }

    std::map<std::string, Options> getOptions(const std::string& configFile)
    {
        namespace bpt = ::boost::property_tree;
        bpt::ptree tree;
        bpt::read_json(configFile, tree);
        std::map<std::string, Options> opts;
        for (auto& subtree: tree)
        {
            opts.emplace(subtree.first, getOptsFromTree(subtree.second));
        }
        return opts;
    }

    void setUpOutputLocations(const fs::path& url, bool doAssumeYes)
    {
        assert(url.has_parent_path());
        fs::path parent = url.parent_path();
        //See if it exists or needs to be created
        if ( !fs::exists(parent) )
        {
            LOG4CXX_DEBUG(logger, "Url folder " << parent << " does not exist. Creating.");
            if (!fs::create_directory(parent))
            {
                throw std::runtime_error("Unable to create folder " + parent.string());
            }
        }
        else if (!fs::is_directory(parent))
        {
            throw std::runtime_error(parent.string() + " exists, and is not a folder.");
        }
        assert(fs::is_directory(parent));
        // Remove old stream files if they're around
        fs::path prefix = url.stem();
        LOG4CXX_DEBUG(logger, "Will remove " << prefix << "* from " << parent);
        std::vector<fs::path> filesToRemove;
        for (fs::recursive_directory_iterator it(parent), itEnd; it != itEnd; ++it)
        {
            if( fs::is_regular_file(*it)
               && (it->path().filename().string().find(prefix.string()) == 0)
               && ((it->path().extension().string() == ".ts") || (it->path().extension().string() == ".m3u8") )
               )
            {
                filesToRemove.push_back(it->path());
            }
        }
        if ( !filesToRemove.empty() )
        {
            LOG4CXX_INFO(logger, "Found " << filesToRemove.size() << " files starting with '" << prefix << "'"<< std::endl);
            if ( doAssumeYes || promptYesNo("Remove them?") )
            {
#ifndef NDEBUG
                int nFilesRemoved = 0;
                std::for_each(filesToRemove.begin(), filesToRemove.end(),
                              [&nFilesRemoved](const fs::path& p)
                              {
                                  if (fs::remove(p))
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
        }

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
                log4cxx::MDC::put("threadname", "writer: " + writer.url());
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
