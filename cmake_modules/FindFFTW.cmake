# - Find libfftw
#
# This module defines
#   FFTW_FOUND            - true if all of the following
#                            are located and set properly
#   LIBFFTW_INCLUDE_DIR  - libfftw include directory
#   LIBFFTW_LIBRARY      - libfftw library

find_package( PkgConfig )
if( PKG_CONFIG_FOUND )
    # If pkg-config finds libFFTW, this will set:
    #   PC_LIBFFTW_FOUND (to TRUE)
    #   PC_LIBFFTW_INCLUDEDIR
    #   PC_LIBFFTW_INCLUDE_DIRS
    #   PC_LIBFFTW_LIBDIR
    #   PC_LIBFFTW_LIBRARY_DIRS
    # These variables are then used as hints to find_path()
    # and find_library()
    pkg_search_module( PC_LIBFFTW libfftw )
endif( PKG_CONFIG_FOUND )

###############
## libFFTW #
###############

find_path( LIBFFTW_INCLUDE_DIR fftw3.h
    HINTS
        # Hints provided by pkg-config
        ${PC_LIBFFTW_INCLUDEDIR}
        ${PC_LIBFFTW_INCLUDEDIR}/*
        ${PC_LIBFFTW_INCLUDE_DIRS}
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
        libfftw
)

find_library( LIBFFTW_LIBRARY fftw3
    HINTS
        # Hints provided by pkg-config
        ${PC_LIBFFTW_LIBDIR}
        ${PC_LIBFFTW_LIBRARY_DIRS}
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
# Sets FFTW_FOUND to true if all of the following are set:
#   LIBFFTW_INCLUDE_DIR
#   LIBFFTW_LIBRARY

find_package_handle_standard_args( FFTW DEFAULT_MSG
    LIBFFTW_INCLUDE_DIR
    LIBFFTW_LIBRARY
)

if( FFTW_FOUND )
    message( STATUS "\tlibfftw: ${LIBFFTW_INCLUDE_DIR}, ${LIBFFTW_LIBRARY}" )
endif( FFTW_FOUND )

mark_as_advanced( LIBFFTW_LIBRARY LIBFFTW_INCLUDE_DIR )
