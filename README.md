# Zoomboard Raspberty-Pi Server
This builds the executable to create an HLS stream to be used by the server for the zoomboard project. It opens the default camera (or a file if one is provided), displays it on a window to allow the user to choose the four corners to be rectified, applies a perspective transform to rectify the image and then creates an HLS stream to serve.

## Configuration files
These are json files that provide the necessary configuration options (input device, resolution, frame rate, other ffmpeg options). as well as the output url. For details about the input device options, see the [ffmpeg faq about capture devices](https://trac.ffmpeg.org/wiki/Capture/Webcam).