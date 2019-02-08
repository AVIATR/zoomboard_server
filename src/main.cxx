//
//  main.cpp
//  transcoder
//
//  Created by Ender Tekin on 10/8/14.
//  Copyright (c) 2014 Smith-Kettlewell. All rights reserved.
//

#include <cassert>
#include <string>
#include <vector>
#include "version.h"
#include "StreamReader.hpp"
#include "StreamWriter.hpp"
#include "Stream.hpp"
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

using avtools::StreamError;
namespace
{
    /// @class A structure containing the pertinent ffmpeg options
    struct Options
    {
        std::string inputDriver;            ///< Name of input driver, e.g., v4l2 for linux, avfoundation for macos etc.
        std::string inputDevice;            ///< Input device to use, e.g., /dev/video0 for linux, 0 for macos etc.
        std::string outputUrl;              ///< Url to publish the output stream
        int frameRate;                      ///< Frame rate to use.
        cv::Size    resolution;             ///< Resolution of the input
    };
    
    /// Parses a json file to retrieve the configuration to use
    /// @param[in] configFile name of configuration file to read
    /// @return a structure containing the ffmpeg options to use
    /// @throw std::runtime_error if there is an issue parsing the configuration file.
    Options getOptions(const std::string& configFile);
    
    /// Gets the requested input parameters from the options
    /// @param[in] opts options retrieved from the configuration file
    /// @return codec parameters to use when opening the input stream
    avtools::Handle<AVDictionary> getInputParams(const Options& opts);

    /// Gets the requested output parameters from the options
    /// @param[in] opts options retrieved from the configuration file
    /// @return codec parameters to use when opening the output stream
    AVCodecParameters getOutputParams(const Options& opts);

} //::<anon>

// The command we are trying to implement for one output stream is
// sudo avconv -f video4linux2 -r 5 -s hd1080 -i /dev/video0 \
//  -vf "format=yuv420p,framerate=5" -c:v libx264 -profile:v:0 high -level 3.0 -flags +cgop -g 1 \
//  -hls_time 0.1 -hls_allow_cache 0 -an -preset ultrafast /mnt/hls/stream.m3u8
int main(int argc, const char * argv[])
{
    if (argc != 2)
    {
        LOG("Incorrect number of arguments, must supply a json configuration file as input.");
        LOG("Usage: ", argv[0], " <config.json>");
        return EXIT_FAILURE;
    }

    const std::string configFile = argv[1];
    const Options opts = getOptions(configFile);
    const auto inParams = getInputParams(opts);
    const AVCodecParameters outParams = getOutputParams(opts);
    // -----------
    // Open the media reader
    // -----------
    avtools::StreamReader reader(opts.inputDriver, opts.inputDevice, *inParams);
    const AVStream* pStr = reader.getVideoStream();
    if (!pStr)
    {
        throw StreamError("Could not find any video streams that can be processed.");
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
    Options getOptions(const std::string& configFile)
    {
        cv::FileStorage fs(configFile, cv::FileStorage::READ);
        Options opts;
        opts.inputDevice = (std::string) fs["device"];
        opts.inputDriver = (std::string) fs["driver"];
        opts.outputUrl = (std::string) fs["url"];
        opts.frameRate = (int) fs["fps"];
        std::string resolution = (std::string) fs["resolution"];
        if (0 == resolution.compare("1080p"))
        {
            opts.resolution = cv::Size(1920, 1080);
        }
        else if (0 == resolution.compare("720p"))
        {
            opts.resolution = cv::Size(1280, 720);
        }
        else if (0 == resolution.compare("VGA"))
        {
            opts.resolution = cv::Size(640, 480);
        }
        else if (0 == resolution.compare("QVGA"))
        {
            opts.resolution = cv::Size(320, 240);
        }
        else
        {
            throw std::runtime_error("Unknown resolution " + resolution + " found in configuration file.");
        }
        fs.release();
        return opts;
    }
    
    avtools::Handle<AVDictionary> getInputParams(const Options& opts)
    {
        AVDictionary* inOpts = nullptr;
        int ret = av_dict_set(&inOpts, "driver", opts.inputDriver.c_str(), 0);
        if (ret < 0)
        {
            throw StreamError("Unable to set driver in input dictionary", ret);
        }
        ret = av_dict_set(&inOpts, "device", opts.inputDevice.c_str(), 0);
        if (ret < 0)
        {
            throw StreamError("Unable to set device in input dictionary", ret);
        }
        ret = av_dict_set_int(&inOpts, "framerate", opts.frameRate, 0);
        if (ret < 0)
        {
            throw StreamError("Unable to set framerate in input dictionary", ret);
        }
        ret = av_dict_set_int(&inOpts, "width", opts.resolution.width, 0);
        if (ret < 0)
        {
            throw StreamError("Unable to set width in input dictionary", ret);
        }
        ret = av_dict_set_int(&inOpts, "height", opts.resolution.height, 0);
        if (ret < 0)
        {
            throw StreamError("Unable to set height in input dictionary", ret);
        }
        return avtools::Handle<AVDictionary>(inOpts, [](AVDictionary* p){av_dict_free(&p); });
    }
}   //::<anon>
