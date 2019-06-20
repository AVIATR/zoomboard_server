#!/bin/bash

#remove old files
rm hls/stream*

#start streaming

if [[ "$OSTYPE" == "linux-gnu" ]]; then
    # *Nix
    ffmpeg -f video4linux2 \    #webcam format
        -r 30 \                 #input framerate
        -s 1080p \              #input frame size - 1080p
        -i "0" \                #first attached camera
        -vf "format=yuv420p,framerate=5" \ #output video formatting
        -c:v libx264 \          #outout codec
        -profile:v:0 high \     #output codec profile
        -preset ultrafast \     #output codec preset
        -tune zerodelay \       #tune output codec parameters
        -level 5.0 \
        -flags +cgop -g 1 \     #various flags to minimize delay etc
        -an \                   #no audio
        -hls_time 0.1 -hls_allow_cache 0 -hls_wrap 30 \ #hls options
        hls/stream.m3u8         # where to put the output stream
        
elif [[ "$OSTYPE" == "darwin"* ]]; then
    # Mac OSX
    ffmpeg -f avfoundation \    #webcam driver
        -r 30 \                 #input framerate
        -s 640x480 \            #input video size
        -i "0" \
        -vf "format=yuv420p,framerate=5" \ #output video formatting
        -c:v libx264 \          #outout codec
        -profile:v:0 high \     #output codec profile
        -preset ultrafast \     #output codec preset
        -tune zerodelay \       #tune output codec parameters
        -level 5.0 \
        -flags +cgop -g 1 \     #various flags to minimize delay etc
        -an \                   #no audio
        -hls_time 0.1 -hls_allow_cache 0 -hls_wrap 30 \ #hls options
        hls/stream.m3u8         # where to put the output stream

#elif [[ "$OSTYPE" == "cygwin" ]]; then
        # POSIX compatibility layer and Linux environment emulation for Windows
#elif [[ "$OSTYPE" == "msys" ]]; then
        # Lightweight shell and GNU utilities compiled for Windows (part of MinGW)
#elif [[ "$OSTYPE" == "win32" ]]; then
        # I'm not sure this can happen.
#elif [[ "$OSTYPE" == "freebsd"* ]]; then
        # ...
else
        # Unknown.
        echo "Implementation for $OSTYPE is not available."
fi
