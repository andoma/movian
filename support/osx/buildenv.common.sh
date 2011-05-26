# pkg-config 0.18 or higher, otherwise PKG_CONFIG_LIBDIR does not work
export PKGCONFIG=/opt/local/bin/pkg-config
export INSTALLDIR="$HOME/src/showtime-env/install-$OSX_TARGET-$CC-$CC_ARCH"
export PKG_CONFIG_LIBDIR="$INSTALLDIR/lib/pkgconfig"
export CFLAGS="-arch $CC_ARCH -isysroot $OSX_SYSROOT"
export LDFLAGS="-arch $CC_ARCH -mmacosx-version-min=$OSX_TARGET -isysroot $OSX_SYSROOT"
# CC exported from sourcing script

(cd freetype* && \
  ./configure "--prefix=$INSTALLDIR" \
    --disable-shared \
    --enable-static && \
  make clean && \
  make && \
  make install \
  )

(cd libav* && \
  ./configure "--prefix=$INSTALLDIR" \
    --disable-encoders \
    --disable-bsfs \
    --disable-filters \
    --disable-muxers \
    --disable-devices \
    --disable-protocols \
    --disable-network \
    --disable-ffserver \
    --disable-ffmpeg \
    --disable-ffplay \
    --disable-ffprobe \
    --disable-bzlib \
    --disable-decoder=twinvq \
    --disable-decoder=snow \
    --disable-decoder=cavs \
    --disable-shared \
    --enable-static && \
  make clean && \
  make && \
  make install \
  )

