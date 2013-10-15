.DEFAULT_GOAL := apk

SRCS += src/arch/android/android.c \
	src/arch/android/android_threads.c \
	src/arch/android/android_video_codec.c \
        src/video/h264_annexb.c \
	src/networking/net_posix.c \
	src/networking/asyncio_posix.c \
	src/networking/net_android.c \
	src/fileaccess/fa_funopen.c \
	src/arch/android/android_glw.c \
	src/arch/android/android_support.c \
	src/prop/prop_jni.c \
	src/ui/glw/glw_video_android.c \

SRCS+=  src/arch/linux/linux_process_monitor.c

SRCS +=	src/arch/android/android_audio.c

${BUILDDIR}/src/pipelines/amp/%.o : CFLAGS = ${OPTFLAGS} -Wmissing-prototypes -Wmissing-declarations -Wimplicit-function-declaration -Werror  -Wno-multichar

${BUILDDIR}/src/video/amp_video.o : CFLAGS = ${OPTFLAGS} -Wmissing-prototypes -Wmissing-declarations -Wimplicit-function-declaration -Werror  -Wno-multichar

${BUILDDIR}/src/arch/android/%.o : CFLAGS = ${OPTFLAGS} \
	-Wall -Werror -Wwrite-strings -Wno-deprecated-declarations \
			-Wno-multichar -std=gnu99

ADIR=${TOPDIR}/android/Showtime

${ADIR}/libs/armeabi/libshowtime.so: ${LIB}.so
	@mkdir -p $(dir $@)
	${STRIP} -o $@ $<

apk: ${ADIR}/libs/armeabi/libshowtime.so
	cd ${ADIR} && ANDROID_HOME=${SDK} ant debug

install: apk
	${ADB} install -r ${ADIR}/bin/Showtime-debug.apk

run:
	${ADB} shell am start -n com.showtimemediacenter.showtime/.GLWActivity

stop:
	${ADB} shell am force-stop com.showtimemediacenter.showtime

logcat:
	${ADB} logcat -v time ActivityManager:I Showtime:D AndroidRuntime:D DEBUG:D *:S
