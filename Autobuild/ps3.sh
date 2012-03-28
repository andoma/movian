TOOLCHAIN_URL=https://github.com/andoma/ps3toolchain/tarball/496f539477230aeb56407bbd31a613ac7c0466e2
TOOLCHAIN_HASH=`echo $1 | sha1sum  | awk '{print $1}'`
TOOLCHAIN="${WORKINGDIR}/${TOOLCHAIN_HASH}"

cleanup() {
    echo "Cleaning up"
    rm -rf ${TOOLCHAIN}
    exit 1
}

export PS3DEV=${TOOLCHAIN}/ps3dev
export PATH=$PATH:$PS3DEV/bin:$PS3DEV/host/ppu/bin:$PS3DEV/host/spu/bin
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
	./toolchain.sh 1 2 3 4 5 11 12
    )

    STATUS=$?
    if [ $STATUS -ne 0 ]; then
	echo "Unable to stage toolchain"
	cleanup
    fi
    trap SIGINT
    set -e
fi


./configure.ps3 ${JARGS} --build=${TARGET} ${RELEASE}
make ${JARGS} BUILD=${TARGET} all pkg self
artifact build.${TARGET}/showtime.self self application/octect-stream showtime.self
artifact build.${TARGET}/showtime.pkg pkg application/octect-stream showtime.pkg
artifact build.${TARGET}/showtime_geohot.pkg pkg application/octect-stream showtime-gh.pkg
