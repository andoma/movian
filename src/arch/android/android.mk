SRCS += src/arch/android/android.c \
	src/arch/android/android_threads.c \
	src/arch/android/android_video_codec.c \
        src/video/h264_annexb.c \
	src/networking/net_posix.c \
	src/networking/asyncio_posix.c \
	src/networking/net_android.c \
	src/fileaccess/fa_funopen.c \
	src/fileaccess/fa_fs.c \
	src/arch/android/android_audio.c \
	src/arch/android/android_glw.c \
	src/arch/android/android_support.c \
	src/prop/prop_jni.c \
	src/ui/glw/glw_video_android.c \
	src/arch/linux/linux_process_monitor.c \
	src/ui/longpress.c \

SRCS += src/htsmsg/persistent_file.c

${BUILDDIR}/src/arch/android/%.o : CFLAGS = ${OPTFLAGS} \
	-Wall -Werror -Wwrite-strings -Wno-deprecated-declarations \
			-Wno-multichar -std=gnu99

.DEFAULT_GOAL := ${BUILDDIR}/${APPNAME}.aligned.apk


MANIFEST := ${BUILDDIR}/AndroidManifest.xml

AAPT      := ${ANDROID_BUILD_TOOLS}/aapt
DX        := ${ANDROID_BUILD_TOOLS}/dx
ZIPALIGN  := ${ANDROID_BUILD_TOOLS}/zipalign
APKSIGNER := ${ANDROID_BUILD_TOOLS}/apksigner

NUMVER := $(shell echo ${VERSION} | awk -F. '{ print $$3 + $$2 * 100000 + $$1 * 10000000 }')

R_JAVA := ${BUILDDIR}/java/com/lonelycoder/mediaplayer/R.java

JAVA_SRCS := $(shell find android/src/com/lonelycoder/mediaplayer -name '*.java')

RESFILES := $(shell find android/res -type f)


${BUILDDIR}/apk/lib/armeabi/libcore.so: ${LIB}.so
	@mkdir -p $(dir $@)
	${STRIP} -o $@ $<

${BUILDDIR}/inst/lib/libavcodec.so:    $(BUILDDIR)/stamps/libav.stamp
${BUILDDIR}/inst/lib/libavdevice.so:   $(BUILDDIR)/stamps/libav.stamp
${BUILDDIR}/inst/lib/libavformat.so:   $(BUILDDIR)/stamps/libav.stamp
${BUILDDIR}/inst/lib/libavresample.so: $(BUILDDIR)/stamps/libav.stamp
${BUILDDIR}/inst/lib/libavutil.so:     $(BUILDDIR)/stamps/libav.stamp
${BUILDDIR}/inst/lib/libswscale.so:    $(BUILDDIR)/stamps/libav.stamp

${BUILDDIR}/apk/lib/armeabi/%.so: ${BUILDDIR}/inst/lib/%.so
	@mkdir -p $(dir $@)
	${STRIP} -o $@ $<

${MANIFEST}: android/AndroidManifest.xml.in ${BUILDDIR}/version_git.h
	sed >$@ -e s/@@VERSION@@/${VERSION}/g -e s/@@APPNAME@@/${APPNAMEUSER}/g -e s/@@VERCODE@@/${NUMVER}/g $<


${R_JAVA}: ${MANIFEST} ${RESFILES}
	@mkdir -p ${BUILDDIR}/java
	${AAPT} package -f -m -J ${BUILDDIR}/java -S android/res \
	-M ${MANIFEST} -I ${ANDROID_PLATFORM}/android.jar

${BUILDDIR}/apk/classes.dex: ${JAVA_SRCS} ${R_JAVA}
	@mkdir -p ${BUILDDIR}/classes
	@mkdir -p $(dir $@)
	javac -source 1.7 -target 1.7 \
	-bootclasspath "${JAVA_HOME}/jre/lib/rt.jar" \
	-classpath ${ANDROID_PLATFORM}/android.jar -d ${BUILDDIR}/classes \
	${R_JAVA} ${JAVA_SRCS}
	${DX} --dex --output=$@ ${BUILDDIR}/classes

${BUILDDIR}/${APPNAME}.unsigned.apk: ${BUILDDIR}/apk/classes.dex ${RESFILES} \
	${BUILDDIR}/apk/lib/armeabi/libcore.so \
	${BUILDDIR}/apk/lib/armeabi/libavcodec.so \
	${BUILDDIR}/apk/lib/armeabi/libavdevice.so \
	${BUILDDIR}/apk/lib/armeabi/libavformat.so \
	${BUILDDIR}/apk/lib/armeabi/libavresample.so \
	${BUILDDIR}/apk/lib/armeabi/libavutil.so \
	${BUILDDIR}/apk/lib/armeabi/libswscale.so
	${AAPT} package -f -M ${MANIFEST} -S android/res \
	-I ${ANDROID_PLATFORM}/android.jar -F $@ ${BUILDDIR}/apk/

${BUILDDIR}/${APPNAME}.aligned.apk: ${BUILDDIR}/${APPNAME}.unsigned.apk
	${ZIPALIGN} -f -p 4 $< $@

${BUILDDIR}/${APPNAME}.apk: ${BUILDDIR}/${APPNAME}.aligned.apk
	${APKSIGNER} sign -ks android/movian.keystore -ks-pass env:MOVIAN_KEYSTORE_PASS --out $@ $<


signed-apk: ${BUILDDIR}/${APPNAME}.apk

install: ${BUILDDIR}/${APPNAME}.apk
	adb install -r $<

