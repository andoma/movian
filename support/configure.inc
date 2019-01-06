#!/bin/bash
#
# Showtime configure shared functions
#
# Copyright (c) 2005-2008 Andreas Öman
#
# Based on FFmpeg's configure script:
#
# Copyright (c) 2000-2002 Fabrice Bellard
# Copyright (c) 2005-2008 Diego Biurrun
# Copyright (c) 2005-2008 Mans Rullgard
#

TOPDIR=`pwd`
BUILD=${CONFIGURE_POSTFIX}
TMPDIR=${TOPDIR}/tmp
DOWNLOADCACHE=${TOPDIR}/.downloadcache
GLW_DEFAULT_SKIN="flat"
DEFAULT_LOGTARGET=""
APPNAME="Movian"
OPTLEVEL=2
PLUGINREPO="https://movian.tv/api/plugins/plugins-v1.json"
LINKER="\${CC}" # Default is to link with c-compiler

CONFIG_LIST="
 airplay
 audiotest
 avahi
 bittorrent
 bookmarks
 bonjour
 bspatch
 bughunt
 cedar
 commoncrypto
 connman
 dvd
 emu_thread_specifics
 fsevents
 ftpclient
 ftpserver
 glw
 glw_backend_gx
 glw_backend_opengl
 glw_backend_opengl_es
 glw_backend_rsx
 glw_frontend_cocoa
 glw_frontend_ps3
 glw_frontend_wii
 glw_frontend_x11
 glw_rec
 glw_settings
 gu
 gumbo
 hls
 htsp
 httpserver
 icecast
 inotify
 kvstore
 libasound
 libav
 libav_attachment_pointer
 libcec
 libfontconfig
 libfreetype
 libjpeg
 libntfs
 libogc
 libpthread
 libpulse
 librtmp
 libx11
 libxext
 libxrandr
 libxss
 libxv
 libxxf86vm
 lirc
 locatedb
 media_settings
 metadata
 nativesmb
 netlog
 nvctrl
 openssl
 playqueue
 plugins
 polarssl
 rar
 realpath
 release
 spotlight
 sqlite
 sqlite_internal
 sqlite_locking
 sqlite_vfs
 stdin
 stpp
 timegm
 tlsf
 upgrade
 upnp
 usagereport
 valgrind
 vdpau
 webkit
 webpopup
 zlib
 vmir
"





cleanup() {
    rm -rf $TMPDIR
}

die() {
    cleanup
    echo "Configure failed"
    exit 1
}

die_invalid_opt() {
    echo "Invalid option: $1"
    die
}

die_cause() {
    echo "$1"
    die
}

set_all(){
    value=$1
    shift
    for var in $*; do
        eval $var=$value
    done
}

enable(){
    set_all yes $*
}

disable(){
    set_all no $*
}


enabled(){
    eval test "x${!1}" = "xyes"
}

disabled(){
    eval test "x${!1}" = "xno" -o "x${!1}" = "x"
}

toupper(){
    echo "$@" | tr abcdefghijklmnopqrstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ
}

tolower(){
    echo "$@" | tr ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz
}

is_in(){
    value=$1
    shift
    for var in $*; do
        [ $var = $value ] && return 0
    done
    return 1
}

print_config(){
    pfx=$1
    header=$2
    makefile=$3
    shift 3
    for cfg; do
        ucname="`toupper $cfg`"
        if enabled $cfg; then
            echo "#define ${pfx}${ucname} 1" >> $header
            echo "#define ENABLE_${ucname} 1" >> $header
            echo "${pfx}${ucname}=yes" >> $makefile
        else
            echo "#define ENABLE_${ucname} 0" >> $header
        fi
    done
}

cp_if_changed(){
    cmp -s "$1" "$2" || cp -f "$1" "$2"
}

download_file() {
    local __resultvar=$2
    local URL_HASH=`echo "$1" | ${SHASUM}  | awk '{print $1}'`
    mkdir -p "${DOWNLOADCACHE}"
    local DLPATH="${DOWNLOADCACHE}/${URL_HASH}"
    if [ ! -f "${DLPATH}" ]; then
	echo "Downloading $1"
	curl -f -L -o "${DLPATH}" "$1" || die_cause "Unable to download file" 
    else
	echo "Found in cache: $1"
    fi
    eval $__resultvar="$DLPATH"
}



common_help(){
  echo "Usage: configure [options]"
  echo "Options: [defaults in brackets after descriptions]"
  echo
  echo "Standard options:"
  echo "  --help                   Print this message"
  echo "  --build=BUILD            Build in build.BUILD [build.$BUILD]"
  echo "  --version=VERSION        Override version from git-describe"
  echo "  --release                Stage for release"
  echo "  --cleanbuild             Erase builddir before configuring"
  echo "  --ccache                 Enable use of ccache"
  echo "  --extra-cflags           Extra cflags"
  echo "  --extra-ldflags          Extra ldflags"
  echo "  --downloadcache          Where to download files during build [$DOWNLOADCACHE]"
  echo " --optlevel=LEVEL          Optimization level [$OPTLEVEL]"
  echo " --plugin-repo=URL         URL to plugin repo [$PLUGINREPO]"
  echo ""
  echo "Platform specific options:"
}




common_opt(){
  case "$1" in

  --build=*) BUILD="$2"
  ;;
  --version=*) VERSIONOVERRIDE="$2"
  ;;
  --extra-cflags=*) EXTRA_CFLAGS="$2"
  ;;
  --extra-ldflags=*) EXTRA_LDFLAGS="$2"
  ;;
  --downloadcache=*) DOWNLOADCACHE="$2"
  ;;
  --plugin-repo=*) PLUGINREPO="$2"
  ;;

  --enable-?*|--disable-?*)
  eval $(echo "$1" | sed 's/--/action=/;s/-/ option=/;s/-/_/g')
  if is_in $option $CONFIG_LIST; then
      $action $option
  else
      die_invalid_opt $1
  fi
  ;;

  --jobs=*)
  ;;

  --cleanbuild) enable cleanbuild
  ;;

  --ccache) enable ccache
  ;;

  --release) enable release
  ;;

  --appname=*) APPNAME="$2"
  ;;
  --optlevel=*) OPTLEVEL="$2"
  ;;
  --sanitize=*) SANITIZE="$optval"
  ;;
  *)
  die_invalid_opt $1
  esac
}


add_stamp() {
    echo >>${CONFIG_MAK} "STAMPS += ${BUILDDIR}/stamps/$1.stamp"
}

setup_env(){
    BUILDDIR=build.${BUILD}

    if enabled cleanbuild; then
	echo "Cleaning builddir: ${BUILDDIR}"
	rm -rf ${BUILDDIR}
    fi

    if enabled ccache; then
	CC="ccache $CC"
	CXX="ccache $CXX"
    fi

    which >/dev/null 2>&1 shasum  && SHASUM=shasum
    which >/dev/null 2>&1 sha1sum && SHASUM=sha1sum

    mkdir -p ${BUILDDIR}
    BUILDDIR=`cd ${BUILDDIR} && pwd`

    CONFIG_H=${BUILDDIR}/config.h.tmp
    CONFIG_MAK=${BUILDDIR}/config.mak.tmp
    CONFIG_DEF=${TOPDIR}/config.default

    mkdir -p ${BUILDDIR}

    rm -rf "${BUILDDIR}/stamps"

    EXT_INSTALL_DIR=${BUILDDIR}/inst

    mkdir -p ${EXT_INSTALL_DIR}/include ${EXT_INSTALL_DIR}/lib

    # Create a tmpdir used during configure
    mkdir -p ${TMPDIR}
    TMPDIR=`cd ${TMPDIR} && pwd`

    # Application name
    lcname="`tolower $APPNAME`"

    ucplatform="`toupper $PLATFORM`"

    # Create config.mak
    cat > ${CONFIG_MAK} << EOF 
# Automatically generated by configure, do not edit
CONFIGURE_ARGS=$@
APPNAME=${lcname}
APPNAMEUSER=${APPNAME}
TOPDIR=$TOPDIR
PLATFORM=$PLATFORM
CONFIGURE_POSTFIX=$CONFIGURE_POSTFIX
OPTLEVEL=${OPTLEVEL}
CFLAGS_cfg += ${EXTRA_CFLAGS}
LDFLAGS_cfg += ${EXTRA_LDFLAGS}
EOF
   
    # Create config.h
    cat > ${CONFIG_H} << EOF 
/* Automatically generated by configure, do not edit */
#define PLATFORM "${PLATFORM}"
#define PLATFORM_${ucplatform}
#define CONFIGURE_ARGS "$@"
#define APPNAME "${lcname}"
#define APPNAMEUSER "${APPNAME}"
#define PLUGINREPO "${PLUGINREPO}"
#define SHOWTIME_DEFAULT_UI "${DEFAULT_UI}"
#define SHOWTIME_DEFAULT_LOGTARGET "${DEFAULT_LOGTARGET}"
EOF

#
# Version override
#
if [ -n "$VERSIONOVERRIDE" ]; then
    echo >>${CONFIG_H} "#define VERSIONOVERRIDE \"${VERSIONOVERRIDE}\""
    echo >>${CONFIG_MAK} "VERSION:=\"${VERSIONOVERRIDE}\""
fi

#
# sanitize
#
if [ -n "$SANITIZE" ]; then
    echo >>${CONFIG_MAK} "LDFLAGS_cfg += -fsanitize=${SANITIZE}"
    echo >>${CONFIG_MAK} "CFLAGS_cfg += -fsanitize=${SANITIZE}"
fi

    # Create config.default
    cat > ${CONFIG_DEF} << EOF 
# Automatically generated by configure, do not edit
BUILD ?= $BUILD
EOF


}


update_ext_submodule() {
    echo "Updating $1, submodule status before update"
    git submodule status ext/$1
    git submodule update -f --init ext/$1
    echo "Updating $1, submodule status after update"
    git submodule status ext/$1
}





libav_setup() {
    update_ext_submodule libav
    echo "Configuring libav..."
    
    LIBAV_BUILD_DIR=${BUILDDIR}/libav/build

    mkdir -p "${LIBAV_BUILD_DIR}"

    #LIBAV_COMMON_FLAGS="--disable-parsers --disable-decoders --disable-demuxers"

    LIBAV_COMMON_FLAGS="${LIBAV_COMMON_FLAGS} --disable-encoders --disable-filters --disable-muxers --disable-devices --disable-demuxer=rtp --disable-protocol=rtp --disable-bzlib --disable-decoder=twinvq --disable-decoder=snow --disable-decoder=cavs --disable-programs --disable-avfilter --enable-decoder=png --enable-decoder=mjpeg --enable-encoder=mjpeg --enable-encoder=png --enable-muxer=spdif --enable-encoder=ac3 --enable-encoder=eac3 --enable-muxer=matroska --enable-encoder=ffvhuff --enable-encoder=pcm_s16le"

    (cd ${LIBAV_BUILD_DIR} && ${TOPDIR}/ext/libav/configure ${LIBAV_ARCH_FLAGS} ${LIBAV_COMMON_FLAGS} --prefix=${EXT_INSTALL_DIR} --extra-cflags="${LIBAV_CFLAGS} ${EXTRA_CFLAGS}" --extra-ldflags="${LIBAV_LDFLAGS} ${EXTRA_LDFLAGS}" --cc="${CC}") || die

    echo >>${CONFIG_MAK} "LIBAV_BUILD_DIR=${LIBAV_BUILD_DIR}"
    echo >>${CONFIG_MAK} "LIBAV_INSTALL_DIR=${LIBAV_INSTALL_DIR}"

    add_stamp libav
}


#
#
#
rtmpdump_setup() {
    if disabled librtmp; then
        return
    fi

    update_ext_submodule rtmpdump
}


#
#
#
gumbo_setup() {
    if disabled gumbo; then
        return
    fi

    update_ext_submodule gumbo-parser
}



#
#
#
vmir_setup() {
    if disabled vmir; then
        return
    fi

    update_ext_submodule vmir
}


ext_setup() {
    libav_setup
    rtmpdump_setup
    gumbo_setup
    vmir_setup
}


#
#
#
libyuv_setup() {
    echo >>${CONFIG_MAK} "LDFLAGS_cfg += -lyuv"
    add_stamp libyuv
    update_ext_submodule libyuv
}


#
#
#
zlib_setup()
{
    ZLIB_VER="1.2.3"
    download_file http://commondatastorage.googleapis.com/nativeclient-mirror/nacl/zlib-${ZLIB_VER}.tar.gz zlibtarball

    ZLIB_SRC_DIR=${BUILDDIR}/zlib-${ZLIB_VER}

    tar -x -C "${BUILDDIR}" -f "${zlibtarball}"

    (cd ${ZLIB_SRC_DIR} && \
        CC=${CC} AR="${TOOLCHAIN}ar -r" RANLIB=${TOOLCHAIN}ranlib \
        ./configure --static --prefix=${EXT_INSTALL_DIR})

    (cd ${ZLIB_SRC_DIR} && make -j4 && make install)
}


#
#
#
bzip2_setup()
{
    BZIP2_VER="1.0.6"
    download_file https://fossies.org/linux/misc/bzip2-${BZIP2_VER}.tar.gz bzip2tarball

    BZIP2_SRC_DIR=${BUILDDIR}/bzip2-${BZIP2_VER}

    tar -x -C "${BUILDDIR}" -f "${bzip2tarball}"

    sed -ibak "s#^CC=gcc\$#CC=${CC}#" ${BZIP2_SRC_DIR}/Makefile
    sed -ibak "s#^AR=ar\$#AR=${TOOLCHAIN}ar#" ${BZIP2_SRC_DIR}/Makefile
    sed -ibak "s#^RANLIB=ranlib\$#RANLIB=${TOOLCHAIN}ranlib#" ${BZIP2_SRC_DIR}/Makefile

    echo >>${CONFIG_MAK} "BZIP2_BUILD_DIR=${BZIP2_SRC_DIR}"

    echo >>${CONFIG_MAK} "LDFLAGS_cfg += -lbz2"
    add_stamp bzip2
}



#
#
#
freetype_setup()
{
    FREETYPE_VER="2.4.9"

    download_file http://download.savannah.gnu.org/releases/freetype/freetype-${FREETYPE_VER}.tar.gz freetypetarball

    FREETYPE_SRC_DIR=${BUILDDIR}/freetype-${FREETYPE_VER}
    FREETYPE_BUILD_DIR=${BUILDDIR}/freetype/build

    tar -x -C "${BUILDDIR}" -f "${freetypetarball}"
    mkdir -p "${FREETYPE_BUILD_DIR}"

    (cd ${FREETYPE_BUILD_DIR} && \
        CFLAGS="${ARCH_CFLAGS}" CC="${CC}" AR=${TOOLCHAIN}ar RANLIB=${TOOLCHAIN}ranlib \
        ${FREETYPE_SRC_DIR}/configure \
        $1 \
	--prefix=${EXT_INSTALL_DIR} \
	--enable-static=yes \
	--enable-shared=no)

    echo >>${CONFIG_MAK} "FREETYPE_BUILD_DIR=${FREETYPE_BUILD_DIR}"

    echo >>${CONFIG_MAK} "CFLAGS_cfg += -I${EXT_INSTALL_DIR}/include/freetype2"
    echo >>${CONFIG_MAK} "LDFLAGS_cfg += -lfreetype -lbz2"
    add_stamp freetype
}

#
# Do final common stuff
#
finalize() {

    cat >>${CONFIG_MAK} <<EOF
CC=$CC
CXX=$CXX
LINKER:=$LINKER
CFLAGS_cfg  += -I${EXT_INSTALL_DIR}/include
LDFLAGS_cfg += -L${EXT_INSTALL_DIR}/lib
STRIP=${TOOLCHAIN}strip
OBJDUMP=${TOOLCHAIN}objdump
OBJCOPY=${TOOLCHAIN}objcopy
RANLIB=${TOOLCHAIN}ranlib
AR=${TOOLCHAIN}ar
EXT_INSTALL_DIR=${EXT_INSTALL_DIR}
EOF

    if enabled libav; then
	echo >>${CONFIG_MAK} "LDFLAGS_cfg += -lavresample -lswscale -lavformat -lavcodec -lavutil"
    fi

    if enabled libcec; then
	echo >>${CONFIG_MAK} "LDFLAGS_cfg += -lcec"
    fi

    if enabled libjpeg; then
	echo >>${CONFIG_MAK} "LDFLAGS_cfg += -ljpeg"
    fi

    if enabled polarssl; then
	echo "Using built-in polarssl"
	echo >>${CONFIG_MAK} "CFLAGS_cfg += -DUSE_POLARSSL"
    fi


    # Generate default skin path for GLW
    if enabled glw; then
	echo >>${CONFIG_MAK} "BUNDLES+=glwskins/${GLW_DEFAULT_SKIN}"
	echo >>${CONFIG_H} "#define SHOWTIME_GLW_DEFAULT_SKIN \""${GLW_DEFAULT_SKIN}"\""
    fi

    # Generate resource path for GU
    if enabled gu; then
	echo >>${CONFIG_MAK} "BUNDLES+=guresources"
    fi

    # Fonts
    echo >>${CONFIG_MAK} "BUNDLES+=res/fonts"
    
    # SVG icons
    echo >>${CONFIG_MAK} "BUNDLES+=res/svg"

    # Languages
    echo >>${CONFIG_MAK} "BUNDLES+=lang"

    # Static web resources
    echo >>${CONFIG_MAK} "BUNDLES+=res/static"

    # Static ecmascript resources
    echo >>${CONFIG_MAK} "BUNDLES+=res/ecmascript"

    # Test signal audio samples
    if enabled audiotest; then
	echo >>${CONFIG_MAK} "BUNDLES+=res/speaker_positions"
    fi

    # sqlite
    if enabled sqlite_vfs; then
	echo >>${CONFIG_MAK} "SQLITE_CFLAGS_cfg += -DSQLITE_OS_OTHER=1 -DSQLITE_DEFAULT_LOCKING_MODE=1"
    fi

    if enabled sqlite_locking; then
	echo >>${CONFIG_MAK} "SQLITE_CFLAGS_cfg += -DSQLITE_MUTEX_NOOP"
    fi

    if enabled sqlite_internal; then
	echo >>${CONFIG_MAK} "CFLAGS_cfg += -Iext/sqlite"
    fi

    # Release tag
    if enabled release; then
	echo >>${CONFIG_MAK} "CFLAGS_dbg = -DNDEBUG -D_NDEBUG"
    else
	echo >>${CONFIG_MAK} "CFLAGS_dbg = -DDEBUG=1"
    fi

    cat >> ${CONFIG_MAK} << EOF 
LDFLAGS_cfg += -lz -lm
EOF
  
    # Generate config files
    print_config CONFIG_ ${CONFIG_H} ${CONFIG_MAK} $CONFIG_LIST

    cp_if_changed ${CONFIG_H} ${BUILDDIR}/config.h
    cp_if_changed ${CONFIG_MAK} ${BUILDDIR}/config.mak
    rm -f ${CONFIG_H} ${CONFIG_MAK}

    cleanup
}


#
# Default enables
#

enable audiotest
enable bittorrent
enable bookmarks
enable ftpclient
enable ftpserver
enable glw_settings
enable gumbo
enable hls
enable htsp
enable icecast
enable kvstore
enable libav
enable media_settings
enable metadata
enable nativesmb
enable netlog
enable playqueue
enable plugins
enable rar
enable sqlite
enable stpp
enable upgrade
enable upnp
enable usagereport
