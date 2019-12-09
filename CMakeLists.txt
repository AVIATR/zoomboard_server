# Set up the project
cmake_minimum_required(VERSION 3.5.0)
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

if(NOT DEFINED WWW_DIR)
    set(WWW_DIR "/tmp/zoombrd")
endif()

cmake_minimum_required(VERSION 3.5)

message(STATUS "CMAKE_BINARY_DIR= ${CMAKE_BINARY_DIR}")
#set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/${CMAKE_BUILD_TYPE}")
#set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/${CMAKE_BUILD_TYPE}")
#set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/${CMAKE_BUILD_TYPE}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
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

#Find Pthreads
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

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

# Find Boost
find_package(Boost COMPONENTS program_options filesystem REQUIRED)
if (Boost_FOUND)
    message(STATUS "Boost Found")
    message(STATUS "Boost_LIBRARIES = ${Boost_LIBRARIES}")
    message(STATUS "Boost_INCLUDE_DIRS = ${Boost_INCLUDE_DIRS}")
endif()

# Find OpenCV
find_package(OpenCV COMPONENTS core imgproc highgui aruco videoio REQUIRED)
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
message(STATUS "Target folder = ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "BUilding for platform = ${CMAKE_SYSTEM_NAME}")
set(RESOURCE_FOLDER "${CMAKE_SOURCE_DIR}/res")
#configure_file("${RESOURCE_FOLDER}/${CMAKE_SYSTEM_NAME}/input.json" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/input.json" COPYONLY)
#configure_file("${RESOURCE_FOLDER}/${CMAKE_SYSTEM_NAME}/output.json" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/output.json" COPYONLY)
if (${CMAKE_SYSTEM_NAME} STREQUAL "Darwin") #Macos - assume default cam is built-in facetime camera
    set(INPUT_DEVICE "0")
    set(MUXER "avfoundation")
    set(DEFAULT_FULL_RESOLUTION "848x480")
elseif (${CMAKE_SYSTEM_NAME} STREQUAL "Linux") #RaspberryPi - assume default cam is Logitech 930
    set(INPUT_DEVICE "/dev/video0")
    set(MUXER "v4l2")
    set(DEFAULT_FULL_RESOLUTION "1920x1080")
    set(CMAKE_CXX_FLAGS "-Wl,-latomic") #workaround an issue with opencv on rpi - https://github.com/opencv/opencv/issues/15278
    configure_file(
        "stream.supervisor.conf.in"
        "stream.supervisor.conf"
        @ONLY
    )
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/stream.supervisor.conf" DESTINATION .)
endif()
set(DEFAULT_LOW_RESOLUTION "384x216")

###################################
#Add subfolders
###################################
add_subdirectory("res") #resources
add_subdirectory("nginx") #nginxs
add_subdirectory("src") #source code

###################################
#Install outputs
###################################
message(STATUS "Files will be installed to ${CMAKE_INSTALL_PREFIX}")
#file(GLOB_RECURSE RES_FILES "${RESOURCE_FOLDER}/*.json")
#file(GLOB RES_FILES "${RESOURCE_FOLDER}/*.json")
#file(GLOB PLATFORM_RES_FILES "${RESOURCE_FOLDER}/${CMAKE_SYSTEM_NAME}/*.json")
#message(STATUS "Resource files to install: ${RES_FILES};${PLATFORM_RES_FILES}")
#install(FILES ${RES_FILES} ${PLATFORM_RES_FILES} DESTINATION .)

#Configure & install scripts
configure_file(
    "stream.sh.in"
    "stream.sh"
    @ONLY
)
set(SCRIPT_FILES "${CMAKE_SOURCE_DIR}/play.sh;${CMAKE_CURRENT_BINARY_DIR}/stream.sh")
message(STATUS "Script files to install: ${SCRIPT_FILES}")
install(PROGRAMS ${SCRIPT_FILES} DESTINATION .)

# CPack packaging
include(InstallRequiredSystemLibraries)
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/COPYING")
set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
include(CPack)
