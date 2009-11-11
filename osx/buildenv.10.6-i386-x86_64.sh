
INSTALLDIR=$HOME/install-10.6-i386-x86_64
# pkg-config 0.18 or higher, otherwise PKG_CONFIG_LIBDIR does not work
PKGCONFIG=/opt/local/bin/pkg-config

export PKG_CONFIG_LIBDIR=$INSTALLDIR/lib/pkgconfig
export CC="gcc" 
export CFLAGS="-arch i386 -arch x86_64 -isysroot /Developer/SDKs/MacOSX10.6.sdk"
export LDFLAGS="-arch i386 -arch x86_64 -mmacosx-version-min=10.6 -isysroot /Developer/SDKs/MacOSX10.6.sdk"

source buildenv.common.sh

