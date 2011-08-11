#!/bin/sh
revision=`cd "$1" && cat debian/changelog |head -1|grep -v current`
test "$revision" && revision=`cd "$1" && cat debian/changelog |head -1|cut -f2 -d' '|sed s/\(//|sed s/\)//`

if ! test $revision; then
    test $revision || revision=`cd "$1" && git describe --dirty --abbrev=5 2>/dev/null | sed  -e 's/-/./g'`
fi

if ! test $revision; then
    test $revision || revision=`cd "$1" && git describe --abbrev=5 2>/dev/null | sed  -e 's/-/./g'`
fi

echo $revision
