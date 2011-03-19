
# pkg-config 0.18 or higher, otherwise PKG_CONFIG_LIBDIR does not work
export PKGCONFIG=/opt/local/bin/pkg-config
export INSTALLDIR="$HOME/showtime-env-$OSX_TARGET-$CC-$CC_ARCH"
export PKG_CONFIG_LIBDIR="$INSTALLDIR/lib/pkgconfig"
export CFLAGS="-arch $CC_ARCH -isysroot $OSX_SYSROOT"
export LDFLAGS="-arch $CC_ARCH -mmacosx-version-min=$OSX_TARGET -isysroot $OSX_SYSROOT"
# CC exported from sourcing script

OPENSSL_TARGET="darwin-$CC_ARCH-cc"
if [ "$CC_ARCH" = "x86_64" ] ; then
  OPENSSL_TARGET="darwin64-x86_64-cc"
fi

(cd openssl* && \
  ./Configure $OPENSSL_TARGET "--prefix=$INSTALLDIR" zlib no-asm no-krb5 shared "-mmacosx-version-min=$OSX_TARGET" "-isysroot $OSX_SYSROOT" && \
  make clean && \
  make && \
  make install \
  )

(cd freetype* && \
  ./configure "--prefix=$INSTALLDIR" \
    --enable-static && \
  make clean && \
  make && \
  make install \
  )

(cd ffmpeg* && \
  ./configure "--prefix=$INSTALLDIR" \
    --enable-static && \
  make clean && \
  make && \
  make install \
  )


