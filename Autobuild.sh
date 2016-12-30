#!/bin/bash
#
# Entry point for the Doozer autobuild system
#
# (c) Andreas Öman 2011. All rights reserved.
#
#

set -eu

JARGS=""
TARGET=""
RELEASE="--release"
WORKINGDIR="/var/tmp/showtime-autobuild"
while getopts "t:j:w:v:" OPTION
do
  case $OPTION in
      t)
	  TARGET="$OPTARG"
	  ;;
      j)
	  JARGS="-j$OPTARG"
	  ;;
      w)
	  WORKINGDIR="$OPTARG"
	  ;;
      v)
	  VERSION="$OPTARG"
	  VERSIONARGS="--version=$OPTARG"
	  ;;
  esac
done

if [[ -z $TARGET ]]; then
    echo "target (-t) not specified"
    exit 1
fi


echo "VERSION: $VERSION"
if [ "$VERSION" = "unset" ] ; then
    VERSION="0.0.0"
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

versioned_artifact() {
    echo "doozer-versioned-artifact:$PWD/$1:$2:$3:$4"
}

if [ -f Autobuild/${TARGET}.sh ]; then
    source Autobuild/${TARGET}.sh
else
    echo "target $TARGET not supported"
    exit 1
fi

