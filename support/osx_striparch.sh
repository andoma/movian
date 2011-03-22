#!/bin/bash

if [ "$2" = "" ] ; then
  echo "Usage: $0 binary-for-archs binary-to-strip"
  exit
fi

ARCHS=""
while read l ; do
  echo $l | grep -q "architectures"
  if [ "$?" = "0" ] ; then
    continue
  fi

  ARCH=`echo $l | sed 's/.*executable \(.*\)/\1/'`
  ARCHS="$ARCH $ARCHS"
done < <(file $1)

i=0
for arch in $ARCHS ; do
  a[$i]=-extract
  a[$(($i+1))]=$arch
  i=$((i+2))
done
a[$i]=-output
a[$(($i+1))]=$2
a[$(($i+2))]=$2

lipo ${a[@]}


