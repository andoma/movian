.DEFAULT_GOAL := ${LIB}.so

SRCS += src/arch/android/android.c \
	src/arch/posix/posix_threads.c \
	src/networking/net_posix.c \
	src/networking/asyncio_posix.c \
	src/networking/net_android.c \
	src/fileaccess/fa_funopen.c \
	src/arch/android/android_audio.c \
	src/arch/android/android_glw.c \



${BUILDDIR}/src/arch/android/android_glw.o : CFLAGS = ${OPTFLAGS} -Wall -Werror

${TOPDIR}/android/Showtime/libs/armeabi/libshowtime.so: ${LIB}.so
	@mkdir -p $(dir $@)
	${STRIP} -o $@ $<

install: ${TOPDIR}/android/Showtime/libs/armeabi/libshowtime.so

