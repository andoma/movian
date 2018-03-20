TOOLCHAIN_URL=https://movian.tv/static/ps3dev.tar.gz
TOOLCHAIN="${WORKINGDIR}/ps3dev"

cleanup() {
    echo "Cleaning up"
    exit 1
}

echo "Toolchain from: '${TOOLCHAIN_URL}' Local install in: ${TOOLCHAIN}"
if [ -d $TOOLCHAIN ]; then
    echo "Toolchain seems to exist"
else
    set +e
    trap cleanup SIGINT
    (
	set -eu
	cd ${WORKINGDIR}
	curl -L "${TOOLCHAIN_URL}" | tar xfz -
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
    --ps3dev=${TOOLCHAIN} --psl1ght=${TOOLCHAIN}/psl1ght \
    ${RELEASE} \
    ${VERSIONARGS} \
    --cleanbuild \
    ${USE_CCACHE} \
    --downloadcache="${WORKINGDIR}/downloadcache"

APPNAME=movian

make ${JARGS} BUILD=${TARGET} pkg self
artifact build.${TARGET}/${APPNAME}.self self application/octect-stream ${APPNAME}.self
artifact build.${TARGET}/${APPNAME}.pkg pkg application/octect-stream ${APPNAME}.pkg
artifact build.${TARGET}/${APPNAME}_geohot.pkg pkg application/octect-stream ${APPNAME}-gh.pkg
