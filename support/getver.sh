#!/bin/sh
revision=`cd "$1" && cat debian/changelog |head -1|grep -v current`
test "$revision" && revision=`cd "$1" && cat debian/changelog |head -1|cut -f2 -d' '|sed s/\(//|sed s/\)//`

if ! test $revision; then
    revision=`cd "$1" && LC_ALL=C svn info 2> /dev/null | grep Revision | cut -d' ' -f2`
    test $revision || revision=`cd "$1" && grep revision .svn/entries 2>/dev/null | cut -d '"' -f2`
    test $revision || revision=`cd "$1" && sed -n -e '/^dir$/{n;p;q}' .svn/entries 2>/dev/null`
    test $revision && revision=SVN-r$revision
fi

echo $revision
