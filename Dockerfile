############################################################
# Dockerfile to build the source code using OpenCV 4.x
############################################################

FROM aviatr/opencv:3.3.1
MAINTAINER Ender Tekin <etekin@wisc.edu>
#ENV DEBIAN_FRONTEND noninteractive
WORKDIR /tmp/avspeech/
RUN chmod +x /tini && \
    apt-get update && \
    apt-get install -y -q fftw3 fftw3-dev libboost-filesystem-dev && \
    mkdir build

COPY src /tmp/avspeech/src/
COPY cmake_modules /tmp/avspeech/cmake_modules
COPY CMakeLists.txt /tmp/avspeech
COPY COPYING /tmp/avspeech
WORKDIR /tmp/avspeech/build
RUN cmake -DBUILD_SHARED_LIBS=YES .. && \
    make && \
    make install && \
    rm -rf /tmp/avspeech

ENTRYPOINT ["/tini", "--"]
CMD ["sh"]
