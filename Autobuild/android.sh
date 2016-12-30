which ccache >/dev/null
if [ $? -eq 0 ]; then
    echo "Using ccache"
    ccache -s
    USE_CCACHE="--ccache"
else
    USE_CCACHE=""
fi

./configure.android --build=${TARGET} \
    ${RELEASE} \
    ${VERSIONARGS} \
    --cleanbuild \
    ${USE_CCACHE} \
    --downloadcache="${WORKINGDIR}/downloadcache" \
    --sdk=/android/android-sdk-linux \
    --ndk=/android/android-ndk-r10e

APPNAME=movian

make ${JARGS} BUILD=${TARGET} release

artifact android/bin/Movian-release.apk apk application/vnd.android.package-archive ${APPNAME}.apk
