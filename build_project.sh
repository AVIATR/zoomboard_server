#!/bin/bash
export PROJ_NAME="zoomboard_server"
function usage {
    echo "Usage: $0 [-x] [-h] [-b config] [build_dir]"
    echo " -x               Create xcode project. By default, a make project is created."
    echo " -h               This help message."
    echo " -b config        Builds the project binaries after the project is built; config is either 'Release' or 'Debug'"
    echo " build_dir        Name of folder to put the build files in. Default is 'build'."
}

function error {
    usage
    exit 1
}

while getopts ":hxb:" opt; do
    case ${opt} in
        h )
            usage
            exit 0
            ;;
        x )
            ARGS+=(-G Xcode)
            ;;
        b )
            CONFIG=$OPTARG
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

ARGS+=(-S $(pwd) -B "${TARGET_DIR}")

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
mkdir "${TARGET_DIR}"

#Build project
echo "Configuring project..."
cmake "${ARGS[@]}"

#Build binaries if requested
if [ -n "$CONFIG" ]; then
    case "$CONFIG" in
        Release|Debug )
            echo "Building binaries..."
            cmake --build "$TARGET_DIR" --config $CONFIG -j4
            ;;
        * )
            echo "Unknown build configuration: $CONFIG" 1>&2
            error
            ;; 
    esac
fi
