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
        avtools::Dictionary codecOpts;      ///< codec options
        avtools::Dictionary muxerOpts;      ///< muxer options
    };

    /// Prints options stream
    /// @param[in] stream output stream
    /// @param[in] opts options structure
    /// @return a reference to the output stream
    inline std::ostream& operator<<(std::ostream& stream, const Options& opts)
    {
        return ( stream << "codec options:\n" << opts.codecOpts << "muxer options:\n" << opts.muxerOpts );
    }

    /// Compares two strings
    /// @param[in] a first string
    /// @param[in] b second string
    /// @return true if the two strings are equivalent. Case is ignored.
    bool strequals(const std::string& a, const std::string& b);

    /// Parses a json file to retrieve the output configuration to use
    /// @param[in] configFile name of configuration file to read
    /// @return set of output options to use for the reader & writer(s)
    /// @throw std::runtime_error if there is an issue parsing the configuration file.
    std::map<std::string, Options> getOptions(const std::string& coonfigFile);

    /// Sets up the output location, creates the folder if it doesn ot exist, asks to remove pre-existing stream files etc.
    /// @param[in] url output url
    /// @param[in] doAssumeYes if true, assume yes to all questions and do not prompy
    void setUpOutputLocations(const fs::path& url, bool doAssumeYes);

    /// Copies necessary output options from an input stream, when only an output file is passed
    /// @param[in] pStr input video stream
    /// @return an options structure with the muxer & codc options filled in from the values in pStr
    Options getOptsFromStream(const AVStream* pStr);

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
    posDesc.add("input", 1);
    posDesc.add("output", 1);
    programDesc.add_options()
    ("help,h", "produce help message")
    ("version,v", "program version")
    ("yes,y", "answer 'yes' to every prompt")
    ("adjust,a", "adjust perspective")
    ("calibration_file,c", bpo::value<std::string>(), "calibration file to use if using aruco markers, created by calibrate_camera. If none is provided, and the adjust option is also passed, then a window is provided for the user to click on the corners of the board.")
    ("output,o", bpo::value<std::string>()->default_value("output.json"), "output file or configuration file")
    ("input,i", bpo::value<std::string>()->default_value("input.json"), "input file or configuration file.")
    ;

    bpo::variables_map vm;
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
#ifndef NDEBUG
    LOG4CXX_DEBUG(logger, "Program arguments:");
    for (auto it: vm)
    {
        LOG4CXX_DEBUG(logger, it.first << ": " << it.second.as<std::string>());
    }
#endif

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

    assert(vm.count("input"));
    assert(vm.count("output"));

    // -----------
    // Open the reader and start the thread to read frames
    // -----------
    const fs::path input = vm["input"].as<std::string>();
    if ( !fs::exists( input ) )
    {
        throw std::runtime_error("Could not find input " + input.string());
    }
    std::map<std::string, Options> inputOpts;
    if ( strequals(input.extension().string(), ".json") )
    {
        LOG4CXX_INFO(logger, "Using input configuration file: " << input);
        inputOpts = getOptions(input.string());
        if (inputOpts.size() != 1)
        {
            throw std::runtime_error("Only one input is allowed, found " + std::to_string(inputOpts.size()));
        }
        LOG4CXX_DEBUG(logger, "Input options:\nURL = " << inputOpts.begin()->first << "\nOptions = " << inputOpts.begin()->second);
    }
    else
    {
        LOG4CXX_INFO(logger, "Using input file: " << input);
        inputOpts[input.string()] = Options();
    }
    assert(inputOpts.size() == 1);
    LOG4CXX_DEBUG(logger, "Opening reader for " << inputOpts.begin()->first);
    avtools::MediaReader rdr(inputOpts.begin()->first, inputOpts.begin()->second.muxerOpts);
    const AVStream* pVidStr = rdr.getVideoStream();
    LOG4CXX_DEBUG(logger, "Input stream info:\n" << avtools::getStreamInfo(pVidStr) );

    // Create output folders if they do not exixt
//    const fs::path outputFolder(vm["output_folder"].as<std::string>());
//    for (auto& opt: opts)
//    {
//        opt.second.url = (outputFolder / fs::path(opt.second.url)).string();
//        setUpOutputLocations(opt.second.url, vm.count("yes"));
//    }

    std::vector<std::thread> threads;
    auto pInFrame = avtools::ThreadsafeFrame::Get(pVidStr->codecpar->width, pVidStr->codecpar->height, PIX_FMT, pVidStr->time_base);
    threads.emplace_back(threadedRead(pInFrame, rdr));

    // -----------
    // Open the outputs and start writer threads
    // -----------
    // Open the writer(s)
    std::vector<avtools::MediaWriter> writers;
    const fs::path output = vm["output"].as<std::string>();
    if ( strequals(output.extension().string(), ".json") )
    {
        LOG4CXX_INFO(logger, "Using output configuration file: " << output);

        std::map<std::string, Options> outputOpts = getOptions(output.string());
        for (auto opt: outputOpts)
        {
            fs::path dir(opt.first);
            setUpOutputLocations(dir.parent_path(), vm.count("yes"));
            LOG4CXX_DEBUG(logger, "Opening writer for URL: " << opt.first <<"\nOptions: " << opt.second);
            writers.emplace_back(opt.first, opt.second.codecOpts, opt.second.muxerOpts);
            LOG4CXX_DEBUG(logger, "Output stream info:\n" << avtools::getStreamInfo(writers.back().getStream()));
        }
    }
    else
    {
        LOG4CXX_INFO(logger, "Using output file: " << output);
        Options outOpts = getOptsFromStream(pVidStr);   //copy required options from the input stream
        writers.emplace_back(output.string(), outOpts.codecOpts, outOpts.muxerOpts);
    }

    // Start writing (and correct perspective if requested)
    cv::Mat trfMatrix;
    auto pTrfFrame = avtools::ThreadsafeFrame::Get(pVidStr->codecpar->width, pVidStr->codecpar->height, PIX_FMT, pVidStr->time_base);
    if (vm.count("adjust") > 0) //perspective adjustment requested
    {
        // Get corners of the board
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
    }
    if (trfMatrix.empty())
    {
        if (vm.count("adjust") > 0)
        {
            LOG4CXX_WARN(logger, "Unable to detect perspective, continuing without perspective adjustment.");
        }
        // add writers to writer input frames
        for (auto &writer : writers)
        {
            threads.emplace_back( threadedWrite(pInFrame, writer) );
        }
    }
    else  // Start the warper thread
    {
        LOG4CXX_DEBUG(logger, "Will apply perspective transform using transformation matrix: " << trfMatrix);
        assert( (AV_NOPTS_VALUE == (*pTrfFrame)->best_effort_timestamp) && (AV_NOPTS_VALUE == (*pTrfFrame)->pts) );
        threads.emplace_back(threadedWarp(pInFrame, pTrfFrame, trfMatrix));
        // add writers to writer perspective transformed frames
        for (auto &writer : writers)
        {
            threads.emplace_back( threadedWrite(pTrfFrame, writer) );
        }
    }

    // -----------
    // Cleanup
    // -----------
    //TODO: There is a potential issue of the program (likely the reader thread) crashes before all threads are joined.
    // We should switch to a task oriented approach or move the thread pool to programstatus, which joins the threads before ending
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

    /// Fills a dictionary from a json map
    void readMapIntoDict(const cv::FileNode& node, avtools::Dictionary& dict)
    {
        assert(node.isMap());
        for (auto n: node)
        {
            if (!n.isNamed())
            {
                throw std::runtime_error("Unable to parse node");
            }
            dict.add(n.name(), (std::string) n);
        }
    }

    std::pair<std::string, Options> getOptsFromMapNode(const cv::FileNode& node)
    {
        if (!node.isMap())
        {
            throw std::runtime_error("Unable to parse node.");
        }
        std::pair<std::string, Options> opts;

        //Find url
        auto n = node["url"];
        if (!n.isString())
        {
            throw std::runtime_error("Unable to parse url");
        }
        else
        {
            n >> opts.first;
        }

        //Find muxer options
        n = node["muxer_options"];
        if (n.empty() || n.isNone())
        {
            LOG4CXX_INFO(logger, "No muxer options found for " << opts.first);
        }
        else if (n.isMap())
        {
            //Read all options to dict
            readMapIntoDict(n, opts.second.muxerOpts);
        }
        else
        {
            throw std::runtime_error("Unable to parse muxer options for " + opts.first);
        }

        //Find codec options
        n = node["codec_options"];
        if (n.empty() || n.isNone())
        {
            LOG4CXX_INFO(logger, "No codec options found for " << opts.first);
        }
        else if (n.isMap())
        {
            //Read all options to dict
            readMapIntoDict(n, opts.second.codecOpts);
        }
        else
        {
            throw std::runtime_error("Unable to parse codec options for " + opts.first);
        }
        return opts;
    }

    std::map<std::string, Options> getOptions(const std::string& configFile)
    {
        std::map<std::string, Options> opts;
        cv::FileStorage cfs(configFile, cv::FileStorage::READ);
        cv::FileNode root = cfs.root();
        try
        {
            if (root.isSeq())
            {
                //assume that there are several configuration options, most likely for output
                for (auto subNode: root)
                {
                    auto val = getOptsFromMapNode(subNode);
                    if ( opts.find(val.first) != opts.end() )
                    {
                        throw std::runtime_error("Multiple options found for url: " + val.first);
                    }
                    opts[val.first] = val.second;
                }
                return opts;
            }
            else
            {
                //assume it is a map. getOptsFromNode will throw if not
                auto val = getOptsFromMapNode(root);
                opts[val.first] = val.second;
                return opts;
            }
        }
        catch (std::exception& err)
        {
            std::throw_with_nested( std::runtime_error("Unable to parse " + configFile) );
        }
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

    Options getOptsFromStream(const AVStream* pStr)
    {
        Options opts;
        opts.muxerOpts.add("framerate", pStr->r_frame_rate);
        opts.codecOpts.add("video_size", std::to_string(pStr->codecpar->width) + "x" + std::to_string(pStr->codecpar->height));
        opts.codecOpts.add("pixel_format", std::to_string((AVPixelFormat) pStr->codecpar->format) );
        return opts;
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
                        g_Status.end(); //if the reader ends, end program
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
                        if (g_Status.isEnded())
                        {
                            break;
                        }
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
