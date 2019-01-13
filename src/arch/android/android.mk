SRCS += src/arch/android/android.c \
	src/arch/android/android_threads.c \
	src/arch/android/android_video_codec.c \
	src/arch/android/android_fs.c \
        src/video/h264_annexb.c \
	src/networking/net_posix.c \
	src/networking/asyncio_posix.c \
	src/networking/net_android.c \
	src/fileaccess/fa_funopen.c \
	src/arch/android/android_audio.c \
	src/arch/android/android_glw.c \
	src/arch/android/android_support.c \
	src/ui/glw/glw_video_android.c \
	src/arch/linux/linux_process_monitor.c \
	src/ui/longpress.c \

SRCS += src/htsmsg/persistent_file.c

${BUILDDIR}/src/arch/android/%.o : CFLAGS = ${OPTFLAGS} \
	-Wall -Werror -Wwrite-strings -Wno-deprecated-declarations \
			-Wno-multichar -std=gnu99

.DEFAULT_GOAL := ${BUILDDIR}/${APPNAME}.apk


MANIFEST := ${BUILDDIR}/AndroidManifest.xml

AAPT      := ${ANDROID_BUILD_TOOLS}/aapt
DX        := ${ANDROID_BUILD_TOOLS}/dx
ZIPALIGN  := ${ANDROID_BUILD_TOOLS}/zipalign
APKSIGNER := ${ANDROID_BUILD_TOOLS}/apksigner

NUMVER := $(shell echo ${VERSION} | awk -F. '{ print $$3 + $$2 * 100000 + $$1 * 10000000 + $(ANDROID_BUILDKIND_DESCRIMINATOR) * 100000000}')

R_JAVA := ${BUILDDIR}/java/com/lonelycoder/mediaplayer/R.java

JAVA_SRCS := $(shell find android/src/com/lonelycoder/mediaplayer -name '*.java')

RESFILES := $(shell find android/res -type f)


${BUILDDIR}/apk/lib/${ANDROID_ABI}/libcore.so: ${LIB}.so
	@mkdir -p $(dir $@)
	${STRIP} -o $@ $<

${BUILDDIR}/inst/lib/libavcodec.so:    $(BUILDDIR)/stamps/libav.stamp
${BUILDDIR}/inst/lib/libavdevice.so:   $(BUILDDIR)/stamps/libav.stamp
${BUILDDIR}/inst/lib/libavformat.so:   $(BUILDDIR)/stamps/libav.stamp
${BUILDDIR}/inst/lib/libavresample.so: $(BUILDDIR)/stamps/libav.stamp
${BUILDDIR}/inst/lib/libavutil.so:     $(BUILDDIR)/stamps/libav.stamp
${BUILDDIR}/inst/lib/libswscale.so:    $(BUILDDIR)/stamps/libav.stamp

${BUILDDIR}/apk/lib/${ANDROID_ABI}/%.so: ${BUILDDIR}/inst/lib/%.so
	@mkdir -p $(dir $@)
	${STRIP} -o $@ $<

${MANIFEST}: android/AndroidManifest.xml.in ${BUILDDIR}/version_git.h
	sed >$@ -e s/@@VERSION@@/${VERSION}/g -e s/@@APPNAME@@/${APPNAMEUSER}/g -e s/@@VERCODE@@/${NUMVER}/g -e s/@@ANDROID_MIN_SDK_VERSION@@/${ANDROID_MIN_SDK_VERSION}/g -e s/@@ANDROID_TARGET_SDK_VERSION@@/${ANDROID_TARGET_SDK_VERSION}/g $<

${R_JAVA}: ${MANIFEST} ${RESFILES}
	@mkdir -p ${BUILDDIR}/java
	${AAPT} package -f -m -J ${BUILDDIR}/java -S android/res \
	-M ${MANIFEST} -I ${ANDROID_PLATFORM_PATH}/android.jar

${BUILDDIR}/apk/classes.dex: ${JAVA_SRCS} ${R_JAVA}
	@mkdir -p ${BUILDDIR}/classes
	@mkdir -p $(dir $@)
	javac -source 1.7 -target 1.7 \
	-bootclasspath "${JAVA_HOME}/jre/lib/rt.jar" \
	-classpath ${ANDROID_PLATFORM_PATH}/android.jar -d ${BUILDDIR}/classes \
	${R_JAVA} ${JAVA_SRCS}
	${DX} --dex --output=$@ ${BUILDDIR}/classes

${BUILDDIR}/${APPNAME}.unsigned.apk: ${BUILDDIR}/apk/classes.dex ${RESFILES} \
	${BUILDDIR}/apk/lib/${ANDROID_ABI}/libcore.so \
	${BUILDDIR}/apk/lib/${ANDROID_ABI}/libavcodec.so \
	${BUILDDIR}/apk/lib/${ANDROID_ABI}/libavdevice.so \
	${BUILDDIR}/apk/lib/${ANDROID_ABI}/libavformat.so \
	${BUILDDIR}/apk/lib/${ANDROID_ABI}/libavresample.so \
	${BUILDDIR}/apk/lib/${ANDROID_ABI}/libavutil.so \
	${BUILDDIR}/apk/lib/${ANDROID_ABI}/libswscale.so
	${AAPT} package -f -M ${MANIFEST} -S android/res \
	-I ${ANDROID_PLATFORM_PATH}/android.jar -F $@ ${BUILDDIR}/apk/

${BUILDDIR}/${APPNAME}.aligned.apk: ${BUILDDIR}/${APPNAME}.unsigned.apk
	${ZIPALIGN} -f -p 4 $< $@

${BUILDDIR}/${APPNAME}.apk: ${BUILDDIR}/${APPNAME}.aligned.apk
	@[ -z "$$MOVIAN_KEYSTORE_PASS" ] && (cp $< $@ ; echo "Warning: Keystore password not present, producing unsigned APK") || (${APKSIGNER} sign -ks android/movian.keystore -ks-pass env:MOVIAN_KEYSTORE_PASS --out $@ $< ; echo "APK signed")

aligned: ${BUILDDIR}/${APPNAME}.aligned.apk

apk: ${BUILDDIR}/${APPNAME}.apk
	@echo "doozer-artifact:${BUILDDIR}/${APPNAME}.apk:apk:application/vnd.android.package-archive:${APPNAME}_api${ANDROID_MIN_SDK_VERSION}_${ANDROID_ABI}.apk:versioncode=${NUMVER}"

install: ${BUILDDIR}/${APPNAME}.apk
	adb install -r $<

run:
	adb shell am start -n com.lonelycoder.mediaplayer/.GLWActivity

stop:
	adb shell am force-stop com.lonelycoder.mediaplayer

logcat:
	adb logcat ActivityManager:I ${APPNAMEUSER}:D AndroidRuntime:D DEBUG:D *:S
