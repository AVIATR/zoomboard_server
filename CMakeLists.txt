# Set up the project
cmake_minimum_required(VERSION 3.5.0)
#SET(MACOSX_DEPLOYMENT_TARGET ${DARWIN_MAJOR_SDK_VERSION}.${DARWIN_MINOR_SDK_VERSION})
#SET(CMAKE_CXX_FLAGS "--sysroot ${CMAKE_OSX_SYSROOT} ${CMAKE_CXX_FLAGS}")
#MESSAGE("Setting MACOSX_DEPLOYMENT_TARGET to '${MACOSX_DEPLOYMENT_TARGET}'.")
#set(CMAKE_OSX_DEPLOYMENT_TARGET "")
#set(MACOSX_DEPLOYMENT_TARGET "")
#set(CMAKE_OSX_SYSROOT "")
#SET(CMAKE_CXX_FLAGS "--sysroot ${CMAKE_OSX_SYSROOT} ${CMAKE_CXX_FLAGS}")
#MESSAGE("Setting MACOSX_DEPLOYMENT_TARGET to '${MACOSX_DEPLOYMENT_TARGET}'.")
if(DEFINED $ENV{PROJ_NAME})
    set(PROJ_NAME $ENV{PROJ_NAME})
else()
    set(PROJ_NAME "zoomboard_server")
endif()

project("${PROJ_NAME}"
	LANGUAGES CXX
	VERSION 0.1.0)

#set(CMAKE_OSX_ARCHITECTURES "x86_64;i386")
set( CMAKE_VERBOSE_MAKEFILE on )	#uncomment for make script debugging purposes

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

###################################
# Detect and add external libraries
###################################
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake_modules" ${CMAKE_MODULE_PATH})
message(STATUS "Module path set to: ${CMAKE_MODULE_PATH}")

#Find Log4cxx
find_package(LOG4CXX REQUIRED)
if (LOG4CXX_FOUND)
    message(STATUS "LOG4CXX Found")
    message(STATUS "LOG4CXX_LIBRARY_NAME = ${LOG4CXX_LIBRARIES}")
    message(STATUS "LOG4CXX_INCLUDE_DIRS = ${LOG4CXX_INCLUDE_DIRS}")
endif()

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
# Configure files
###################################
message("\nProject = ${PROJECT_NAME}")
message(STATUS "Build type = ${CMAKE_BUILD_TYPE}")
message(STATUS "SOURCE DIR = ${PROJECT_SOURCE_DIR}")
message(STATUS "BINARY DIR = ${PROJECT_BINARY_DIR}")
#Add source folder
add_subdirectory("src")
set(TGT_FOLDER $<TARGET_FILE_DIR:${PROJECT_NAME}>)
message(STATUS "Target folder = ${TGT_FOLDER}")
configure_file("config.json" ${TGT_FOLDER} COPYONLY)

# CPack packaging
include(InstallRequiredSystemLibraries)
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/COPYING")
set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
include(CPack)
