#!/bin/bash
if [[ "$OSTYPE" == "linux-gnu" ]]; then
    sudo rm /usr/share/hls/stream*
    #start streaming
    sudo ffmpeg -f video4linux2 -r 30 -s 1920x1080 -i "0" -f hls -hls_time 0.3 -hls_allow_cache 0 -hls_flags temp_file+delete_segments+independent_segments -hls_delete_threshold 1 -hls_list_size 60 -c:v libx264 -b:v 7000k -maxrate 10000k -crf 18 -flags +cgop+low_delay+qscale -r 15 -g 15 -refs 1 -strict normal -level 4.2 -profile:v high -preset ultrafast -tune zerolatency -pix_fmt yuv420p -vf "[in]drawtext=text='time=%{localtime}':box=1:x=(w-tw)/2:y=2*lh, drawtext=text='frame=%{n}, ts=%{pts\:hms}':box=1:x=(w-tw)/2:y=(3*lh)" /usr/share/nginx/hls/stream.m3u8
elif [[ "$OSTYPE" == "darwin"* ]]; then
    #remove old files
    rm hls/stream*
    #start streaming
    ffmpeg -f avfoundation  -r 30 -s 640x480 -i "0" -vf "format=yuv420p,framerate=5" -c:v libx264 -profile:v:0 high -preset ultrafast -tune zerodelay -level 5.0 -flags +cgop -g 1 -an -hls_time 0.1 -hls_allow_cache 0 -hls_wrap 30 hls/stream.m3u8
else        # Unknown.
    echo "Implementation for $OSTYPE is not available."
fi
