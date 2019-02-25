# - Find libavcodec and libavformat
# Find the libavcodec and libavformat include directories
# and libraries
#
# This module defines
#   FFMPEG_FOUND            - true if all of the following
#                            are located and set properly
#   LIBAVCODEC_INCLUDE_DIR  - libavcodec include directory
#   LIBAVCODEC_LIBRARY      - libavcodec library
#   LIBAVDEVICE_INCLUDE_DIR - libavdevice include directory
#   LIBAVDEVICE_LIBRARY     - libavdevice library
#   LIBAVFORMAT_INCLUDE_DIR - libavformat include directory
#   LIBAVFORMAT_LIBRARY     - libavformat library
#   LIBAVUTIL_INCLUDE_DIR   - libavutil include directory
#   LIBAVUTIL_LIBRARY       - libavutil library
#   LIBAVFILTER_INCLUDE_DIR - libavfilter include directory
#   LIBAVFILTER_LIBRARY     - libavfilter library
#   LIBSWSCALE_INCLUDE_DIR  - libswscale include directory
#   LIBSWSCALE_LIBRARY      - libswscale library
#   LIBSWRESAMPLE_INCLUDE_DIR libswresample include directory
#   LIBSWRESAMPLE_LIBRARY   - libswresample library

find_package( PkgConfig )
if( PKG_CONFIG_FOUND )
    # If pkg-config finds libavcodec, this will set:
    #   PC_LIBAVCODEC_FOUND (to TRUE)
    #   PC_LIBAVCODEC_INCLUDEDIR
    #   PC_LIBAVCODEC_INCLUDE_DIRS
    #   PC_LIBAVCODEC_LIBDIR
    #   PC_LIBAVCODEC_LIBRARY_DIRS
    #   LIBAVUTIL_INCLUDE_DIR
    #   LIBAVUTIL_LIBRARY
    #   LIBAVFILTER_INCLUDE_DIR
    #   LIBAVFILTER_LIBRARY
    #   LIBSWSCALE_INCLUDE_DIR
    #   LIBSWSCALE_LIBRARY
    #   LIBSWRESAMPLE_INCLUDE_DIR
    #   LIBSWRESAMPLE_LIBRARY
    # These variables are then used as hints to find_path()
    # and find_library()
    pkg_search_module( PC_LIBAVCODEC libavcodec )

    # Same as above, but searching for the remaining modules instead
    pkg_search_module( PC_LIBAVDEVICE libavdevice )
    pkg_search_module( PC_LIBAVFORMAT libavformat )
    pkg_search_module( PC_LIBAVUTIL libavutil )
    pkg_search_module( PC_LIBAVFILTER libavfilter )
    pkg_search_module( PC_LIBSWSCALE libswscale )
    pkg_search_module( PC_LIBSWRESAMPLE libswresample )

endif( PKG_CONFIG_FOUND )

###############
## libavcodec #
###############

find_path( LIBAVCODEC_INCLUDE_DIR libavcodec/avcodec.h
    HINTS
        # Hints provided by pkg-config
        ${PC_LIBAVCODEC_INCLUDEDIR}
        ${PC_LIBAVCODEC_INCLUDEDIR}/*
        ${PC_LIBAVCODEC_INCLUDE_DIRS}
    PATHS
        # Standard include directories
        /usr/include/
        ~/usr/include/
        /opt/local/include/
        /usr/local/include/
        /opt/kde4/include/
        ${KDE4_INCLUDE_DIR}/
        # Search all subdirs of the above
        /usr/include/*
        ~/usr/include/*
        /opt/local/include/*
        /usr/local/include/*
        /opt/kde4/include/*
        ${KDE4_INCLUDE_DIR}/*
    PATH_SUFFIXES
        # Subdirectory hints
        libavcodec
        ffmpeg
        ffmpeg/libavcodec
)

find_library( LIBAVCODEC_LIBRARY avcodec
    HINTS
        # Hints provided by pkg-config
        ${PC_LIBAVCODEC_LIBDIR}
        ${PC_LIBAVCODEC_LIBRARY_DIRS}
    PATHS
        ~/usr/lib/
        /opt/local/lib/
        /usr/lib/
        /usr/lib64/
        /usr/local/lib/
        /opt/kde4/lib/
        ${KDE4_LIB_DIR}
)

###############
## libavdevice#
###############

find_path( LIBAVDEVICE_INCLUDE_DIR libavdevice/avdevice.h
    HINTS
        # Hints provided by pkg-config
        ${PC_LIBAVDEVICE_INCLUDEDIR}
        ${PC_LIBAVDEVICE_INCLUDEDIR}/*
        ${PC_LIBAVDEVICE_INCLUDE_DIRS}
    PATHS
        # Standard include directories
        /usr/include/
        ~/usr/include/
        /opt/local/include/
        /usr/local/include/
        /opt/kde4/include/
        ${KDE4_INCLUDE_DIR}/
        # Search all subdirs of the above
        /usr/include/*
        ~/usr/include/*
        /opt/local/include/*
        /usr/local/include/*
        /opt/kde4/include/*
        ${KDE4_INCLUDE_DIR}/*
    PATH_SUFFIXES
        # Subdirectory hints
        libavdevice
        ffmpeg
        ffmpeg/libavdevice
)

find_library( LIBAVDEVICE_LIBRARY avdevice
    HINTS
        # Hints provided by pkg-config
        ${PC_LIBAVDEVICE_LIBDIR}
        ${PC_LIBAVDEVICE_LIBRARY_DIRS}
    PATHS
        ~/usr/lib/
        /opt/local/lib/
        /usr/lib/
        /usr/lib64/
        /usr/local/lib/
        /opt/kde4/lib/
        ${KDE4_LIB_DIR}
)

################
## libavformat #
################

find_path( LIBAVFORMAT_INCLUDE_DIR libavformat/avformat.h
    HINTS
        # Hints provided by pkg-config
        ${PC_LIBAVFORMAT_INCLUDEDIR}
        ${PC_LIBAVFORMAT_INCLUDEDIR}/*
        ${PC_LIBAVFORMAT_INCLUDE_DIRS}
    PATHS
        # Standard include directories
        /usr/include/
        ~/usr/include/
        /opt/local/include/
        /usr/local/include/
        /opt/kde4/include/
        ${KDE4_INCLUDE_DIR}/
        # Search all subdirs of the above
        /usr/include/*
        ~/usr/include/*
        /opt/local/include/*
        /usr/local/include/*
        /opt/kde4/include/*
        ${KDE4_INCLUDE_DIR}/*
    PATH_SUFFIXES
        # Subdirectory hints
        libavformat
        ffmpeg
        ffmpeg/libavformat
)

find_library( LIBAVFORMAT_LIBRARY avformat
    HINTS
        # Hints provided by pkg-config
        ${PC_LIBAVFORMAT_LIBDIR}
        ${PC_LIBAVFORMAT_LIBRARY_DIRS}
    PATHS
        ~/usr/lib/
        /opt/local/lib/
        /usr/lib/
        /usr/lib64/
        /usr/local/lib/
        /opt/kde4/lib/
        ${KDE4_LIB_DIR}
)

################
## libavfilter #
################

find_path( LIBAVFILTER_INCLUDE_DIR libavfilter/avfilter.h
    HINTS
        # Hints provided by pkg-config
        ${PC_LIBAVFILTER_INCLUDEDIR}
        ${PC_LIBAVFILTER_INCLUDEDIR}/*
        ${PC_LIBAVFILTER_INCLUDE_DIRS}
    PATHS
        # Standard include directories
        /usr/include/
        ~/usr/include/
        /opt/local/include/
        /usr/local/include/
        /opt/kde4/include/
        ${KDE4_INCLUDE_DIR}/
        # Search all subdirs of the above
        /usr/include/*
        ~/usr/include/*
        /opt/local/include/*
        /usr/local/include/*
        /opt/kde4/include/*
        ${KDE4_INCLUDE_DIR}/*
    PATH_SUFFIXES
        # Subdirectory hints
        libavfilter
        ffmpeg
        ffmpeg/libavfilter
)

find_library( LIBAVFILTER_LIBRARY avfilter
    HINTS
        # Hints provided by pkg-config
        ${PC_LIBAVFILTER_LIBDIR}
        ${PC_LIBAVFILTER_LIBRARY_DIRS}
    PATHS
        ~/usr/lib/
        /opt/local/lib/
        /usr/lib/
        /usr/lib64/
        /usr/local/lib/
        /opt/kde4/lib/
        ${KDE4_LIB_DIR}
)

################
## libavutil   #
################

find_path( LIBAVUTIL_INCLUDE_DIR libavutil/avutil.h
    HINTS
        # Hints provided by pkg-config
        ${PC_LIBAVUTIL_INCLUDEDIR}
        ${PC_LIBAVUTIL_INCLUDEDIR}/*
        ${PC_LIBAVUTIL_INCLUDE_DIRS}
    PATHS
        # Standard include directories
        /usr/include/
        ~/usr/include/
        /opt/local/include/
        /usr/local/include/
        /opt/kde4/include/
        ${KDE4_INCLUDE_DIR}/
        # Search all subdirs of the above
        /usr/include/*
        ~/usr/include/*
        /opt/local/include/*
        /usr/local/include/*
        /opt/kde4/include/*
        ${KDE4_INCLUDE_DIR}/*
    PATH_SUFFIXES
        # Subdirectory hints
        libavutil
        ffmpeg
        ffmpeg/libavutil
)

find_library( LIBAVUTIL_LIBRARY avutil
    HINTS
        # Hints provided by pkg-config
        ${PC_LIBAVUTIL_LIBDIR}
        ${PC_LIBAVUTIL_LIBRARY_DIRS}
    PATHS
        ~/usr/lib/
        /opt/local/lib/
        /usr/lib/
        /usr/lib64/
        /usr/local/lib/
        /opt/kde4/lib/
        ${KDE4_LIB_DIR}
)

################
## libswscale  #
################

find_path( LIBSWSCALE_INCLUDE_DIR libswscale/swscale.h
    HINTS
        # Hints provided by pkg-config
        ${PC_LIBSWSCALE_INCLUDEDIR}
        ${PC_LIBSWSCALE_INCLUDEDIR}/*
        ${PC_LIBSWSCALE_INCLUDE_DIRS}
    PATHS
        # Standard include directories
        /usr/include/
        ~/usr/include/
        /opt/local/include/
        /usr/local/include/
        /opt/kde4/include/
        ${KDE4_INCLUDE_DIR}/
        # Search all subdirs of the above
        /usr/include/*
        ~/usr/include/*
        /opt/local/include/*
        /usr/local/include/*
        /opt/kde4/include/*
        ${KDE4_INCLUDE_DIR}/*
    PATH_SUFFIXES
        # Subdirectory hints
        libswscale
        ffmpeg
        ffmpeg/libswscale
)

find_library( LIBSWSCALE_LIBRARY swscale
    HINTS
        # Hints provided by pkg-config
        ${PC_LIBSWSCALE_LIBDIR}
        ${PC_LIBSWSCALE_LIBRARY_DIRS}
    PATHS
        ~/usr/lib/
        /opt/local/lib/
        /usr/lib/
        /usr/lib64/
        /usr/local/lib/
        /opt/kde4/lib/
        ${KDE4_LIB_DIR}
)

###################
## libswresample  #
###################

find_path( LIBSWRESAMPLE_INCLUDE_DIR libswresample/swresample.h
    HINTS
        # Hints provided by pkg-config
        ${PC_LIBSWRESAMPLE_INCLUDEDIR}
        ${PC_LIBSWRESAMPLE_INCLUDEDIR}/*
        ${PC_LIBSWRESAMPLE_INCLUDE_DIRS}
    PATHS
        # Standard include directories
        /usr/include/
        ~/usr/include/
        /opt/local/include/
        /usr/local/include/
        /opt/kde4/include/
        ${KDE4_INCLUDE_DIR}/
        # Search all subdirs of the above
        /usr/include/*
        ~/usr/include/*
        /opt/local/include/*
        /usr/local/include/*
        /opt/kde4/include/*
        ${KDE4_INCLUDE_DIR}/*
    PATH_SUFFIXES
        # Subdirectory hints
        libswresample
        ffmpeg
        ffmpeg/libswresample
)

find_library( LIBSWRESAMPLE_LIBRARY swresample
    HINTS
        # Hints provided by pkg-config
        ${PC_LIBSWRESAMPLE_LIBDIR}
        ${PC_LIBSWRESAMPLE_LIBRARY_DIRS}
    PATHS
        ~/usr/lib/
        /opt/local/lib/
        /usr/lib/
        /usr/lib64/
        /usr/local/lib/
        /opt/kde4/lib/
        ${KDE4_LIB_DIR}
)

include( FindPackageHandleStandardArgs )
# Sets FFMPEG_FOUND to true if all of the following are set:
#   LIBAVCODEC_INCLUDE_DIR
#   LIBAVCODEC_LIBRARY
#   LIBAVDEVICE_INCLUDE_DIR
#   LIBAVDEVICE_LIBRARY
#   LIBAVFORMAT_INCLUDE_DIR
#   LIBAVFORMAT_LIBRARY
#   LIBAVUTIL_INCLUDE_DIR
#   LIBAVUTIL_LIBRARY
#   LIBAVFILTER_INCLUDE_DIR
#   LIBAVFILTER_LIBRARY
#   LIBSWSCALE_INCLUDE_DIR
#   LIBSWSCALE_LIBRARY
#   LIBSWRESAMPLE_INCLUDE_DIR
#   LIBSWRESAMPLE_LIBRARY
find_package_handle_standard_args( FFmpeg DEFAULT_MSG
    LIBAVCODEC_INCLUDE_DIR
    LIBAVCODEC_LIBRARY
    LIBAVDEVICE_INCLUDE_DIR
    LIBAVDEVICE_LIBRARY
    LIBAVFORMAT_INCLUDE_DIR
    LIBAVFORMAT_LIBRARY
    LIBAVUTIL_INCLUDE_DIR
    LIBAVUTIL_LIBRARY
    LIBAVFILTER_INCLUDE_DIR
    LIBAVFILTER_LIBRARY
    LIBSWSCALE_INCLUDE_DIR
    LIBSWSCALE_LIBRARY
    LIBSWRESAMPLE_INCLUDE_DIR
    LIBSWRESAMPLE_LIBRARY
)
if( FFMPEG_FOUND )
    message( STATUS "\tlibavcodec:\t${LIBAVCODEC_INCLUDE_DIR}, ${LIBAVCODEC_LIBRARY}" )
    message( STATUS "\tlibavdevice:\t${LIBAVDEVICE_INCLUDE_DIR}, ${LIBAVDEVICE_LIBRARY}" )
    message( STATUS "\tlibavformat:\t${LIBAVFORMAT_INCLUDE_DIR}, ${LIBAVFORMAT_LIBRARY}" )
    message( STATUS "\tlibavutil:\t${LIBAVUTIL_INCLUDE_DIR}, ${LIBAVUTIL_LIBRARY}" )
    message( STATUS "\tlibavfilter:\t${LIBAVFILTER_INCLUDE_DIR}, ${LIBAVFILTER_LIBRARY}" )
    message( STATUS "\tlibswscale:\t${LIBSWSCALE_INCLUDE_DIR}, ${LIBSWSCALE_LIBRARY}" )
    message( STATUS "\tlibswresample:\t${LIBSWRESAMPLE_INCLUDE_DIR}, ${LIBSWRESAMPLE_LIBRARY}" )
endif( FFMPEG_FOUND )

set(FFMPEG_INCLUDE_DIRS ${LIBAVCODEC_INCLUDE_DIR} ${LIBAVDEVICE_INCLUDE_DIR} ${LIBAVFORMAT_INCLUDE_DIR} ${LIBAVUTIL_INCLUDE_DIR} ${LIBAVFILTER_INCLUDE_DIR} ${LIBSWSCALE_INCLUDE_DIR})
list(REMOVE_DUPLICATES FFMPEG_INCLUDE_DIRS)
set(FFMPEG_LIBRARIES ${LIBAVCODEC_LIBRARY} ${LIBAVDEVICE_LIBRARY} ${LIBAVFORMAT_LIBRARY} ${LIBAVUTIL_LIBRARY} ${LIBAVFILTER_LIBRARY} ${LIBSWSCALE_LIBRARY} ${LIBSWRESAMPLE_LIBRARY})

mark_as_advanced( 
    LIBAVCODEC_LIBRARY LIBAVCODEC_INCLUDE_DIR
    LIBAVDEVICE_LIBRARY LIBAVDEVICE_INCLUDE_DIR 
    LIBAVFORMAT_LIBRARY LIBAVFORMAT_INCLUDE_DIR
    LIBAVUTIL_LIBRARY LIBAVUTIL_INCLUDE_DIR
    LIBAVFILTER_LIBRARY LIBAVFILTER_INCLUDE_DIR
    LIBSWSCALE_LIBRARY LIBSWSCALE_INCLUDE_DIR 
    LIBSWRESAMPLE_LIBRARY LIBSWRESAMPLE_INCLUDE_DIR
    FFMPEG_INCLUDE_DIRS FFMPEG_LIBRARIES
)
