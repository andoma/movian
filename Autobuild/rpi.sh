
build()
{
    mkdir -p ${WORKINGDIR}

    TOOLCHAIN_URL=http://www.lonelycoder.com/download/arm-unknown-linux-gnueabi.tar.gz
    TOOLCHAIN="${WORKINGDIR}/arm-unknown-linux-gnueabi"

    SYSROOT_URL=http://www.lonelycoder.com/download/rpi_alpha_sysroot.tar.gz
    SYSROOT="${WORKINGDIR}/rpi_alpha_sysroot"

  
    cleanup() {
	echo "Cleaning up"
	rm -rf "${TOOLCHAIN}" "${SYSROOT}" "${TEMPFILE}"
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


    echo "Sysroot firmware from: '${SYSROOT_URL}' Local install in: ${SYSROOT}"
    if [ -d "${SYSROOT}" ]; then
	echo "Sysroot seems to exist"
    else
	set +e
	trap cleanup SIGINT
	(
	    set -eu
	    mkdir -p ${SYSROOT}
	    cd ${SYSROOT}
	    curl -L "${SYSROOT_URL}" | tar xfz - --strip-components=1
	)
	
	STATUS=$?
	if [ $STATUS -ne 0 ]; then
	    echo "Unable to stage sysroot"
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
	--toolchain="${TOOLCHAIN}/bin/arm-linux-gnueabihf-" \
	--sysroot="${SYSROOT}" \
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
