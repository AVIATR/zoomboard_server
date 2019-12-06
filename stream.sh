#!/bin/sh

#Make output folder & remove old files
OUT_DIR=/tmp/zoombrd
mkdir -p ${OUT_DIR}
rm -rf ${OUT_DIR}/*
mkdir ${OUT_DIR}/html/
mkdir ${OUT_DIR}/hls/
cp html/index.html ${OUT_DIR}/html/

if [[ "$OSTYPE" == "linux-gnu" ]]; then
    DRIVER=v4l2 #video4linux2
    RESOLUTION=1920x1080
elif [[ "$OSTYPE" == "darwin"* ]]; then
    DRIVER=avfoundation
    RESOLUTION=848x480
else        # Unknown.
    echo "Implementation for $OSTYPE is not available."
fi

#start streaming
ffmpeg -f ${DRIVER} -s ${RESOLUTION} -r 30 -i "0" -f hls -hls_time 0.3 -hls_allow_cache 0 -hls_flags temp_file+delete_segments+independent_segments -hls_delete_threshold 1 -hls_list_size 60 -c:v libx264 -b:v 7000k -maxrate 10000k -crf 18 -flags +cgop+low_delay+qscale -r 15 -g 15 -refs 1 -strict normal -level 4.2 -profile:v high -preset ultrafast -tune zerolatency -pix_fmt yuv420p -vf "[in]drawtext=text='time=%{localtime}':box=1:x=(w-tw)/2:y=2*lh, drawtext=text='frame=%{n}, ts=%{pts\:hms}':box=1:x=(w-tw)/2:y=(3*lh)" ${OUT_DIR}/hls/stream.m3u8
