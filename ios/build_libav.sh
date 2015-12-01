#!/bin/sh

set -e

export PATH=$PATH:$PWD

function build_for_arch()
{
    (
        O="${OBJECT_FILE_DIR}/libav-$1"
        set -e
        echo "Checking for: ${O}/libavformat/libavformat.a"
        if [ -f "${O}/libavformat/libavformat.a" ]; then
            exit 0
        fi

        rm -rf   "${O}"
        mkdir -p "${O}"
        cd       "${O}"

        CC=`xcrun --sdk ${SDK_NAME} -f clang`

        "${SRCROOT}/../ext/libav/configure" \
            --cc=${CC} \
            --arch=$1 \
            --cpu=generic \
            --sysroot=`xcrun --sdk ${SDK_NAME} --show-sdk-path` \
            --target-os=darwin \
            --extra-cflags="-arch $1  -fembed-bitcode" \
            --extra-ldflags="-arch $1  -fembed-bitcode" \
            --enable-cross-compile \
            --enable-static \
            --disable-shared \
            --disable-encoders \
            --disable-bsfs \
            --disable-filters \
            --disable-muxers \
            --disable-devices \
            --disable-demuxer=rtp \
            --disable-protocol=rtp \
            --disable-decoder=twinvq \
            --disable-decoder=snow \
            --disable-decoder=cavs \
            --enable-encoder=mjpeg \
            --enable-encoder=png \
            --disable-avfilter \
            --enable-encoder=ac3 \
            --enable-encoder=eac3 \
            --disable-programs

        make -j8 V=1
        mkdir -p "${BUILT_PRODUCTS_DIR}"
        make install-headers DESTDIR="${BUILT_PRODUCTS_DIR}" V=1
    )
}

function rebuild_for_arch()
{
    (
        O="${OBJECT_FILE_DIR}/libav-$1"
        set -e
        cd       "${O}"

        make -j8 V=1
        mkdir -p "${BUILT_PRODUCTS_DIR}"
        make install-headers DESTDIR="${BUILT_PRODUCTS_DIR}" V=1
    )

}
for A in $ARCHS; do
    build_for_arch $A
done


mkdir -p "${BUILT_PRODUCTS_DIR}/lib"

for F in avcodec avformat avresample avutil swscale; do
    lipo -create \
         "${OBJECT_FILE_DIR}"/libav-*/lib${F}/lib${F}.a \
         -output \
         "${BUILT_PRODUCTS_DIR}/lib/lib${F}.a"
done


