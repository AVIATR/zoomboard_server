#!/bin/bash
export PROJ_NAME="zoomboard_server"
function usage {
    echo "Usage: $0 [-h] [-x] [-c config] [-w www_dir] [-i install_dir] [build_dir]"
    echo " -h               This help message."
    echo " -x               Create xcode project. By default, a make project is created."
    echo " -w www_dir.      Sets the root folder where web-served files will be created. Streams will be in www_dir/hls. Default is /tmp/zoombrd"
    echo " -c config        Builds the project binaries after the project is built; config is either 'Release' or 'Debug'"
    echo " -i install_dir   Builds the binaries after the project is configured & installs the outputs in install_dir"
    echo " build_dir        Name of folder to put the build files in. Default is 'build'."
}

function error {
    usage
    exit 1
}

CONFIG=Debug

while getopts ":hxc:i:" opt; do
    case ${opt} in
        h )
            usage
            exit 0
            ;;
        x )
            ARGS+=(-G Xcode)
            ;;
        c )
            CONFIG=$OPTARG
            ;;
        w )
            WWW_DIR=$OPT_ARG
            ARGS+=(-DWWW_DIR=${WWW_DIR})
            ;;
        i )
            INSTALL_DIR=$OPTARG
            echo "Setting installation folder to ${INSTALL_DIR}."
            ARGS+=(-DCMAKE_INSTALL_PREFIX=${INSTALL_DIR})
            ;;
        \? )
            echo "Invalid option: $OPTARG" 1>&2
            error
            ;;
        : )
            echo "Invalid option: $OPTARG requires an argument" 1>&2
            error
            ;;
    esac
done
shift $((OPTIND -1))

if [ ${#@} -gt 1 ]; then
    echo "Multiple arguments provided for build dir: $@" 1>&2
    error
elif [ -z "$@" ]; then
    TARGET_DIR='build'
else
    TARGET_DIR="$@"    
fi

ARGS+=(-S $(pwd) -B "${TARGET_DIR}" -DCMAKE_BUILD_TYPE=${CONFIG})

#Cleanup or create target folder
if [[ -e "${TARGET_DIR}" ]]; then
    read -p "${TARGET_DIR} exists, and will be removed. Are you sure (y/n)? " CHOICE
    echo    #move to a new line
    if [[ $CHOICE =~ ^[Yy]$ ]]; then
        rm -rf "${TARGET_DIR}"
    else
        echo "Exiting. Project has not been built."
        exit 0
    fi
fi
mkdir -p "${TARGET_DIR}"

#Build project
echo "Configuring project..."
cmake "${ARGS[@]}"

#Build & install binaries if requested
if [ -n ${INSTALL_DIR} ]; then
    case "$CONFIG" in
        Release|Debug )
            echo "Building binaries..."
            cmake --build "$TARGET_DIR" --config $CONFIG -j4 --target install
            ;;
        * )
            echo "Unknown build configuration: $CONFIG" 1>&2
            error
            ;; 
    esac
fi
