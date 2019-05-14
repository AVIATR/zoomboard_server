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
    static const std::string INPUT_DRIVER = "avfoundation";
    // Initialize logger
    log4cxx::LoggerPtr logger(log4cxx::Logger::getLogger("zoombrd"));
    static const AVPixelFormat PIX_FMT = AVPixelFormat::AV_PIX_FMT_BGR24;

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
        return ( stream << "url:" << opts.url
                << "\ncodec options:\n" << opts.codecOpts.as_string() << "\n"
                << "\nmuxer options:\n" << opts.muxerOpts.as_string() );
    }


    /// Parses a json file to retrieve the output configuration to use
    /// @param[in] configFile name of configuration file to read
    /// @return output options to use for the writer
    /// @throw std::runtime_error if there is an issue parsing the configuration file.
    Options getOptions(const std::string& configFile, const std::string& type);

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

    class ReadySignal
    {
    protected:
        mutable std::mutex mutex_;
        std::atomic_bool isReady_;
    public:
        /// Ctor
        ReadySignal();
        /// Dtor
        virtual ~ReadySignal();
        /// Used to signal that the signal is ready
        inline void ready();
        /// @return true if the signal is ready
        inline bool isReady() const;
    };  //::<anon>::ReadySignal

    /// Function that starts a stream reader that reads from a stream int to a threaded frame
    /// @param[in,out] pFrame threadsafe frame to write to
    /// @param[in] rdr an opened media reader
    /// @return a new thread that reads frames from the input stream and updates the threaded frame
    std::thread threadedRead(std::weak_ptr<avtools::ThreadsafeFrame> pFrame, avtools::MediaReader& rdr);

    /// Function that starts a stream writer that writes to a stream from a threaded frame
    /// @param[in] pFrame threadsafe frame to read from
    /// @param[in] writer media writer instance
    /// @param[in] timebase time base of the incoming frames
    /// @return a new thread that reads frames from the input frame and writes to an output file
    std::thread threadedWrite(std::weak_ptr<const avtools::ThreadsafeFrame> pFrame, avtools::MediaWriter& writer, avtools::TimeBaseType timebase);

    /// Maintains communication between threads re: exceptions & program end
    static ProgramStatus g_Status;

    /// Used to signal that the reader thread has frames ready, so writed can wait
//    static ReadySignal g_ReaderReady;

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
    const auto debugLevel = log4cxx::Level::getInfo();
#endif
    log4cxx::Logger::getRootLogger()->setLevel(debugLevel);
    logger->setLevel(debugLevel);
    LOG4CXX_DEBUG(logger,"Created ConsoleAppender appender");

    const std::string USAGE = "Usage: " + std::string(argv[0]) + " <config_file.json>";
    // Parse command line arguments & load the config file
    if (argc != 2)
    {
        LOG4CXX_FATAL(logger, "Incorrect number of arguments." << USAGE);
        return EXIT_FAILURE;
    }
    const std::string configFile = argv[1];
    //Until we convert to C++17, we need to use boost::filesystem to check for file. Afterwards, we can use std::filesystem
    if ( !boost::filesystem::exists( configFile ) )
    {
        LOG4CXX_FATAL(logger, "Configuration file " << configFile << " does not exist." << USAGE);
        return EXIT_FAILURE;
    }

    Options inOpts = getOptions(configFile, "input");
    Options outOpts = getOptions(configFile, "output");
    // -----------
    // Open the reader & writer, and start the threads to read/write frames
    // -----------
    LOG4CXX_DEBUG(logger, "Input options are:\n" << inOpts << "\nOpening reader.");
    avtools::MediaReader rdr(inOpts.url, inOpts.muxerOpts);
    const AVStream* pVidStr = rdr.getVideoStream();
    LOG4CXX_DEBUG(logger, "Input stream info:\n" << avtools::getStreamInfo(pVidStr) );
    auto pFrame = avtools::ThreadsafeFrame::Get(pVidStr->codecpar->width, pVidStr->codecpar->height, PIX_FMT);

    LOG4CXX_DEBUG(logger, "Output options are:\n" << outOpts << "\nOpening writer.");

    //add output time base
    outOpts.codecOpts.add("time_base", std::to_string( pVidStr->time_base ));
    avtools::MediaWriter writer(outOpts.url, outOpts.codecOpts, outOpts.muxerOpts);
    const AVStream* pOutStr = writer.getStream();

    LOG4CXX_DEBUG(logger, "Output stream info:\n" << avtools::getStreamInfo(pOutStr));
    // Filter graph to convert input frames to output format
//    avtools::FilterGraph graph(pVidStr->codecpar->width, pVidStr->codecpar->height, PIX_FMT,
//                               pVidStr->sample_aspect_ratio, pVidStr->time_base, pVidStr->avg_frame_rate,
//                               pOutStr->codecpar->width, pOutStr->codecpar->height, (AVPixelFormat) pOutStr->codecpar->format,
//                               pOutStr->sample_aspect_ratio, pOutStr->time_base, pOutStr->avg_frame_rate);

    std::thread readerThread = threadedRead(pFrame, rdr);
    std::thread writerThread = threadedWrite(pFrame, writer, pVidStr->time_base);

    std::cout << "press Ctrl+C to exit...";

    // -----------
    // Cleanup
    // -----------
    readerThread.join();    //wait for reader thread to finish
    writerThread.join();    //wait for writer to finish
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
    std::string toLower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
        return s;
    }

    void getOptsFromTree(const boost::property_tree::ptree& tree, Options& opts)
    {
        for (auto& branch : tree)
        {
            if ( toLower(branch.first) == "url" )
            {
                opts.url = branch.second.get_value<std::string>();
            }
            if ( toLower(branch.first) == "codec_options" )
            {
                for (auto &leaf : branch.second)
                {
                    opts.codecOpts.add(leaf.first, leaf.second.get_value<std::string>());
                }
            }
            if ( toLower(branch.first) == "muxer_options" )
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
            if (toLower(subtree.first) == toLower(type))
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

    // -------------------------
    // ReadySignal definitions
    // -------------------------
    ReadySignal::ReadySignal():
    mutex_(),
    isReady_(false)
    {}

    ReadySignal::~ReadySignal() = default;

    void ReadySignal::ready()
    {
        isReady_.store(true);
    }

    bool ReadySignal::isReady() const
    {
        return isReady_.load();
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

    std::thread threadedWrite(std::weak_ptr<const avtools::ThreadsafeFrame> pFrame, avtools::MediaWriter& writer, avtools::TimeBaseType timebase)
    {
        return std::thread([pFrame, &writer, timebase](){
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
                        writer.write(nullptr, timebase);
                        break;
                    }
                    const auto& inFrame = *ppFrame;
                    {
                        auto lock = inFrame.getReadLock();
                        inFrame.cv.wait(lock, [&inFrame, ts](){return g_Status.isEnded() || (inFrame->best_effort_timestamp > ts);});
                        if (g_Status.isEnded())
                        {
                            writer.write(nullptr, timebase);
                            break;
                        }
                        assert(inFrame->best_effort_timestamp > ts);
                        ts = inFrame->best_effort_timestamp;
                        LOG4CXX_DEBUG(logger, "Writer received frame:\n" << inFrame.info(1));
                        //Push frame to filtergraph
                        writer.write(inFrame, timebase);
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

}   //::<anon>
