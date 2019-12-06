#!/bin/sh
set -e

STREAM_FOLDER=$(pwd)
function USAGE {
    echo "Usage: $0 [-h] [-s stream_folder]"
}
#remove any old files
while getopts ":hs:" opt; do
    case ${opt} in
        h ) # process help request
            USAGE
            exit 0
            ;;
        
        s ) # process option s
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
    -v$(pwd)/html:/tmp/zoombrd/html \
    -v"${STREAM_FOLDER}":/tmp/zoombrd/hls \
    -p8080:8080 \
    tiangolo/nginx-rtmp

#to access the stream, the url is http://127.0.0.1:8080/hls/stream.m3u8
