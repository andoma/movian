
INSTALLDIR=$HOME/install-10.6-i386-x86_64
# pkg-config 0.18 or higher, otherwise PKG_CONFIG_LIBDIR does not work
PKGCONFIG=/opt/local/bin/pkg-config

export OSX_TARGET=10.6
export OSX_SYSROOT="/Developer/SDKs/MacOSX10.6.sdk"
export PKG_CONFIG_LIBDIR=$INSTALLDIR/lib/pkgconfig
export CC="gcc" 
export CFLAGS="-arch i386 -arch x86_64 -isysroot $OSX_SYSROOT"
export LDFLAGS="-arch i386 -arch x86_64 -mmacosx-version-min=$OSX_TARGET -isysroot $OSX_SYSROOT"

source buildenv.common.sh

