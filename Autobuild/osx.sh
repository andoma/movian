set +e
which ccache >/dev/null
if [ $? -eq 0 ]; then
    echo "Using ccache"
    ccache -s
    export CCACHE_CPP2=yes
    USE_CCACHE="--ccache"
else
    USE_CCACHE=""
fi
set -e

set -x
./configure.osx ${RELEASE} --cleanbuild ${USE_CCACHE} \
    --downloadcache="${WORKINGDIR}/downloadcache"

set +x
make ${JARGS} dist
artifact build.osx/Movian.zip zip application/zip Movian.zip
artifact build.osx/movian.bin bin application/octet-stream Movian.bin
