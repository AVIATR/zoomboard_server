//
//  main.cpp
//  transcoder
//
//  Created by Ender Tekin on 10/8/14.
//  Copyright (c) 2014 Smith-Kettlewell. All rights reserved.
//

#include <cassert>
#include <csignal>
#include <cstdio>
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
#include <log4cxx/patternlayout.h>
#ifndef NDEBUG
#include <log4cxx/fileappender.h>
#include <log4cxx/filter/levelrangefilter.h>
#endif

#include <boost/program_options.hpp>
#include "opencv2/core.hpp"
#include "opencv2/imgproc.hpp"
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/log.h>
#include <libavutil/bprint.h>
}
#include "common.hpp"
#include "MediaReader.hpp"
#include "MediaWriter.hpp"
#include "Media.hpp"
#include "ThreadsafeFrame.hpp"
#include "ThreadManager.hpp"
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

    /// Callback function for libav log messages - used to direct them to the logger
    /// @see av_log_default_callback, https://github.com/FFmpeg/FFmpeg/blob/n4.1.3/libavutil/log.c
    /// @param[in] p ptr to a struct of which the first field is a pointer to an AVClass struct.
    /// @param[in] level message log level
    /// @param[in] fmr formatting to apply to the message
    /// @param[in] vaArgs other arguments passed to the logger
    void logLibAVMessages(void *p, int level, const char * fmt, va_list vaArgs);

    // Initialize logger
    static const char LOG_FORMAT_STRING[] = "%d %-5p [%-8X{threadname} %.8t] %c{1} - %m%n";

    log4cxx::LoggerPtr logger(log4cxx::Logger::getLogger("zoombrd"));
    log4cxx::LoggerPtr libavLogger(log4cxx::Logger::getLogger("zoombrd.libav"));
    std::mutex g_libavLogMutex;
} //::<anon>

/// Maintains communication between threads re: exceptions & program end
ThreadManager g_ThreadMan;

// The command we are trying to implement for one output stream is
// sudo avconv -f video4linux2 -r 5 -s hd1080 -i /dev/video0 \
//  -vf "format=yuv420p,framerate=5" -c:v libx264 -profile:v:0 high -level 3.0 -flags +cgop -g 1 \
//  -hls_time 0.1 -hls_allow_cache 0 -an -preset ultrafast /mnt/hls/stream.m3u8
// For further help, see https://libav.org/avconv.html
int main(int argc, const char * argv[])
{
    // Set up signal handler to end program
    std::signal( SIGINT, [](int){g_ThreadMan.end();} );
    std::cout << "press Ctrl+C to exit..." << std::endl;

    try{

    // Set up logger. See https://logging.apache.org/log4cxx/latest_stable/usage.html for more info,
    //see https://logging.apache.org/log4cxx/latest_stable/apidocs/classlog4cxx_1_1_pattern_layout.html for patterns
    log4cxx::MDC::put("threadname", "main");    //add name of thread
    log4cxx::LayoutPtr colorLayoutPtr(new log4cxx::ColorPatternLayout(LOG_FORMAT_STRING));
    log4cxx::AppenderPtr consoleAppPtr(new log4cxx::ConsoleAppender(colorLayoutPtr));
    log4cxx::BasicConfigurator::configure(consoleAppPtr);
#ifndef NDEBUG
    //Also add file appender - see https://stackoverflow.com/questions/13967382/how-to-set-log4cxx-properties-without-property-file
    log4cxx::LayoutPtr layoutPtr(new log4cxx::PatternLayout(LOG_FORMAT_STRING));
    log4cxx::AppenderPtr fileAppenderPtr(new log4cxx::FileAppender(layoutPtr, fs::path(argv[0]).filename().string()+".log", false));
    log4cxx::BasicConfigurator::configure(fileAppenderPtr);
#endif

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
#ifndef NDEBUG
    ("quiet,q", "suppresses messages that are not errors or warnings in debug builds")
#endif
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

    // Set log level.
#ifndef NDEBUG  //if debugging log it all.
    const log4cxx::LevelPtr pLogLevel = log4cxx::Level::getAll();
    assert(pLogLevel);
    const int avLogLevel = AV_LOG_VERBOSE;
    if (vm.count("quiet"))  //filter console logs to warnings & above, log all in the file log
    {
        log4cxx::filter::LevelRangeFilterPtr pFilterPtr(new log4cxx::filter::LevelRangeFilter());
        pFilterPtr->setLevelMin(log4cxx::Level::getWarn());
        pFilterPtr->setAcceptOnMatch(true);
        consoleAppPtr->addFilter(pFilterPtr);
    }
#else
    const log4cxx::LevelPtr pLogLevel = log4cxx::Level::getWarn();
    const int avLogLevel = AV_LOG_WARNING;
#endif
    log4cxx::Logger::getRootLogger()->setLevel(pLogLevel);
    logger->setLevel(pLogLevel);
    libavLogger->setLevel(pLogLevel);
    av_log_set_level(avLogLevel);
    av_log_set_callback(&logLibAVMessages);

    LOG4CXX_DEBUG(logger, "Program arguments:");
    for (auto it: vm)
    {
        LOG4CXX_DEBUG(logger, it.first << ": " << it.second.as<std::string>());
    }

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

    auto pInFrame = avtools::ThreadsafeFrame::Get(pVidStr->codecpar->width, pVidStr->codecpar->height, PIX_FMT, pVidStr->time_base);
    g_ThreadMan.addThread(threadedRead(pInFrame, rdr));

    // -----------
    // Open the outputs and start writer threads
    // -----------
    // Open the writer(s)
    std::vector<avtools::MediaWriter> writers;
    const fs::path output = vm["output"].as<std::string>();
    if ( strequals(output.extension().string(), ".json") )
    {
        if ( !fs::exists( output ) )
        {
            throw std::runtime_error("Could not find output configuration file " + output.string());
        }
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
            g_ThreadMan.addThread( threadedWrite(pInFrame, writer) );
        }
    }
    else  // Start the warper thread
    {
        LOG4CXX_DEBUG(logger, "Will apply perspective transform using transformation matrix: " << trfMatrix);
        assert( (AV_NOPTS_VALUE == (*pTrfFrame)->best_effort_timestamp) && (AV_NOPTS_VALUE == (*pTrfFrame)->pts) );
        g_ThreadMan.addThread( threadedWarp(pInFrame, pTrfFrame, trfMatrix) );
        // add writers to writer perspective transformed frames
        for (auto &writer : writers)
        {
            g_ThreadMan.addThread( threadedWrite(pTrfFrame, writer) );
        }
    }

    // -----------
    // Cleanup
    // -----------
    g_ThreadMan.join();
    if (g_ThreadMan.hasExceptions())
    {
        LOG4CXX_ERROR(logger, "Exiting with errors...");
        return EXIT_FAILURE;
    }
    LOG4CXX_DEBUG(logger, "Exiting successfully...");
    return EXIT_SUCCESS;

    }
    catch (std::exception& err)
    {
        LOG4CXX_ERROR(logger, "Exiting with errors...");
        g_ThreadMan.end();
        g_ThreadMan.addException(std::current_exception());
        return EXIT_FAILURE;
    }
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

    Options getOptsFromMapNode(const cv::FileNode& node)
    {
        if (!node.isMap())
        {
            throw std::runtime_error("Unable to parse options for url " + node.name());
        }
        Options opts;

        //Find muxer options
        auto n = node["muxer_options"];
        if (n.empty() || n.isNone())
        {
            LOG4CXX_INFO(logger, "No muxer options found for " << node.name());
        }
        else if (n.isMap())
        {
            //Read all options to dict
            readMapIntoDict(n, opts.muxerOpts);
        }
        else
        {
            throw std::runtime_error("Unable to parse muxer options for " + node.name());
        }

        //Find codec options
        n = node["codec_options"];
        if (n.empty() || n.isNone())
        {
            LOG4CXX_INFO(logger, "No codec options found for " << node.name());
        }
        else if (n.isMap())
        {
            //Read all options to dict
            readMapIntoDict(n, opts.codecOpts);
        }
        else
        {
            throw std::runtime_error("Unable to parse codec options for " + node.name());
        }
        return opts;
    }

    std::map<std::string, Options> getOptions(const std::string& configFile)
    {
        std::map<std::string, Options> opts;
        LOG4CXX_DEBUG(logger, "Reading configuration file " << configFile);
        cv::FileStorage cfs;
        cfs.open(configFile, cv::FileStorage::READ | cv::FileStorage::FORMAT_JSON);
        try
        {
            assert (cfs.isOpened());
            for (auto url: cfs.root())
            {
                auto opt = getOptsFromMapNode(url);
                if ( opts.find(url.name()) != opts.end() )
                {
                    throw std::runtime_error("Multiple options found for url: " + url.name());
                }
                opts[url.name()] = opt;
            }
            return opts;
        }
        catch (std::exception& err)
        {
            LOG4CXX_ERROR(logger, err.what());
            std::throw_with_nested( std::runtime_error("Unable to open configuration file " + configFile) );
        }
    }

    void setUpOutputLocations(const fs::path& url, bool doAssumeYes)
    {
        fs::path parent = url.has_parent_path() ? url.parent_path() : ".";
        if (!url.has_parent_path())
        {
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
        }
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
                std::for_each(filesToRemove.begin(), filesToRemove.end(), [](const fs::path& p){fs::remove(p);});
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

    int convertAVLevelToLog4CXXLevel(int level)
    {
        switch (level)
        {
            case AV_LOG_QUIET:
                return log4cxx::Level::OFF_INT ;
            case AV_LOG_DEBUG:
                return log4cxx::Level::DEBUG_INT;
            case AV_LOG_VERBOSE:
                return log4cxx::Level::TRACE_INT;
            case AV_LOG_INFO:
                return log4cxx::Level::INFO_INT;
            case AV_LOG_WARNING:
                return log4cxx::Level::WARN_INT;
            case AV_LOG_ERROR:
                return log4cxx::Level::ERROR_INT;
            case AV_LOG_FATAL:
            case AV_LOG_PANIC:
                return log4cxx::Level::FATAL_INT;
            default:
                return log4cxx::Level::ALL_INT;
        }
    }

    void logLibAVMessages(void *ptr, int level, const char * fmt, va_list vaArgs)
    {
        std::lock_guard<std::mutex> guard(g_libavLogMutex);
        const int av_log_level = av_log_get_level();
        if (level >= 0)
        {
            level &= 0xff;
        }

        if (level > av_log_level)
        {
            return;
        }

        static const int LINE_SZ = 1024;
        static std::string msg;
        static std::string prevMsg;
        prevMsg.reserve(LINE_SZ + 1);
        const int flags = av_log_get_flags();
        static bool doPrint = true;
        static int count = 0;

        AVClass* avc = ptr ? *(AVClass **) ptr : nullptr;

        if (doPrint && avc)
        {
            if (avc->parent_log_context_offset)
            {
                AVClass** parent = *(AVClass ***) (((uint8_t *) ptr) + avc->parent_log_context_offset);
                if (parent && *parent)
                {
                    msg += "|" + std::string((*parent)->item_name(parent));
                }
            }
            msg += "|" + std::string(avc->item_name(ptr)) + "|\t";
        }

        char errorMsg[LINE_SZ+1];
        int len = std::vsnprintf(errorMsg, LINE_SZ, fmt, vaArgs);
        if (len < 0)
        {
            LOG4CXX_WARN(libavLogger, "Error writing error message to buffer");
            return;
        }

        if(len > 0)
        {
            assert((len >= LINE_SZ) || (errorMsg[len] == '\0'));
            char lastc = (len <= LINE_SZ ? errorMsg[len-1] : 0);
            doPrint = ( (lastc == '\n') || (lastc == '\r') );
            msg += errorMsg;
        }

        if (doPrint)
        {
            msg.pop_back(); //remove trailing newline
            //sanitize error message:
            std::transform(msg.begin(), msg.end(), msg.begin(),
                           [](char c){
                               return ( (c < 0x08) || (c > 0x0D && c < 0x20) ? '?' : c);
                           });

            LOG4CXX_LOG(libavLogger, log4cxx::Level::toLevel(convertAVLevelToLog4CXXLevel(level)), msg);
            msg.clear();
            if ( (flags & AV_LOG_SKIP_REPEATED) && (len > 0) && (prevMsg == errorMsg) && ( errorMsg[len-1] != '\r') )
            {
                count++;
                LOG4CXX_LOG(libavLogger, log4cxx::Level::toLevel(convertAVLevelToLog4CXXLevel(level)),
                            "    Last message repeated " << count << " times\r");
                return;
            }
        }
        if (count > 0) {
            LOG4CXX_ERROR(libavLogger, "    Last message repeated " << count << " times\r");
            count = 0;
        }
        prevMsg = errorMsg;
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
                while (!g_ThreadMan.isEnded())
                {
                    const AVStream* pS = rdr.read(frame);
                    if (!pS)
                    {
                        g_ThreadMan.end(); //if the reader ends, end program
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
                    g_ThreadMan.addException(std::current_exception());
                }
            }
            g_ThreadMan.end();
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
                while (!g_ThreadMan.isEnded())
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
                        LOG4CXX_DEBUG(logger, "Waiting for incoming frame.");
                        inFrame.cv.wait(lock, [&inFrame, ts](){return g_ThreadMan.isEnded() || (inFrame->best_effort_timestamp > ts);});   //note that we assume the timebase of the incoming frames do not change
                        if (g_ThreadMan.isEnded())
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
                    g_ThreadMan.addException(std::current_exception());
                    g_ThreadMan.end();
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
                while (!g_ThreadMan.isEnded())
                {
                    auto ppInFrame = pInFrame.lock();
                    if (!ppInFrame)
                    {
                        if (g_ThreadMan.isEnded())
                        {
                            break;
                        }
                        throw std::runtime_error("Warper received null frame.");
                    }
                    const auto& inFrame = *ppInFrame;
                    {
                        auto rLock = inFrame.getReadLock();
                        inFrame.cv.wait(rLock, [&inFrame, ts](){return g_ThreadMan.isEnded() ||  (inFrame->best_effort_timestamp > ts);});    //wait until fresh frame is available
                        if (g_ThreadMan.isEnded())
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
                            LOG4CXX_DEBUG(logger, "Warper using transformation matrix: " << trfMatrix );
                            LOG4CXX_DEBUG(logger, "output image data is at: " << (void*) outImg.data );
                            cv::warpPerspective(inImg, outImg, trfMatrix, outImg.size(), cv::InterpolationFlags::INTER_LANCZOS4);
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
                    g_ThreadMan.addException(std::current_exception());
                    g_ThreadMan.end();
                }
            }
            LOG4CXX_DEBUG(logger, "Exiting warper thread: isEnded=" << std::boolalpha << g_ThreadMan.isEnded());
        });
    }

}   //::<anon>
