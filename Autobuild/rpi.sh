
build()
{
    mkdir -p ${WORKINGDIR}

    TEMPFILE="${WORKINGDIR}/tmpfile.zip"
    TOOLCHAIN_URL=http://www.lonelycoder.com/download/arm-unknown-linux-gnueabi.tar.gz
    TOOLCHAIN="${WORKINGDIR}/arm-unknown-linux-gnueabi"
 
    FW_HASH="2bcb2bc77be4ff5d9ecc79be73d527eba4e65366"
    FW_URL="https://github.com/raspberrypi/firmware/archive/${FW_HASH}.zip"
    FW_PATH="${WORKINGDIR}/firmware-${FW_HASH}"

   
    cleanup() {
	echo "Cleaning up"
	rm -rf "${TOOLCHAIN}" "${FW_PATH}" "${TEMPFILE}"
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


    echo "RPi firmware from: '${FW_URL}' Local install in: ${FW_PATH}"
    if [ -d "${FW_PATH}" ]; then
	echo "Firmware seems to exist"
    else
	set +e
	trap cleanup SIGINT
	(
	    set -eu
	    cd ${WORKINGDIR}
	    curl -L "${FW_URL}" >"${TEMPFILE}"
	    unzip "${TEMPFILE}"
	)
	
	STATUS=$?
	if [ $STATUS -ne 0 ]; then
	    echo "Unable to stage firmware"
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

    ./configure.rpi --build=${TARGET} \
	--toolchain="${TOOLCHAIN}/bin/arm-linux-gnueabihf-" \
	--vcroot="${FW_PATH}/hardfp/opt/vc/" \
	${RELEASE} \
	--cleanbuild \
	${USE_CCACHE}

    make ${JARGS} BUILD=${TARGET}
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
