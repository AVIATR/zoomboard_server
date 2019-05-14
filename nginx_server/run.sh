#!/bin/bash
set -e

#remove any old files
rm hls/*

#launch nginx server
docker run -d --name my_server \
    -v$(pwd)/nginx.conf:/etc/nginx/nginx.conf \
    -v$(pwd):/usr/share/nginx \
    -p8080:8080 \
    tiangolo/nginx-rtmp

#start streaming
ffmpeg -f avfoundation -r 30 -s 640x480 -i "0" \
    -vf "format=yuv420p,framerate=5" -c:v libx264 -profile:v:0 high -level 3.0 -flags +cgop -g 1 -an\
        -hls_time 0.1 -hls_allow_cache 0 -preset ultrafast -hls_wrap 30 \
            hls/stream.m3u8

#to access the stream, the url is http://127.0.0.1:8080/hls/stream.m3u8
