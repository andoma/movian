
build()
{
    TOOLCHAIN_URL=https://github.com/andoma/ps3toolchain/tarball/3fd10be8f90aad2f98bf51d43718980f63b19c0d
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


    make ${JARGS} BUILD=${TARGET} pkg self
    artifact build.${TARGET}/showtime.self self application/octect-stream showtime.self
    artifact build.${TARGET}/showtime.pkg pkg application/octect-stream showtime.pkg
    artifact build.${TARGET}/showtime_geohot.pkg pkg application/octect-stream showtime-gh.pkg
}

BUILD_DEPS="git-core build-essential autoconf bison flex libelf-dev libtool pkg-config texinfo libncurses5-dev libz-dev python-dev libssl-dev libgmp3-dev ccache zip curl wget"

deps()
{
    DISTID=`lsb_release -si`
    case "${DISTID}" in
	Ubuntu)
	    if [[ $EUID -ne 0 ]]; then
		echo "Build dependencies must be installed as root"
		exit 1
	    fi
	    apt-get --yes --force-yes install ${BUILD_DEPS}
	    ;;
	*)
	    echo "Don't know how to install deps on ${DISTID}"
	    exit 1
	    ;;
    esac
}

buildenv()
{
    echo ${BUILD_DEPS} | sha1sum | awk '{print $1}'
}

eval $OP
