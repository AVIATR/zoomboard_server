#!/bin/bash
set -e

function USAGE {
    echo "Usage: run.sh [-h] [-l logfile] stream"
}
#remove any old files
while getopts ":hl:" opt; do
    case ${opt} in
        h ) # process option h
            USAGE
            exit 0
            ;;
            
        l ) # process option t
            LOG_FILE=${OPTARG}
            ;;
    
        \? )
            echo "Invalid Option: -$OPTARG" 1>&2
            USAGE
            exit 1
            ;;
    esac
done
shift $((OPTIND -1))

if (($# == 0))
then
    echo "No stream specified"
    USAGE
    exit 1
fi
STREAM=$@

REGEX='(https?|ftp|file)://[-A-Za-z0-9\+&@#/%?=~_|!:,.;]*[-A-Za-z0-9\+&@#/%=~_|]'
if [[ $STREAM =~ $REGEX ]]
then #url
    #See if url is reachable
    RESPONSE=$(curl --write-out %{http_code} --silent --output /dev/null --head $STREAM)
    if [ $RESPONSE -ne "200" ]
    then
        echo "Stream not found at $STREAM"
        exit 1
    fi
elif [ ! -f $STREAM ] #regular file
then
    echo "Could not find file $STREAM"
    exit 1
fi
echo "Playing stream at $STREAM"
#launch nginx server
CMD="ffplay -fflags nobuffer -vf \"[in]drawtext=text='time=%{localtime}':box=1:x=(w-tw)/2:y=h-(4*lh), drawtext=text='frame=%{n}, ts=%{pts\:hms}':box=1:x=(w-tw)/2:y=h-(2*lh)\""
echo ${CMD}
if [ -z ${LOG_FILE} ]
then
    eval ${CMD} ${STREAM}
else
    eval FFREPORT=file=${LOG_FILE} ${CMD} -report ${STREAM}
fi