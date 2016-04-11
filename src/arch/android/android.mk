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

include ${TOPDIR}/android/android-dist.mk
