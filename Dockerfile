############################################################
# Dockerfile to build the source code using OpenCV 4.x
############################################################

FROM aviatr/opencv
MAINTAINER Ender Tekin <etekin@wisc.edu>
ENV DEBIAN_FRONTEND noninteractive
ENV SOURCE_DIR /tmp/rtmp_server
ENV OPENCV_REPOSITORY https://github.com/opencv/opencv.git
WORKDIR ${SOURCE_DIR}
RUN apt-get update && \
    apt-get install -y -q libboost-filesystem-dev

#Build & install project
COPY . ${SOURCE_DIR}
RUN cd ${SOURCE_DIR} && \
    ./build_project.sh -b build && \
    cd build && \
    make && \
    make install && \
    rm -rf ${SOURCE_DIR}

CMD ["/usr/local/rtmp_server", "config_linux.json"]
