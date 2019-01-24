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
        std::string inputDevice;            ///< Name of input device, e.g., v4l2 for linux, avfoundation for macos etc.
        std::string inputSource;            ///< Input source to use, e.g., /dev/video0 for linux, 0 for macos etc.
        std::string url;                    ///< Url to publish the output stream
        avtools::TimeBaseType frameRate;    ///< Frame rate to use.
        cv::Size    resolution;             ///< Resolution of the input
    };
    
    /// Parses a json file to retrieve the configuration to use
    /// @param[in] configFile name of configuration file to read
    /// @return a structure containing the ffmpeg options to use
    /// @throw std::runtime_error if there is an issue parsing the configuration file.
    Options getOptions(const std::string& configFile);
    
} //::<anon>

int main(int argc, const char * argv[])
{
    if (argc != 2)
    {
        LOG("Incorrect number of arguments, must supply a json configuration file as input.");
        LOG("Usage: ", argv[0], " <config.json>");
        return EXIT_FAILURE;
    }
    try
    {
        const std::string configFile = argv[1];
        const Options opts = getOptions(configFile);
        // Open the media reader
        avtools::MultiMediaReader mmReader(infile);
        
        //We will only process the first audio and video streams
        const AVStream* pAuStr = mmReader.getFirstAudioStream();
        if (!pAuStr)
        {
            throw MMError("Transcoder: Could not find any audio streams that can be processed.");
        }
        const int inAuStr = pAuStr->index;
        LOGD("Transcoder: found input audio stream: ", inAuStr);

        // -----------
        // Transcode
        // -------------
        avtools::MultiMediaWriter mmWriter(outfile);
        if ( !mmWriter.isOpen() )
        {
            throw MMError("Transcoder: Unable to open " + outfile + " for writing");
        }
        // Add output audio stream
        int outAuStr=-1, outVidStr=-1;
        auto hAuOutCodecPar = getOutputAudioCodecParameters(*pAuStr->codecpar);
        assert(hAuOutCodecPar && (hAuOutCodecPar->codec_type == AVMEDIA_TYPE_AUDIO));
        assert(hAuOutCodecPar->codec_id == pAuStr->codecpar->codec_id);
        const AVStream* pStrOut = mmWriter.addStream(*hAuOutCodecPar, {1, hAuOutCodecPar->sample_rate});
        if (!pStrOut)
        {
            throw MMError("Transcoder: Unable to add audio stream to output file.");
        }
        assert(pStrOut->codecpar->codec_type == AVMEDIA_TYPE_AUDIO);
        outAuStr = pStrOut->index;
        //Open audio transcoder
        auto hAudioTranscoder = std::make_unique<AudioTranscoder>(*pAuStr->codecpar, *pStrOut->codecpar);
        if (!hAudioTranscoder)
        {
            throw MMError("Transcoder: Unable to open audio transcoder.");
        }
        assert ( (inAuStr >= 0) && (outAuStr >= 0) );
        LOGD("Transcoder: added output audio stream: ", inAuStr, "->", outAuStr);
        
        // Add output video stream
        const AVStream* pVidStr = mmReader.getFirstVideoStream();
        int inVidStr = -1;
        if (pVidStr)
        {
            inVidStr = pVidStr->index;
            LOGD("Transcoder: found input video stream: ", inVidStr);
            const AVStream* pStrOut = mmWriter.addStream(*pVidStr->codecpar, pVidStr->time_base);
            if (!pStrOut)
            {
                throw MMError("Transcoder: Unable to add video stream to output file.");
            }
            outVidStr = pStrOut->index;
            assert ( (inVidStr >= 0) && (outVidStr >= 0) );
            LOGD("Transcoder: added output audio stream: ", inVidStr, "->", outVidStr);
        }

        // Start reading and writing frames
        AVFrame const * pF = nullptr;
        const AVStream* pS = nullptr;
        while ((pS = mmReader.read(pF)))
        {
            assert(pF);
            const int stream = pS->index;
            if (stream == inAuStr)  //audio frame -> transcode & write
            {
                assert(hAudioTranscoder);
                hAudioTranscoder->push(pF);
                while (const AVFrame* pFTranscoded = hAudioTranscoder->pop())
                {
                    mmWriter.write(pFTranscoded, outAuStr);
                }
            }
            else if (stream == inVidStr) //video frame -> just write
            {
                mmWriter.write(pF, outVidStr);
            }
        }
        // EOF. Cleanup & write buffered data
        // Flush buffered data in transcoder
        hAudioTranscoder->push(nullptr);
        while (const AVFrame* pF = hAudioTranscoder->pop())
        {
            mmWriter.write(pF, outAuStr);
        }
        hAudioTranscoder.reset(nullptr);
        //signal eof to writer
        mmWriter.write(nullptr, -1);
    }
    catch(std::exception& e)
    {
        logging::print_exception(e);
        return EXIT_FAILURE;
    }
    LOGD("Exiting successfully...");
    return EXIT_SUCCESS;
}

namespace
{

}   //::<anon>
