CHANGELOG=debian/changelog
NOW=`date -R`
VER=`git describe | sed "s/\([0-9]*\)\.\([0-9]*\)-\([0-9]*\)-.*/\1.\2.\3/"`
echo >${CHANGELOG} "showtime (${VER}) unstable; urgency=low"
echo >>${CHANGELOG}
echo >>${CHANGELOG} "  * The full changelog can be found at "
echo >>${CHANGELOG} "    http://www.lonelycoder.com/showtime/download"
echo >>${CHANGELOG}
echo >>${CHANGELOG} " -- Andreas Öman <andreas@lonelycoder.com>  ${NOW}"

export JOBSARGS
export JARGS
dpkg-buildpackage -b -us -uc

for a in ../showtime*${VER}*.deb; do
    artifact $a deb application/x-deb `basename $a`
    rm -f $a
done

for a in ../showtime*${VER}*.changes; do
    artifact $a changes text/plain `basename $a`
    rm -f $a
done

rm -f ${CHANGELOG}
dh_clean
