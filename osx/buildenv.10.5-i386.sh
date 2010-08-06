
INSTALLDIR=$HOME/install-10.5-i386
# pkg-config 0.18 or higher, otherwise PKG_CONFIG_LIBDIR does not work
PKGCONFIG=/opt/local/bin/pkg-config

export OSX_TARGET=10.5
export OSX_SYSROOT="/Developer/SDKs/MacOSX10.5.sdk"
export PKG_CONFIG_LIBDIR=$INSTALLDIR/lib/pkgconfig
export CC="gcc-4.0" 
export CFLAGS="-arch i386 -isysroot $OSX_SYSROOT"
export LDFLAGS="-arch i386 -mmacosx-version-min=$OSX_TARGET -isysroot $OSX_SYSROOT"

source buildenv.common.sh

