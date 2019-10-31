#!/bin/bash
if [[ "$OSTYPE" == "linux-gnu" ]]; then
    sudo rm /usr/share/hls/stream*
    #start streaming
    sudo ffmpeg -f video4linux2  -r 30 -s 1920x1080 -i "0" -vf "format=yuv420p,framerate=5" -c:v libx264 -profile:v:0 high -preset ultrafast -tune zerodelay -level 5.0 -flags +cgop -g 1 -an -hls_time 0.1 -hls_allow_cache 0 -hls_wrap 30 /usr/share/nginx/hls/stream.m3u8
elif [[ "$OSTYPE" == "darwin"* ]]; then
    #remove old files
    rm hls/stream*
    #start streaming
    ffmpeg -f avfoundation  -r 30 -s 640x480 -i "0" -vf "format=yuv420p,framerate=5" -c:v libx264 -profile:v:0 high -preset ultrafast -tune zerodelay -level 5.0 -flags +cgop -g 1 -an -hls_time 0.1 -hls_allow_cache 0 -hls_wrap 30 hls/stream.m3u8
else        # Unknown.
    echo "Implementation for $OSTYPE is not available."
fi
