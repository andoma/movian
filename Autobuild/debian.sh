CHANGELOG=debian/changelog

NOW=`date -R`

echo >${CHANGELOG} "showtime (${VERSION}) unstable; urgency=low"
echo >>${CHANGELOG}
echo >>${CHANGELOG} "  * The full changelog can be found at "
echo >>${CHANGELOG} "    http://www.lonelycoder.com/showtime/download"
echo >>${CHANGELOG}
echo >>${CHANGELOG} " -- Andreas Öman <andreas@lonelycoder.com>  ${NOW}"

export JARGS
export ARCH

if ccache=$(which ccache); then
    echo "Using ccache"
    ccache -s
    USE_CCACHE="--ccache"
else
    USE_CCACHE=""
fi

export USE_CCACHE

dpkg-buildpackage -b -us -uc

for a in ../showtime*${VERSION}*.deb; do
    versioned_artifact "$a" deb application/x-deb `basename $a`
done

#for a in ../showtime*${VERSION}*.changes; do
#    versioned_artifact "$a" changes text/plain `basename $a`
#done

