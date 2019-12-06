# Define sources and targets
# Set version information in a version.h file
message(STATUS "Current source dir: ${CMAKE_CURRENT_SOURCE_DIR}")
message(STATUS "Current binary dir: ${CMAKE_CURRENT_BINARY_DIR}")
configure_file(
    "common.hpp.in"
    "include/common.hpp"
    )
include_directories("${CMAKE_CURRENT_BINARY_DIR}/include")

#Get files for zoomboard_server
#file(GLOB SOURCE_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "main.cxx" "*.cpp" )
#file(GLOB HEADER_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "*.h" "*.hpp" "*.ipp" "${CMAKE_CURRENT_BINARY_DIR}/include/common.hpp")

set(INCLUDE_DIRS ${FFMPEG_INCLUDE_DIRS} ${Boost_INCLUDE_DIRS} ${OpenCV_INCLUDE_DIRS} ${LOG4CXX_INCLUDE_DIRS} ${HEADERS_DIR})
list(REMOVE_DUPLICATES INCLUDE_DIRS)
include_directories(${INCLUDE_DIRS})
message(STATUS "Include Dirs = ${INCLUDE_DIRS}")

#Set up aruco executable
set(TARGET_NAME "create_markers")
set(DEPENDENCIES "${CMAKE_CURRENT_BINARY_DIR}/include/common.hpp" common.cpp create_markers.cxx)
add_executable(${TARGET_NAME} ${DEPENDENCIES})
set_property(TARGET ${TARGET_NAME} PROPERTY CXX_STANDARD 14)
set_property(TARGET ${TARGET_NAME} PROPERTY CXX_STANDARD_REQUIRED ON)
set(LINKED_LIBS ${Boost_LIBRARIES} ${OpenCV_LIBRARIES} ${LOG4CXX_LIBRARIES})
target_link_libraries(${TARGET_NAME} ${LINKED_LIBS})
message(STATUS "${TARGET_NAME} Dependencies = ${DEPENDENCIES};${LINKED_LIBS}" )
get_target_property(LINKED_LIBS ${TARGET_NAME} INTERFACE_LINK_LIBRARIES)
message(STATUS "${TARGET} Linked Libraries: ${LINKED_LIBS}" )
set_target_properties( ${TARGET_NAME}
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
install(TARGETS ${TARGET_NAME} RUNTIME DESTINATION .)

#Set up calibration executable
set(TARGET_NAME "calibrate_camera")
set(DEPENDENCIES "${CMAKE_CURRENT_BINARY_DIR}/include/common.hpp" common.cpp calibrate.cxx)
add_executable(${TARGET_NAME} ${DEPENDENCIES})
set_property(TARGET ${TARGET_NAME} PROPERTY CXX_STANDARD 14)
set_property(TARGET ${TARGET_NAME} PROPERTY CXX_STANDARD_REQUIRED ON)
set(LINKED_LIBS ${Boost_LIBRARIES} ${OpenCV_LIBRARIES} ${LOG4CXX_LIBRARIES})
target_link_libraries(${TARGET_NAME} ${LINKED_LIBS})
message(STATUS "${TARGET_NAME} Dependencies = ${DEPENDENCIES};${LINKED_LIBS}" )
get_target_property(LINKED_LIBS ${TARGET_NAME} INTERFACE_LINK_LIBRARIES)
message(STATUS "${TARGET} Linked Libraries: ${LINKED_LIBS}" )
set_target_properties( ${TARGET_NAME}
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
install(TARGETS ${TARGET_NAME} RUNTIME DESTINATION .)

#Set up main executable
set(TARGET_NAME ${PROJECT_NAME})
file(GLOB SRC_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS "*.hpp" "*.cpp")
set(DEPENDENCIES "${CMAKE_CURRENT_BINARY_DIR}/include/common.hpp" ${SRC_FILES} main.cxx)
add_executable(${TARGET_NAME} ${DEPENDENCIES})
set_property(TARGET ${TARGET_NAME} PROPERTY CXX_STANDARD 14)
set_property(TARGET ${TARGET_NAME} PROPERTY CXX_STANDARD_REQUIRED ON)
set(LINKED_LIBS ${FFMPEG_LIBRARIES} ${Boost_LIBRARIES} ${OpenCV_LIBRARIES} ${LOG4CXX_LIBRARIES} Threads::Threads)
target_link_libraries(${TARGET_NAME} ${LINKED_LIBS})
message(STATUS "${TARGET_NAME} Dependencies = ${DEPENDENCIES};${LINKED_LIBS}" )
get_target_property(LINKED_LIBS ${TARGET_NAME} INTERFACE_LINK_LIBRARIES)
message(STATUS "${TARGET} Linked Libraries: ${LINKED_LIBS}" )
set_target_properties( ${TARGET_NAME}
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
install(TARGETS ${TARGET_NAME} RUNTIME DESTINATION .)
#Copy any previously avilable calibration files
#file(GLOB CAMERA_CALIBRATION_FILES "${RESOURCE_FOLDER}/*.json")
#message(STATUS "Found calibration files: ${CAMERA_CALIBRATION_FILES}")
#file(COPY ${CAMERA_CALIBRATION_FILES} DESTINATION "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
add_custom_command(
    TARGET ${PROJECT_NAME}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} 
    ARGS -E copy "${RESOURCE_FOLDER}/*.json" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)

#Copy io specification files
#file(GLOB IO_SPEC_FILES "${RESOURCE_FOLDER}/${CMAKE_SYSTEM_NAME}/*.json")
#file(COPY ${IO_SPEC_FILES} DESTINATION "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
add_custom_command(
    TARGET ${PROJECT_NAME}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND}
    ARGS -E copy "${RESOURCE_FOLDER}/${CMAKE_SYSTEM_NAME}/*.json" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)