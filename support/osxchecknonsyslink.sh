#!/bin/sh

LIBS=`otool -L $1 | tail +2 | grep -vE "/usr/lib|/System/Library"`

if [ "$LIBS" = "" ] ; then
  exit 0
fi

echo "ERROR: $1 is linked with non-system libraries"
echo "$LIBS"
exit 1

