# Zoomboard Raspberry-Pi Server
This builds the executable to create an HLS stream to be used by the server for the zoomboard project. It opens the default camera (or a file if one is provided), displays it on a window to allow the user to choose the four corners to be rectified, applies a perspective transform to rectify the image and then creates an HLS stream to serve.

## Dependencies
This project depends on a few external open-source libraries:
* [OpenCV](https://opencv.org/): for computer-vision related things and graphical interface. OpenCV is used for obtaining the corners of the board and undoing the perspective transformation
* [LibAV](https://www.libav.org/): These are used for opening the input stream and transcoding the video stream to the output formats.
* [log4cxx](https://logging.apache.org/log4cxx/latest_stable/): Used for logging across threads

## Configuration file
This json file provides the necessary configuration options (input device, resolution, frame rate, other ffmpeg options). as well as the output url and options. For details about the input device options, see the [ffmpeg faq about capture devices](https://trac.ffmpeg.org/wiki/Capture/Webcam).


## References
### X264 Settings
We are using libx264 for encoding. The presets and how they correspond to various codec parameters can be found [here](http://dev.beandog.org/x264_preset_reference.html). If you have the `x264` binary installed, you can also see the installed presets by `x264 --fullhelp`.