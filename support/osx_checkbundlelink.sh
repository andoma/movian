#!/bin/sh

if [ "$1" = "" ] ; then
  echo "Usage: $0 binary-path"
  exit
fi

BINDIR=`dirname $1`

otool -L $1 | tail +2 | cut -f 1 -d " "  | while read LIB ; do
  echo "$LIB" | grep -qE "/usr/lib|/System/Library"
  if [ "$?" = "0" ] ; then
    continue
  fi

  echo "$LIB" | grep -qE "@loader_path/"
  if [ "$?" = "0" ] ; then
    if [ -e "${LIB/@loader_path/$BINDIR}" ] ; then
      continue 
    fi
  fi

  echo "ERROR: $1 is linked with non-system libraries or libraries outside bundle"
  echo "$LIB"
  exit 1
done

exit 0

