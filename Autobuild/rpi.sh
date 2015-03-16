
build()
{
    mkdir -p ${WORKINGDIR}

    STOSVERSION="stos-host-2.0.1.gb0b51"

    TOOLCHAIN_URL=https://movian.tv/static/${STOSVERSION}.tar.bz2
    TOOLCHAIN="${WORKINGDIR}/${STOSVERSION}"

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
	    cd "${WORKINGDIR}"
            mkdir -p "${STOSVERSION}"
            cd  "${STOSVERSION}"
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

    set +e
    which ccache >/dev/null
    if [ $? -eq 0 ]; then
	echo "Using ccache"
	ccache -s
	USE_CCACHE="--ccache"
    else
	USE_CCACHE=""
    fi
    set -e

    set -x
    ./configure.rpi --build=${TARGET} \
	--toolchain="${TOOLCHAIN}/host/usr/bin/arm-buildroot-linux-gnueabihf-" \
	--sysroot="${TOOLCHAIN}/host/usr/arm-buildroot-linux-gnueabihf/sysroot" \
	${RELEASE} \
	--cleanbuild \
	${USE_CCACHE} \
        --downloadcache="${WORKINGDIR}/downloadcache"

    set +x
    make ${JARGS} BUILD=${TARGET} squashfs

    artifact build.${TARGET}/showtime.sqfs sqfs application/octet-stream showtime.sqfs

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
