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
#include <stdexcept>
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
#include "opencv2/highgui.hpp"
#include "opencv2/aruco.hpp"
#include "common.hpp"

namespace
{
    namespace bpo = ::boost::program_options;

    /// Reads the board used for calibration from the marker file
    /// @param[in] markerFile file that containst the info re: the board & marker dictionary
    /// @return the grid board used for calibration
    const cv::Ptr<cv::aruco::GridBoard> getArucoBoard(const std::string& markerFile);

    /// Calculates the camera calibration matrix
    /// @param[out] cameraMatrix calculated camera matrix
    /// @param[out] set of distribution coefficients
    /// @param[in] brd grid of aruco markers used in calibration
    /// @return final re-projection error.
    [[maybe_unused]]
    double getCalibrationMatrix(cv::Mat& cameraMatrix, cv::Mat& distCoeffs, const cv::Ptr<cv::aruco::GridBoard> brd);

    // Initialize logger
    log4cxx::LoggerPtr logger(log4cxx::Logger::getLogger("calibration"));

    /// Saves calibration outputs
    /// @param[in] calibrationFile file to save to
    /// @param[in] dict dictionary of aruco markers that were used in calibration
    /// @param[in] cameraMatrix camera matrix
    /// @param[in] distCoeffs vector of distortion coefficients
    void saveCalibrationOutputs(const std::string& calibrationFile, const cv::Ptr<cv::aruco::Dictionary> dict, const cv::Mat& cameraMatrix, const cv::Mat& distCoeffs);

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
#ifndef NDEBUG
    //Also add file appender - see https://stackoverflow.com/questions/13967382/how-to-set-log4cxx-properties-without-property-file
    log4cxx::AppenderPtr fileAppenderPtr(new log4cxx::FileAppender(layoutPtr, fs::path(argv[0]).filename().string()+".log", false));
    log4cxx::BasicConfigurator::configure(fileAppenderPtr);
    // Set log level.
    const auto debugLevel = log4cxx::Level::getDebug();
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
    ("marker_file,m", bpo::value<std::string>()->default_value("marker_file.json"), "json file containing the marker dictionary to use for calibration")
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

    assert(vm.count("calibration_file") && vm.count("marker_file"));

    // Load the aruco dictionary to use for calibration
    const std::string markerFile = vm["marker_file"].as<std::string>();
    if (!fs::exists(markerFile))
    {
        throw std::runtime_error("A marker file could not be found, please check path or use create_markers to create one.");
    }
    auto gridBrd = getArucoBoard(markerFile);

    const std::string calibrationFile = vm["calibration_file"].as<std::string>();
    if ( fs::exists( calibrationFile ) )
    {
        bool doOverwrite = promptYesNo("Calibration file " + calibrationFile + " exists and will be overwritten. Proceed?");
        if (not doOverwrite)
        {
            return EXIT_SUCCESS;
        }
        LOG4CXX_DEBUG(logger, "Calibration file will be overwritten.");
    }

    // -----------
    // Start the calibration process
    // -----------
    cv::Mat cameraMatrix, distCoeffs;
    try
    {
        getCalibrationMatrix(cameraMatrix, distCoeffs, gridBrd);
    }
    catch (std::exception &err)
    {
        LOG4CXX_ERROR(logger, err.what());
    }

    //Save marker configuration and calibration matrix:
    saveCalibrationOutputs(calibrationFile, gridBrd->dictionary, cameraMatrix, distCoeffs);

    // Cleanup
    LOG4CXX_DEBUG(logger, "Exiting successfully...");
    return EXIT_SUCCESS;
}

namespace
{
    const cv::Ptr<cv::aruco::GridBoard> getArucoBoard(const std::string& markerFile)
    {
        cv::Mat markers;
        int markerSz;
        try
        {
            cv::FileStorage fs(markerFile, cv::FileStorage::READ);
            fs["markers"] >> markers;
            fs["marker_size"] >> markerSz;
        }
        catch (std::exception& err)
        {
            std::throw_with_nested( std::runtime_error("Unable to read board information from " + markerFile) );
        }
        auto pDict = cv::makePtr<cv::aruco::Dictionary>(markers, markerSz);
        return cv::aruco::GridBoard::create(MARKER_X, MARKER_Y, MARKER_LEN, MARKER_SEP, pDict);
    }

    double getCalibrationMatrix(cv::Mat& cameraMatrix, cv::Mat& distCoeffs, const cv::Ptr<cv::aruco::GridBoard> gridBrd)
    {
        // Open camera
        cv::VideoCapture camera;
        //TODO: Read the default size from the config.json file
        camera.set(cv::CAP_PROP_FRAME_WIDTH, 1024);
        camera.set(cv::CAP_PROP_FRAME_HEIGHT, 768);
        if ( !camera.open(0) )
        {
            throw std::runtime_error("Unable to open default webcam");
        }

        // Read frames
        cv::Mat inputImg(camera.get(cv::CAP_PROP_FRAME_HEIGHT), camera.get(cv::CAP_PROP_FRAME_WIDTH), CV_8UC3);

        double projError = std::numeric_limits<double>::infinity();
        std::vector<std::vector<cv::Point2f> > corners;
        std::vector<int> ids, counter;
        while ( (cv::waitKey(20) < 0) && (projError > 0.1) )
        {
            std::vector<std::vector<cv::Point2f> > frameCorners;
            std::vector<int> frameIds;
            // detect markers
            if (!camera.read(inputImg))
            {
                throw std::runtime_error("Unable to read frames from default camera");
            }
            cv::imshow("Camera image", inputImg);
            cv::aruco::detectMarkers(inputImg, gridBrd->dictionary, frameCorners, frameIds, cv::aruco::DetectorParameters::create());
            int n = (int) frameIds.size();
            assert(frameCorners.size() == n);
            if (0 == n)
            {
                LOG4CXX_DEBUG(logger, "Could not detected any markers.");
                continue;
            }
            corners.insert(corners.end(), frameCorners.begin(), frameCorners.end());
            ids.insert(ids.end(), frameIds.begin(), frameIds.end());
            counter.push_back(n);
            // calibrate based on detected markers
            projError = cv::aruco::calibrateCameraAruco(corners,ids, counter, gridBrd, inputImg.size(), cameraMatrix, distCoeffs);
            LOG4CXX_DEBUG(logger, "Detected " << n << " new markers. Projection error = " << projError);
        }
        if (counter.empty())
        {
            throw std::runtime_error("No markers found for calibration");
        }
        return projError;
    }

    void saveCalibrationOutputs(const std::string& calibrationFile, const cv::Ptr<cv::aruco::Dictionary> dict, const cv::Mat& cameraMatrix, const cv::Mat& distCoeffs)
    {
        cv::FileStorage fs(calibrationFile, cv::FileStorage::WRITE);
        fs << "markers" << dict->bytesList;
        fs << "marker_size" << dict->markerSize;
        fs << "camera_matrix" << cameraMatrix;
        fs << "distortion_coefficients" << distCoeffs;
#ifndef NDEBUG
        LOG4CXX_DEBUG(logger, fs.releaseAndGetString());
#else
        fs.release();
#endif
    }


}   //::<anon>
