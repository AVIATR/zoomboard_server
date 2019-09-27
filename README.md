# Zoomboard Raspberry-Pi Server
This builds the executable to create an HLS stream to be used by the server for the zoomboard project. It opens the default camera (or a file if one is provided), displays it on a window to allow the user to choose the four corners to be rectified, applies a perspective transform to rectify the image and then creates an HLS stream to serve.

## Dependencies
This project depends on a few external open-source libraries:
* [OpenCV](https://opencv.org/): for computer-vision related things and graphical interface. OpenCV is used for obtaining the corners of the board and undoing the perspective transformation
* [LibAV](https://www.libav.org/): These are used for opening the input stream and transcoding the video stream to the output formats.
* [log4cxx](https://logging.apache.org/log4cxx/latest_stable/): Used for logging across threads

### Installing dependencies on MacOS:
To install the dependencies on MacOS using homebrew, just do
`brew install ffmpeg ffmpeg-dev cmake opencv log4cxx boost`

### Installing dependencies on Raspbian:
To install the dependencies on Raspbian:
`sudo apt-get install libavcodec-dev libavformat-dev libavfilter-dev \
  libavdevice-dev libswscale-dev cmake liblog4cxx-dev \
  libboost-dev libboost-program-options-dev libboost-filesystem-dev`
  
Unfortunately, the version of OpenCV that is in the repos does not come with Aruco. It is recommended that OpenCV be built from scratch. To do this, refer to the instructions [here](https://www.learnopencv.com/install-opencv-4-on-raspberry-pi/).

## Configuration file
This json file provides the necessary configuration options (input device, resolution, frame rate, other ffmpeg options). as well as the output url and options. For details about the input device options, see the [ffmpeg faq about capture devices](https://trac.ffmpeg.org/wiki/Capture/Webcam).

## Calibration
The system is set up initially by creating the markers, using the `create_markers` executable. To run, simply use

    ./create_markers <marker_file.json>

where `marker_file.json` is a json file to save the markers to. This also saves an image called `arucobrd_2x2.png` to the same folder. This image is then used for camera calibration using the `calibrate_camera` executable. To calibrate, simply use

    ./calibrate_camera -m <marker_file.json> <calibration_file.json>

where `marker_file.json` is the output of the `create_markers` process, and `calibration_file.json` is the output of camera calibration that contains the calibration matrix and distortion coefficients.

## Local testing of the server
The zoomboard server is started via

    ./zoomboard_server -i <input> -o <output>
    
where the `<input>` and `<output>` can be media files or preferably .json files that have information re: input/output parameters to use if non-default values are to be used. To see how these files are set up, look at the example `input.json` and `output.json` files.

The nginx server can be started using the script in the nginx folder. The usage is

    ./run.sh -s <stream_folder>

where `stream_folder` is the folder where the server is putting the stream files. The streams can then be played via ffplay:

    ffplay http://127.0.0.1:8080/hls/stream_lr.m3u8

To play the hi-res stream, replace the URL above with `http://127.0.0.1:8080/hls/stream_hr.m3u8`


## References
### LibAV Reading/Writing Process
An overview of the LibAV read/write process can be found in the documentation.
* *Decoding Process:* An overview is found [here](https://ffmpeg.org/doxygen/2.8/group__lavf__decoding.html)
* *Encoding Process:* An overview is found [here](https://ffmpeg.org/doxygen/2.8/group__lavf__encoding.html) and [here](https://ffmpeg.org/doxygen/2.8/group__libavf.html)

### X264 Settings
We are using libx264 for encoding. The presets and how they correspond to various codec parameters can be found [here](http://dev.beandog.org/x264_preset_reference.html). If you have the `x264` binary installed, you can also see the installed presets by `x264 --fullhelp`. Some more information about the tuning and presets is given [here](https://trac.ffmpeg.org/wiki/Encode/H.264).

Specific parameters can also be found for ffmpeg [here](https://ffmpeg.org/ffmpeg-codecs.html#libx264_002c-libx264rgb), and some more details about the parameters is also given [here](https://sites.google.com/site/linuxencoding/x264-ffmpeg-mapping)


### Further reading about libav and transcoding:
Here are some useful links that were very helpful during the development of this software:
* https://stackoverflow.com/a/40278283
* https://ffmpeg.org/ffmpeg-formats.html#hls-2

An overview of the LibAV read/write process can be found in the documentation.
* *Decoding Process:* An overview is found [here](https://ffmpeg.org/doxygen/2.8/group__lavf__decoding.html)
* *Encoding Process:* An overview is found [here](https://ffmpeg.org/doxygen/2.8/group__lavf__encoding.html) and [here](https://ffmpeg.org/doxygen/2.8/group__libavf.html). For framerate scaling, timestamps need to be adjusted, and some frames
may be dropped. Some information about the different timebases etc. are given [here](https://stackoverflow.com/questions/40275242/libav-ffmpeg-copying-decoded-video-timestamps-to-encoder/40278283#40278283).

The full set of muxer, codec and x264 private codec options can be found by examining the AVOption definitions in the following files:
* Muxer Options: https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/options_table.h
* Codec Options: https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/options_table.h
* x264 Private Options: https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/libx264.c

To minimize delay, we are not using any B frames. More information about the group of frames structure and the frame types can be found at [1](https://en.wikipedia.org/wiki/Group_of_pictures), [2](https://en.wikipedia.org/wiki/Inter_frame), [3](https://en.wikipedia.org/wiki/Intra-frame_coding).

We also use CRF encoding for the high-rate stream, and constant QP encoding for the low-rate stream. To learn more about these, see [here](https://slhck.info/video/2017/02/24/crf-guide.html)

### Debugging
To explore the frame information & group of frames structure, use ffprobe.
* For detailed frame info: `ffprobe -show_frames <stream.m3u8>`
* For compact info re: frame type, timestamps etc: `ffprobe -show_entries frame=key_frame,pict_type,best_effort_timestamp_time -of compact <stream.m3u8>`
and replace `<stream.m3u8>` with the name of the particular output stream playlist.

### Discovered demuxer and decoder options using intergrated camera & avfoundation:
#### Demuxer options
* avioflags:0x00000000
* probesize:5000000
* formatprobesize:1048576
* fflags:0x00200000
* seek2any:false
* analyzeduration:0
* cryptokey:
* indexmem:1048576
* rtbufsize:3041280
* fdebug:0x00000000
* max_delay:-1
* fpsprobesize:-1
* f_err_detect:0x00000001
* err_detect:0x00000001
* use_wallclock_as_timestamps:false
* skip_initial_bytes:0
* correct_ts_overflow:true
* f_strict:0
* strict:0
* max_ts_probe:50
* dump_separator:,\ 
* codec_whitelist:
* format_whitelist:
* protocol_whitelist:
* protocol_blacklist:
* max_streams:1000
* skip_estimate_duration_from_pts:false

#### Demuxer private options
* list_devices:0
* video_device_index:0
* audio_device_index:-1
* pixel_format:uyvy422
* framerate:30/1
* video_size:848x480
* capture_cursor:0
* capture_mouse_clicks:0

#### Decoder options
* flags:0x00000000
* ar:0
* ac:0
* bug:0x00000001
* strict:0
* err_detect:0x00000000
* idct:0
* ec:0x00000003
* debug:0x00000000
* flags2:0x00000000
* threads:1
* skip_top:0
* skip_bottom:0
* lowres:0
* skip_loop_filter:0
* skip_idct:0
* skip_frame:0
* channel_layout:0
* request_channel_layout:0
* ticks_per_frame:1
* color_primaries:2
* color_trc:2
* colorspace:2
* color_range:0
* chroma_sample_location:0
* thread_type:0x00000003
* request_sample_fmt:none
* sub_charenc:
* sub_charenc_mode:0x00000000
* sub_text_format:1
* refcounted_frames:true
* apply_cropping:true
* skip_alpha:false
* field_order:0
* dump_separator:
* codec_whitelist:
* max_pixels:2147483647
* hwaccel_flags:0x00000001
* extra_hw_frames:-1

#### Decoder private options
* top:auto

### Discovered muxer and encoder options for writing h264 encoded hls streams:
#### Muxer options
* audio_preload:0
* avioflags:0x00000000
* avoid_negative_ts:-1
* chunk_duration:0
* chunk_size:0
* fdebug:0x00000000
* fflags:0x00200000
* flush_packets:-1
* f_strict:0
* max_delay:-1
* max_interleave_delta:10000000
* metadata_header_padding:-1
* output_ts_offset:0
* packetsize:0
* start_time_realtime:-9223372036854775808
* strict:0

#### Muxer private options
* cc_stream_map:
* hls_allow_cache:0
* hls_base_url:
* hls_delete_threshold:1
* hls_enc:false
* hls_enc_iv:
* hls_enc_key:
* hls_enc_key_url:
* hls_flags:0x00000802
* hls_fmp4_init_filename:init.mp4
* hls_init_time:0.000000
* hls_key_info_file:
* hls_list_size:10
* hls_playlist_type:0
* hls_segment_filename:
* hls_segment_size:0
* hls_segment_type:0
* hls_start_number_source:0
* hls_subtitle_path:
* hls_time:0.100000
* hls_ts_options:
* hls_vtt_options:
* hls_wrap:0
* http_persistent:false
* http_user_agent:
* master_pl_name:
* master_pl_publish_rate:0
* method:
* start_number:0
* strftime:false
* strftime_mkdir:false
* timeout:-0.000001
* use_localtime:false
* use_localtime_mkdir:false
* var_stream_map:

#### Encoder options
* ab:128000
* ac:0
* ar:0
* aspect:0/1
* audio_service_type:0
* b:128000
* bf:0
* bidir_refine:1
* brd_scale:0
* bt:4000000
* bufsize:0
* b_qfactor:1.250000
* b_qoffset:1.250000
* b_sensitivity:40
* b_strategy:0
* channel_layout:0
* chromaoffset:0
* chroma_sample_location:0
* cmp:0
* coder:0
* colorspace:2
* color_primaries:2
* color_range:0
* color_trc:2
* compression_level:-1
* context:0
* cutoff:0
* dark_mask:0.000000
* dc:0
* dct:0
* debug:0x00000000
* dia_size:0
* dump_separator:
* field_order:0
* flags2:0x00000000
* flags:0x00400000
* frame_size:0
* g:12
* global_quality:0
* idct:0
* ildctcmp:8
* i_qfactor:0.710000
* i_qoffset:0.000000
* keyint_min:25
* last_pred:0
* level:-99
* lumi_mask:0.000000
* maxrate:0
* max_pixels:2147483647
* max_prediction_order:-1
* mbcmp:0
* mbd:0
* mblmax:3658
* mblmin:236
* mepc:256
* me_range:16
* minrate:0
* min_prediction_order:-1
* mpeg_quant:0
* mv0_threshold:256
* nr:0
* nssew:8
* precmp:0
* pred:0
* preme:0
* pre_dia_size:0
* profile:-99
* ps:0
* p_mask:0.000000
* qblur:0.500000
* qcomp:0.600000
* qdiff:3
* qmax:51
* qmin:10
* rc_init_occupancy:0
* rc_max_vbv_use:0.000000
* rc_min_vbv_use:3.000000
* refs:1
* sar:0/1
* scplx_mask:0.000000
* sc_threshold:0
* side_data_only_packets:true
* skipcmp:13
* skip_exp:0
* skip_factor:0
* skip_threshold:0
* slices:0
* strict:0
* subcmp:0
* subq:8
* tcplx_mask:0.000000
* threads:1
* thread_type:0x00000003
* ticks_per_frame:1
* timecode_frame_start:-1
* trellis:0

#### Encoder private options
* 8x8dct:auto
* a53cc:true
* aq-mode:-1
* aq-strength:-1.000000
* aud:auto
* avcintra-class:-1
* b-bias:-2147483648
* b-pyramid:-1
* bluray-compat:auto
* b_strategy:0
* chromaoffset:0
* coder:0
* cplxblur:-1.000000
* crf:-1.000000
* crf_max:-1.000000
* deblock:
* direct-pred:-1
* fast-pskip:auto
* fastfirstpass:true
* forced-idr:false
* intra-refresh:auto
* level:
* mbtree:auto
* me_method:-1
* mixed-refs:auto
* motion-est:-1
* nal-hrd:-1
* noise_reduction:0
* partitions:
* passlogfile:
* preset:ultrafast
* profile:high422
* psy-rd:
* psy:auto
* qp:-1
* rc-lookahead:-1
* sc_threshold:0
* slice-max-size:-1
* ssim:auto
* stats:
* tune:zerolatency
* weightb:auto
* weightp:-1
* wpredp:
* x264-params:
* x264opts: