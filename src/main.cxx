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
#include "version.h"
#include "MediaReader.hpp"
#include "MediaWriter.hpp"
#include "Media.hpp"
#include "log.hpp"
//#include "opencv2/core/core.hpp"
//#include "opencv2/imgproc/imgproc.hpp"
//#include "opencv2/objdetect/objdetect.hpp"
//#include "opencv2/highgui/highgui.hpp"
#include <opencv2/opencv.hpp>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/avutil.h>
}

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
    /// @param[out] outputOptions a structure containing the output stream options to use
    /// @throw std::runtime_error if there is an issue parsing the configuration file.
    void getOptions(const std::string& configFile, Options& inputOptions, Options& outputOptions);

    /// Wraps a cv::mat around libav frames. Note that the matrix is just wrapped around the
    /// existing data, so data is not cloned. Make sure that the matrix is done being used before reading new
    /// data into the frame.
    /// @param[in] a decoded video frame
    /// @return a cv::mat wrapper around the frame data
    cv::Mat getImage(const avtools::Frame& frame);

    /// Converts a frame size string in WxH format to a size
    /// @param[in] sizeStr string containing size info
    /// @return a cv::Size containing the width and height parsed from sizeStr
    /// @throw std::runtime_error if the frame size info could not be extracted
    cv::Size getDims(const std::string& sizeStr);
} //::<anon>

// The command we are trying to implement for one output stream is
// sudo avconv -f video4linux2 -r 5 -s hd1080 -i /dev/video0 \
//  -vf "format=yuv420p,framerate=5" -c:v libx264 -profile:v:0 high -level 3.0 -flags +cgop -g 1 \
//  -hls_time 0.1 -hls_allow_cache 0 -an -preset ultrafast /mnt/hls/stream.m3u8
// For further help, see https://libav.org/avconv.html
int main(int argc, const char * argv[])
{
    // Parse command line arguments & load the config file
    if (argc != 2)
    {
        LOG("Incorrect number of arguments, must supply a json configuration file as input.");
        LOG("Usage: ", argv[0], " <config.json>");
        return EXIT_FAILURE;
    }

    const std::string configFile = argv[1];
    Options inOpts, outOpts;
    getOptions(configFile, inOpts, outOpts);
    // -----------
    // Open the media reader
    // -----------
    avtools::MediaReader reader(inOpts.url, inOpts.streamOptions, avtools::MediaReader::InputType::CAPTURE_DEVICE);
    LOGD("Opened input stream.");
    avtools::Frame inputFrame(nullptr, AVMEDIA_TYPE_VIDEO);
    avtools::Frame outFrame;
    AVPixelFormat inPixFmt = (AVPixelFormat) reader.getVideoStream()->codecpar->format;
    std::unique_ptr<avtools::ImageConversionContext> imgFmtConvertor = nullptr;
    if (inPixFmt != AV_PIX_FMT_BGR24)
    {
        assert (sws_isSupportedOutput(AV_PIX_FMT_BGR24));
        avtools::CodecParameters outParam(reader.getVideoStream()->codecpar);
        outParam->format = AV_PIX_FMT_BGR24;    //corresponds to the BGR24 format used by OpenCV
        LOGD("Output parameters: ", *outParam.get());
        outFrame = avtools::Frame(outParam);

        if (!sws_isSupportedInput(inPixFmt))
        {
            throw MediaError("Unsupported input pixel format " + std::to_string(inPixFmt));
        }
        imgFmtConvertor = std::make_unique<avtools::ImageConversionContext>(*reader.getVideoStream()->codecpar, *outParam.get());
    }
    cv::namedWindow("Converted image");
    while (cv::waitKey(1) < 0)
    {
        const AVStream* pS = reader.read(inputFrame);
        if (!pS)
        {
            LOGD("End of stream reached.");
            break;
        }
        LOGD("Video Frame, time: ", avtools::calculateTime(inputFrame->best_effort_timestamp - pS->start_time, pS->time_base));
        LOGD("Input frame info:\n", inputFrame.info(1));
        cv::Mat img;
        if (imgFmtConvertor)    //image format needs to be converted to BGR24
        {
            LOGD("Output frame info:\n", outFrame.info(1));
            av_frame_copy_props(outFrame.get(), inputFrame.get());
            imgFmtConvertor->convert(inputFrame, outFrame);
            img = getImage(outFrame);
        }
        else
        {
            img = getImage(inputFrame);
        }
        LOGD("Image info:", img.size);
        cv::imshow("Converted image", img);
    }

    // -----------
    // Get calibration matrix
    // -----------
    LOGX;
    
    // -----------
    // open transcoder & processor
    // -----------
    LOGX;
    
    // -----------
    // Open the output stream writers - one for lo-res, one for hi-res
    // -----------
    LOGX;
    
    // -----------
    // Start the read/write loop
    // -----------
    LOGX;
    
    // -----------
    // Cleanup
    // -----------
    LOGX;
    LOGD("Exiting successfully...");
    return EXIT_SUCCESS;

//        //
//        avtools::MultiMediaWriter mmWriter(outfile);
//        if ( !mmWriter.isOpen() )
//        {
//            throw MMError("Transcoder: Unable to open " + outfile + " for writing");
//        }
//        // Add output audio stream
//        int outAuStr=-1, outVidStr=-1;
//        auto hAuOutCodecPar = getOutputAudioCodecParameters(*pAuStr->codecpar);
//        assert(hAuOutCodecPar && (hAuOutCodecPar->codec_type == AVMEDIA_TYPE_AUDIO));
//        assert(hAuOutCodecPar->codec_id == pAuStr->codecpar->codec_id);
//        const AVStream* pStrOut = mmWriter.addStream(*hAuOutCodecPar, {1, hAuOutCodecPar->sample_rate});
//        if (!pStrOut)
//        {
//            throw MMError("Transcoder: Unable to add audio stream to output file.");
//        }
//        assert(pStrOut->codecpar->codec_type == AVMEDIA_TYPE_AUDIO);
//        outAuStr = pStrOut->index;
//        //Open audio transcoder
//        auto hAudioTranscoder = std::make_unique<AudioTranscoder>(*pAuStr->codecpar, *pStrOut->codecpar);
//        if (!hAudioTranscoder)
//        {
//            throw MMError("Transcoder: Unable to open audio transcoder.");
//        }
//        assert ( (inAuStr >= 0) && (outAuStr >= 0) );
//        LOGD("Transcoder: added output audio stream: ", inAuStr, "->", outAuStr);
//
//        // Add output video stream
//        const AVStream* pVidStr = mmReader.getFirstVideoStream();
//        int inVidStr = -1;
//        if (pVidStr)
//        {
//            inVidStr = pVidStr->index;
//            LOGD("Transcoder: found input video stream: ", inVidStr);
//            const AVStream* pStrOut = mmWriter.addStream(*pVidStr->codecpar, pVidStr->time_base);
//            if (!pStrOut)
//            {
//                throw MMError("Transcoder: Unable to add video stream to output file.");
//            }
//            outVidStr = pStrOut->index;
//            assert ( (inVidStr >= 0) && (outVidStr >= 0) );
//            LOGD("Transcoder: added output audio stream: ", inVidStr, "->", outVidStr);
//        }
//
//        // Start reading and writing frames
//        AVFrame const * pF = nullptr;
//        const AVStream* pS = nullptr;
//        while ((pS = mmReader.read(pF)))
//        {
//            assert(pF);
//            const int stream = pS->index;
//            if (stream == inAuStr)  //audio frame -> transcode & write
//            {
//                assert(hAudioTranscoder);
//                hAudioTranscoder->push(pF);
//                while (const AVFrame* pFTranscoded = hAudioTranscoder->pop())
//                {
//                    mmWriter.write(pFTranscoded, outAuStr);
//                }
//            }
//            else if (stream == inVidStr) //video frame -> just write
//            {
//                mmWriter.write(pF, outVidStr);
//            }
//        }
//        // EOF. Cleanup & write buffered data
//        // Flush buffered data in transcoder
//        hAudioTranscoder->push(nullptr);
//        while (const AVFrame* pF = hAudioTranscoder->pop())
//        {
//            mmWriter.write(pF, outAuStr);
//        }
//        hAudioTranscoder.reset(nullptr);
//        //signal eof to writer
//        mmWriter.write(nullptr, -1);
//    }
//    catch(std::exception& e)
//    {
//        logging::print_exception(e);
//        return EXIT_FAILURE;
//    }
//    LOGD("Exiting successfully...");
//    return EXIT_SUCCESS;
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
                LOGW("Unknown options for ", n.name(), " found in config file for ", node.name());
            }
        }
        if (!isUrlFound)
        {
            throw std::runtime_error("URL not found in config file for " + node.name());
        }
        if (!isStreamOptsFound) //note that this can be skipped for stream defaults
        {
            LOGW("Stream options not found in config file for ", node.name());
        }
        if (!isCodecOptsFound) //note that this can be skipped for codec defaults
        {
            LOGW("Codec options not found in config file for ", node.name());
        }
    }

    void getOptions(const std::string& configFile, Options& inOpts, Options& outOpts)
    {
        cv::FileStorage fs(configFile, cv::FileStorage::READ);
        const auto rootNode = fs.root();
        // Read input options
        bool isInputOptsFound = false, isOutputOptsFound = false;
        for (auto it = rootNode.begin(); it != rootNode.end(); ++it)
        {
            cv::FileNode node = *it;
            if (node.name() == INPUT_DRIVER) //Found input options for this architecture
            {
                LOGD("Found input options for ", INPUT_DRIVER);
                if (isInputOptsFound)
                {
                    throw std::runtime_error("Found multiple options for " + INPUT_DRIVER);
                }
                readFileNodeIntoOpts(node, inOpts);
                isInputOptsFound = true;
            }
            else if (node.name() == "output")  //Found the output options
            {
                LOGD("Found output options.");
                if (isOutputOptsFound)
                {
                    throw std::runtime_error("Found multiple output options");
                }
                readFileNodeIntoOpts(node, outOpts);
                isOutputOptsFound = true;
            }
        }
        if (!isInputOptsFound)
        {
            throw std::runtime_error("Unable to find input options for " + INPUT_DRIVER + " in " + configFile);
        }
        if (!isOutputOptsFound)
        {
            throw std::runtime_error("Unable to find output options in " + configFile);
        }
        fs.release();
    }

    cv::Mat getImage(const avtools::Frame& frame)
    {
        cv::Mat mat(frame->height, frame->width, CV_8UC3, frame->data[0], frame->linesize[0]);
        return mat;
    }

    cv::Size getDims(const std::string& sizeStr)
    {
        int sep = 0;
        bool isFound = false;
        for (auto it = sizeStr.begin(); it != sizeStr.end(); ++ it)
        {
            if ( ::isdigit(*it) )
            {
                if (!isFound)
                {
                    ++sep;
                }
            }
            else if (!isFound && ((*it == 'x') || (*it == 'X')) )
            {
                isFound = true;
            }
            else
            {
                throw std::runtime_error("Unable to parse " + sizeStr + " to extract size info.");
            }
        }
        return cv::Size( std::stoi(sizeStr.substr(0, sep)), std::stoi(sizeStr.substr(sep+1)) );
    }
}   //::<anon>
