TOOLCHAIN_URL=https://github.com/andoma/ps3toolchain/tarball/481515bbfbfbe22f775ff5d42095be75eccc7373
TOOLCHAIN_HASH=`echo ${TOOLCHAIN_URL} | sha1sum  | awk '{print $1}'`
TOOLCHAIN="${WORKINGDIR}/${TOOLCHAIN_HASH}"

cleanup() {
    echo "Cleaning up"
    rm -rf ${TOOLCHAIN}.broken
    mv ${TOOLCHAIN} ${TOOLCHAIN}.broken
    exit 1
}

export PS3DEV=${TOOLCHAIN}/ps3dev
export PSL1GHT=${TOOLCHAIN}/PSL1GHT
export PATH=$PATH:$PS3DEV/bin:$PS3DEV/host/ppu/bin:$PS3DEV/host/spu/bin
export PATH=$PATH:$PSL1GHT/host/bin

echo "Toolchain from: '${TOOLCHAIN_URL}' Local install in: ${TOOLCHAIN}"
if [ -d $TOOLCHAIN ]; then
    echo "Toolchain seems to exist"
else
    set +e
    trap cleanup SIGINT
    (
	set -eu
	mkdir -p ${TOOLCHAIN}
	cd ${TOOLCHAIN}
	curl -L "${TOOLCHAIN_URL}" | tar xfz -
	cd *
	PARALLEL=${JARGS} ./toolchain.sh 1 2 3 4 5 11 12
    )

    STATUS=$?
    if [ $STATUS -ne 0 ]; then
	echo "Unable to stage toolchain"
	cleanup
    fi
    trap SIGINT
    set -e
fi

which ccache >/dev/null
if [ $? -eq 0 ]; then
    echo "Using ccache"
    ccache -s
    USE_CCACHE="--ccache"
else
    USE_CCACHE=""
fi

./configure.ps3 --build=${TARGET} \
    ${RELEASE} \
    --cleanbuild \
    ${USE_CCACHE} \
    --downloadcache="${WORKINGDIR}/downloadcache"

APPNAME=movian

make ${JARGS} BUILD=${TARGET} pkg self
artifact build.${TARGET}/${APPNAME}.self self application/octect-stream ${APPNAME}.self
artifact build.${TARGET}/${APPNAME}.pkg pkg application/octect-stream ${APPNAME}.pkg
artifact build.${TARGET}/${APPNAME}_geohot.pkg pkg application/octect-stream ${APPNAME}-gh.pkg
