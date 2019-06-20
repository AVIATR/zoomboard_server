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

#to access the stream, the url is http://127.0.0.1:8080/hls/stream.m3u8
