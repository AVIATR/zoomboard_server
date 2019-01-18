# Enable debug symbols by default (you can also set it on the command line: -D CMAKE_BUILD_TYPE=Release)
if(NOT DEFINED CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Debug")
elseif (CMAKE_BUILD_TYPE STREQUAL "")
  set(CMAKE_BUILD_TYPE "Debug")
endif()
cmake_minimum_required(VERSION 3.5)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}")
list(APPEND CMAKE_LIBRARY_PATH ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})

set(CMAKE_CXX_STANDARD 14)

message(STATUS "Build type = ${CMAKE_BUILD_TYPE}")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  message(STATUS "Compile options = ${CMAKE_CXX_FLAGS_DEBUG}")
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
  message(STATUS "Compile options = ${CMAKE_CXX_FLAGS_RELEASE}")
else()
  message(FATAL_ERROR "Unknown build type")
endif()

set(LOCAL_HEADERS_DIR "include")
include_directories(${LOCAL_HEADERS_DIR})

# Set version information in a version.h file
configure_file(
  "version.h.in"
  "${LOCAL_HEADERS_DIR}/version.h"
  )

###################################
# Detect and add external libraries
###################################

# Find ffmpeg
find_package(FFMPEG REQUIRED)
if (FFMPEG_FOUND)
  message(STATUS "FFMPEG Found")
  message(STATUS "FFMPEG_LIBRARIES = ${FFMPEG_LIBRARIES}")
  message(STATUS "FFMPEG_INCLUDE_DIRS = ${FFMPEG_INCLUDE_DIRS}")
endif()

# Find OpenCV
find_package(OpenCV REQUIRED)
if (OpenCV_FOUND)
  message(STATUS "OpenCV Found")
  message(STATUS "OpenCV_LIBRARIES = ${OpenCV_LIBRARIES}")
  message(STATUS "OpenCV_INCLUDE_DIRS = ${OpenCV_INCLUDE_DIRS}")
endif()

###################################
# Add subprojects
###################################
message(STATUS "SOURCE DIR = ${PROJECT_SOURCE_DIR}")
message(STATUS "BINARY DIR = ${PROJECT_BINARY_DIR}")

# Install header files
message(STATUS "Files will be installed to ${CMAKE_INSTALL_PREFIX}")
file(GLOB HEADERS "${LOCAL_HEADERS_DIR}/*.h" "${LOCAL_HEADERS_DIR}/*.hpp")
install(FILES ${HEADERS} DESTINATION include)
