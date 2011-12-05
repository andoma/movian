#!/bin/bash
#
# Entry point for the Doozer autobuild system
#
# (c) Andreas Öman 2011. All rights reserved.
#
#

set -eu

BUILD_API_VERSION=1
EXTRA_BUILD_NAME=""
JARGS=""
TARGET=""

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

case $TARGET in
    linux-all)
	./configure ${JARGS} --build=${TARGET} --enable-all
	make ${JARGS} BUILD=${TARGET}
	;;
    *)
	echo "target $TARGET not supported"
	exit 1
	;;
esac
