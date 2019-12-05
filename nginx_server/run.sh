#!/bin/bash
set -e

STREAM_FOLDER=$(pwd)
function USAGE {
    echo "Usage: run.sh [-h] [-s stream_folder]"
}
#remove any old files
while getopts ":hs:" opt; do
    case ${opt} in
        h ) # process option a
            USAGE
            ;;
        
        s ) # process option t
            STREAM_FOLDER=${OPTARG}
            ;;
        
        \? )
            echo "Invalid Option: -$OPTARG" 1>&2
            USAGE
            exit 1
        ;;
    esac
done
shift $((OPTIND -1))

echo "Stream folder is ${STREAM_FOLDER}"
#launch nginx server
docker run -d --rm --name zoombrd \
    -v$(pwd)/nginx.conf:/etc/nginx/nginx.conf \
    -v$(pwd)/html:/usr/share/nginx/html \
    -v"${STREAM_FOLDER}":/usr/share/nginx/hls \
    -p8080:8080 \
    tiangolo/nginx-rtmp

#to access the stream, the url is http://127.0.0.1:8080/hls/stream.m3u8
