# Define sources and targets
# Set version information in a version.h file
message(STATUS "Current source dir: ${CMAKE_CURRENT_SOURCE_DIR}")
message(STATUS "Current binary dir: ${CMAKE_CURRENT_BINARY_DIR}")
configure_file(
    "version.h.in"
    "include/version.h"
    )
include_directories("${CMAKE_CURRENT_BINARY_DIR}/include")
#Get files
file(GLOB SOURCE_FILES "*.cxx" "*.cpp")
file(GLOB HEADER_FILES "*.h" "*.hpp" "*.ipp" "${CMAKE_CURRENT_BINARY_DIR}/include/*.h")
#Set dependenies
set(TARGET_NAME ${PROJECT_NAME})
add_executable(${TARGET_NAME} ${HEADER_FILES} ${SOURCE_FILES})
set_property(TARGET ${TARGET_NAME} PROPERTY CXX_STANDARD 14)
set_property(TARGET ${TARGET_NAME} PROPERTY CXX_STANDARD_REQUIRED ON)
set(INCLUDE_DIRS ${FFMPEG_INCLUDE_DIRS} ${Boost_INCLUDE_DIRS} ${LOG4CXX_INCLUDE_DIRS} ${HEADERS_DIR})
list(REMOVE_DUPLICATES INCLUDE_DIRS)
include_directories(${INCLUDE_DIRS})
set(DEPENDENCIES ${FFMPEG_LIBRARIES} ${Boost_LIBRARIES} ${LOG4CXX_LIBRARIES})
target_link_libraries(${TARGET_NAME} ${DEPENDENCIES})
message(STATUS "${TARGET_NAME} Dependencies = ${HEADER_FILES};${SOURCE_FILES};${DEPENDENCIES}" )
message(STATUS "${TARGET_NAME} Include Dirs = ${INCLUDE_DIRS}")
get_target_property(LINKED_LIBS ${TARGET_NAME} INTERFACE_LINK_LIBRARIES)
message(STATUS "${TARGET} Linked Libraries: ${LINKED_LIBS}" )
set_target_properties( ${TARGET_NAME}
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )	
# Install files
message(STATUS "Files will be installed to ${CMAKE_INSTALL_PREFIX}")
install(FILES ${HEADER_FILES} DESTINATION include)
install(TARGETS ${TARGET_NAME} RUNTIME DESTINATION bin)
