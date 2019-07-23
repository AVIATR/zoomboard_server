//
//  calibrate.cxx
//
//  Created by Ender Tekin on 7/23/19.
//

#include <cassert>
#include <string>
//#include <algorithm>
#include <vector>
//#include <functional>
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
#include "opencv2/aruco.hpp"
#include "version.h"

namespace
{
    namespace bfs = ::boost::filesystem;
    namespace bpo = ::boost::program_options;

    /// Asks the user to choose the corners of the board and undoes the
    /// perspective transform. This can then be used with
    /// cv::warpPerspective to correct the perspective of the video.
    /// also @see https://docs.opencv.org/3.1.0/da/d54/group__imgproc__transform.html
    /// @param[in] pFrame input frame that will be updated by the reader thread
    /// @return a perspective transformation matrix
    /// TODO: Should also return the roi so we send a smaller framesize if need be (no need to send extra data)
    cv::Mat_<double> getCalibrationMatrix(const cv::Mat& img);

    // Initialize logger
    log4cxx::LoggerPtr logger(log4cxx::Logger::getLogger("calibration"));

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
    log4cxx::LayoutPtr layoutPtr(new log4cxx::PatternLayout("%d %-5p %c{1} - %m%n"));
    log4cxx::AppenderPtr consoleAppPtr(new log4cxx::ConsoleAppender(layoutPtr));
    log4cxx::BasicConfigurator::configure(consoleAppPtr);
    //Also add file appender - see https://stackoverflow.com/questions/13967382/how-to-set-log4cxx-properties-without-property-file
    log4cxx::AppenderPtr fileAppenderPtr(new log4cxx::FileAppender(layoutPtr, "calibration_log.txt", false));
    log4cxx::BasicConfigurator::configure(fileAppenderPtr);

    // Set log level.
#ifndef NDEBUG
    const auto debugLevel = log4cxx::Level::getDebug();
#else
    const auto debugLevel = log4cxx::Level::getWarn();
#endif
    log4cxx::Logger::getRootLogger()->setLevel(debugLevel);
    logger->setLevel(debugLevel);

    //Parse command line options
    static const std::string PROGRAM_NAME = bfs::path(argv[0]).filename().string() + " v" + std::to_string(ZOOMBOARD_SERVER_VERSION_MAJOR) + "." + std::to_string(ZOOMBOARD_SERVER_VERSION_MINOR);

    bpo::options_description programDesc(PROGRAM_NAME + " options");
    bpo::positional_options_description posDesc;
    bpo::variables_map vm;
    posDesc.add("calibration_file", -1);
    programDesc.add_options()
    ("help,h", "produce help message")
    ("version,v", "program version")
    ("calibration_file", bpo::value<std::string>()->default_value("calibration.json"), "path of configuration file to write calibration results to")
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

    assert(vm.count("calibration_file"));

    const std::string calibrationFile = vm["calibration_file"].as<std::string>();
    //Until we convert to C++17, we need to use boost::filesystem to check for file. Afterwards, we can use std::filesystem
    if ( bfs::exists( calibrationFile ) )
    {
        char answer;
        do
        {
            std::cout << "Calibration file " << calibrationFile << " exists and will be overwritten. Proceed? (Y/N)\n";
            std::cin >> answer;
        }
        while( !std::cin.fail() && (answer != 'y') && (answer != 'Y')&& (answer != 'n') && (answer != 'N') );

        if ((answer == 'n') || (answer == 'N'))
        {
            return EXIT_SUCCESS;
        }
    }

    // -----------
    // Start the calibration process
    // -----------
    // Open the writer(s)

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

    std::map<std::string, Options> getOptions(const bfs::path& configFile)
    {
        namespace bpt = ::boost::property_tree;
        bpt::ptree tree;
        bpt::read_json(configFile.string(), tree);
        std::map<std::string, Options> opts;
        for (auto& subtree: tree)
        {
            opts.emplace(subtree.first, getOptsFromTree(subtree.second));
        }
        return opts;
    }


    /// Converts a cv::point to a string representation
    /// @param[in] pt the point to represent
    /// @return a string representation of the point in the form (pt.x, pt.y)
    template<typename T>
    std::string to_string(const cv::Point_<T>& pt)
    {
        return "(" + std::to_string(pt.x) + "," + std::to_string(pt.y) + ")";
    }

}   //::<anon>
