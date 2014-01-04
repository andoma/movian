
build()
{
    mkdir -p ${WORKINGDIR}

    TOOLCHAIN_HASH=376766b6504cd4da67f87ad213ac730230e4ee1e
    TOOLCHAIN_URL=https://djo0w2q39wuyp.cloudfront.net/file/${TOOLCHAIN_HASH}
    TOOLCHAIN="${WORKINGDIR}/${TOOLCHAIN_HASH}"

    cleanup() {
	echo "Cleaning up"
	rm -rf "${TOOLCHAIN}"
	exit 1
    }
    
    echo "Toolchain from: '${TOOLCHAIN_URL}' Local install in: ${TOOLCHAIN}"
    if [ -d "${TOOLCHAIN}" ]; then
	echo "Toolchain seems to exist"
    else
	set +e
	trap cleanup SIGINT
	(
	    set -eu
	    mkdir -p ${TOOLCHAIN}
	    cd ${TOOLCHAIN}
	    curl -L "${TOOLCHAIN_URL}" | tar xfj -
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

    set -x

    ./configure.meson --build=${TARGET} \
	--toolchain="${TOOLCHAIN}/host/usr/bin/arm-none-linux-gnueabi-" \
	--sysroot="${TOOLCHAIN}/host/usr/arm-buildroot-linux-gnueabi/sysroot" \
        --mksquashfs="${TOOLCHAIN}/host/usr/bin/mksquashfs" \
	${RELEASE} \
	--cleanbuild \
	${USE_CCACHE} \
        --enable-magneto \
        --disable-spidermonkey


    make ${JARGS} BUILD=${TARGET} squashfs

    artifact build.${TARGET}/showtime.sqfs sqfs application/octet-stream showtime.sqfs

}

deps()
{
    DISTID=`lsb_release -si`
    case "${DISTID}" in
	Ubuntu)
	    if [[ $EUID -ne 0 ]]; then
		echo "Build dependencies must be installed as root"
		exit 1
	    fi
#	    apt-get install git-core build-essential autoconf bison flex libelf-dev libtool pkg-config texinfo libncurses5-dev libz-dev python-dev libssl-dev libgmp3-dev ccache zip
	    ;;
	*)
	    echo "Don't know how to install deps on ${DISTID}"
	    exit 1
	    ;;
    esac
}


eval $OP
