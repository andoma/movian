#!/bin/bash
#
# Entry point for the Doozer autobuild system
#
# (c) Andreas Öman 2011. All rights reserved.
#
#

set -eu

BUILD_API_VERSION=2
EXTRA_BUILD_NAME=""
JARGS=""
TARGET=""
RELEASE="--release"
while getopts "vht:e:j:" OPTION
do
  case $OPTION in
      v)
	  echo $BUILD_API_VERSION
	  exit 0
	  ;;
      h)
	  echo "This script is intended to be used by the autobuild system only"
	  exit 0
	  ;;
      t)
	  TARGET="$OPTARG"
	  ;;
      e)
	  EXTRA_BUILD_NAME="$OPTARG"
	  ;;
      j)
	  JARGS="--jobs=$OPTARG"
	  ;;
  esac
done


if [[ -z $TARGET ]]; then
    echo "target (-t) not specified"
    exit 1
fi

#
# $1 = local file path
# $2 = type
# $3 = content-type
# $4 = filename
#
artifact() {
    echo "doozer-artifact:$PWD/$1:$2:$3:$4"
}

case $TARGET in
    linux-all)
	./configure ${JARGS} --build=${TARGET} --enable-all ${RELEASE}
	make ${JARGS} BUILD=${TARGET}
	;;

    ps3)
	./configure.ps3 ${JARGS} --build=${TARGET} ${RELEASE}
	make ${JARGS} BUILD=${TARGET} all pkg self
	artifact build.${TARGET}/showtime.self bin application/octect-stream showtime.self
	artifact build.${TARGET}/showtime.pkg bin application/octect-stream showtime.pkg
	artifact build.${TARGET}/showtime_geohot.pkg bin application/octect-stream showtime-gh.pkg
	;;

    *)
	echo "target $TARGET not supported"
	exit 1
	;;
esac
