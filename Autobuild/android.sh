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
    --downloadcache="${WORKINGDIR}/downloadcache"

APPNAME=movian

if [ -z "${MOVIAN_KEYSTORE_PASS}" ]; then
    make ${JARGS} BUILD=${TARGET}
else
    make ${JARGS} BUILD=${TARGET} signed-apk
    artifact build.${TARGET}/${APPNAME}.apk apk application/vnd.android.package-archive ${APPNAME}.apk
fi
