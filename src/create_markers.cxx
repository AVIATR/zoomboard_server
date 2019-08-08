//
//  calibrate.cxx
//  Uses aruco markers to calibrate
//  Also see https://docs.opencv.org/4.1.0/d9/d6a/group__aruco.html#gab9159aa69250d8d3642593e508cb6baa and
//  https://docs.opencv.org/4.1.0/d9/d0c/group__calib3d.html#ga687a1ab946686f0d85ae0363b5af1d7b
//
//  Created by Ender Tekin on 7/23/19.
//

#include <cassert>
#include <string>
#include <vector>
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
#include "opencv2/highgui.hpp"
#include "opencv2/aruco.hpp"
#include "common.hpp"

namespace
{
    namespace bpo = ::boost::program_options;

    // Initialize logger
    log4cxx::LoggerPtr logger(log4cxx::Logger::getLogger("aruco"));

    /// Saves calibration outputs
    /// @param[in] calibrationFile file to save to
    /// @param[in] dict dictionary of aruco markers that were used in calibration
    void saveMarkerConfiguration(const std::string& calibrationFile, const cv::Ptr<cv::aruco::Dictionary> dict);

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
    // Set log level.
#ifndef NDEBUG
    const auto debugLevel = log4cxx::Level::getDebug();
    //Also add file appender - see https://stackoverflow.com/questions/13967382/how-to-set-log4cxx-properties-without-property-file
    log4cxx::AppenderPtr fileAppenderPtr(new log4cxx::FileAppender(layoutPtr, fs::path(argv[0]).filename().string()+".log", false));
    log4cxx::BasicConfigurator::configure(fileAppenderPtr);
#else
    const auto debugLevel = log4cxx::Level::getWarn();
#endif
    log4cxx::Logger::getRootLogger()->setLevel(debugLevel);
    logger->setLevel(debugLevel);

    //Parse command line options
    static const std::string PROGRAM_NAME = fs::path(argv[0]).filename().string() + " v" + std::to_string(ZOOMBOARD_SERVER_VERSION_MAJOR) + "." + std::to_string(ZOOMBOARD_SERVER_VERSION_MINOR);

    bpo::options_description programDesc(PROGRAM_NAME + " options");
    bpo::positional_options_description posDesc;
    bpo::variables_map vm;
    posDesc.add("calibration_file", -1);
    programDesc.add_options()
    ("help,h", "produce help message")
    ("version,v", "program version")
    ("marker_file", bpo::value<std::string>()->default_value("markers.json"), "path of file to write created marker info to")
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

    assert(vm.count("marker_file"));

    const std::string markerFile = vm["marker_file"].as<std::string>();
    //Until we convert to C++17, we need to use boost::filesystem to check for file. Afterwards, we can use std::filesystem
    if ( fs::exists( markerFile ) )
    {
        bool doOverwrite = promptYesNo("Marker file " + markerFile + " exists and will be overwritten. Proceed?");
        if (not doOverwrite)
        {
            return EXIT_SUCCESS;
        }
        LOG4CXX_DEBUG(logger, "Calibration file will be overwritten.");
    }

    //Create 2x2 aruco board image if it does not exist
    const fs::path arucoFile = "arucobrd_2x2.png";
    if ( fs::exists(arucoFile))
    {
        LOG4CXX_DEBUG(logger, "2x2 Aruco board image file " << arucoFile << " found and will be overwritten.");
    }
    auto dict = cv::aruco::generateCustomDictionary(4, MARKER_SIZE);
    auto gridBrd = cv::aruco::GridBoard::create(MARKER_X, MARKER_Y, MARKER_LEN, MARKER_SEP, dict);
    cv::Mat brdImg;
    cv::aruco::drawPlanarBoard(gridBrd, cv::Size(1024,1024), brdImg, 48);
    cv::imwrite(arucoFile.string(), brdImg);
    LOG4CXX_DEBUG(logger, "Saved 2x2 Aruco board image file as " << arucoFile);

    //Save marker configuration and calibration matrix:
    saveMarkerConfiguration(markerFile, dict);

    // Cleanup
    LOG4CXX_DEBUG(logger, "Exiting successfully...");
    return EXIT_SUCCESS;
}

namespace
{
    void saveMarkerConfiguration(const std::string& calibrationFile, const cv::Ptr<cv::aruco::Dictionary> dict)
    {
        cv::FileStorage fs(calibrationFile, cv::FileStorage::WRITE);
        fs << "markers" << dict->bytesList;
        fs << "marker_size" << dict->markerSize;
#ifndef NDEBUG
        LOG4CXX_DEBUG(logger, fs.releaseAndGetString());
#else
        fs.release();
#endif
    }

}   //::<anon>
