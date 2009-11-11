(cd freetype* && \
  ./configure --prefix=$INSTALLDIR \
    --enable-static && \
  make clean && \
  make && \
  make install \
  )

