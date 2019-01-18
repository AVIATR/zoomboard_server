#!/bin/bash
export PROJ_NAME="rtmp_server"
function usage {
    echo "Usage: $0 [-x|--xcode] [-h|--help] [-b|--build] [build_dir]"
    echo " -x               Create xcode project. By default, a make project is created."
    echo " -h               This help message."
    echo " -b               Builds the project binaries after the project is built"
    echo " build_dir        Name of folder to put the build files in. Default is 'build'."
}

if [ -z "$1" ]; then
    usage
    exit 1
fi
TARGET_DIR='build'

ARGS=(-DBUILD_SHARED_LIBS=YES)
while [[ $# -gt 0 ]]
do
key="$1"
case $key in
    -h|--help)
        usage
        exit 0
        ;;
    -x|--xcode)
        IS_XCODE=yes
        ARGS+=(-G Xcode)
        shift #past switch
        ;;
    -b|--build)
        ARGS+=(--build)
        echo "Project binaries will be built."
        shift #past switch
        ;;
    *)    # unknown option
        TARGET_DIR="$1" # save it in an array for later
        shift # past argument
        ;;
esac
done

ARGS+=(-S $(pwd) -B "${TARGET_DIR}")
echo CMAKE ARGS = "${ARGS[@]}"

if [[ -e "${TARGET_DIR}" ]]; then
    read -p "${TARGET_DIR} exists, and will be removed. Are you sure? " CHOICE
    echo    # (optional) move to a new line
    if [[ $CHOICE =~ ^[Yy]$ ]]; then
        rm -rf "${TARGET_DIR}"
    else
        echo "Exiting. Project has not been built."
        exit
    fi
fi
mkdir "${TARGET_DIR}"

if [ "${IS_XCODE}" = yes ]; then
    echo "Building Xcode project in ${TARGET_DIR}"
else
    echo "Building make project in ${TARGET_DIR}"
fi
cmake "${ARGS[@]}"
