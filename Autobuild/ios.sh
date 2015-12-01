set -e
BUILDDIR="${PWD}/build.ios"

rm -rf "${BUILDDIR}"
mkdir -p "${BUILDDIR}"

(cd ios && xcodebuild archive -scheme Movian -archivePath "${BUILDDIR}/app")

(cd ios && xcodebuild -exportArchive -archivePath "${BUILDDIR}/app.xcarchive" -exportPath "${BUILDDIR}/" -exportOptionsPlist nobitcode.plist)

artifact build.ios/Movian.ipa ipa application/octet-stream Movian.ipa
