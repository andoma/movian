TOOLCHAIN_URL=https://github.com/andoma/ps3toolchain/tarball/3719cbe6bb2fb64d6a2ac2ac30a7da141a12e119
TOOLCHAIN_HASH=`echo ${TOOLCHAIN_URL}b | sha1sum  | awk '{print $1}'`
TOOLCHAIN="${WORKINGDIR}/${TOOLCHAIN_HASH}"

cleanup() {
    echo "Cleaning up"
    rm -rf ${TOOLCHAIN}
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
	PARALLEL=${JARGS} ./toolchain.sh 1 2 3 4 5 11 12 13
    )

    STATUS=$?
    if [ $STATUS -ne 0 ]; then
	echo "Unable to stage toolchain"
	cleanup
    fi
    trap SIGINT
    set -e
fi

./configure.ps3 ${JOBSARGS} --build=${TARGET} ${RELEASE} --cleanbuild --ccache
make ${JARGS} BUILD=${TARGET} pkg self
artifact build.${TARGET}/showtime.self self application/octect-stream showtime.self
artifact build.${TARGET}/showtime.pkg pkg application/octect-stream showtime.pkg
artifact build.${TARGET}/showtime_geohot.pkg pkg application/octect-stream showtime-gh.pkg

#on debian/ubuntu
#apt-get install git-core build-essential autoconf bison flex libelf-dev libtool pkg-config texinfo libncurses5-dev libz-dev python-dev libssl-dev libgmp3-dev ccache zip

