
# CC exported from sourcing script

(cd freetype* && \
  ./configure --prefix=$INSTALLDIR \
    --enable-static && \
  make clean && \
  make && \
  make install \
  )

(cd openssl* && \
  ./Configure darwin-i386-cc --prefix=$INSTALLDIR zlib no-asm no-krb5 shared -mmacosx-version-min=$OSX_TARGET "-isysroot $OSX_SYSROOT" && \
  make clean && \
  make && \
  make install \
  )

