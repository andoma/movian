
INSTALLDIR=$HOME/install-10.4-i386-ppc
# pkg-config 0.18 or higher, otherwise PKG_CONFIG_LIBDIR does not work
PKGCONFIG=/opt/local/bin/pkg-config

export PKG_CONFIG_LIBDIR=$INSTALLDIR/lib/pkgconfig
export CC="gcc-4.0" 
export CFLAGS="-arch i386 -arch ppc -isysroot /Developer/SDKs/MacOSX10.4u.sdk"
export LDFLAGS="-arch i386 -arch ppc -mmacosx-version-min=10.4 -isysroot /Developer/SDKs/MacOSX10.4u.sdk"

source buildenv.common.sh

