.DEFAULT_GOAL := ${LIB}.so

SRCS += src/arch/android/android.c \
	src/arch/android/android_threads.c \
	src/networking/net_posix.c \
	src/networking/asyncio_posix.c \
	src/networking/net_android.c \
	src/fileaccess/fa_funopen.c \
	src/arch/android/android_audio.c \
	src/arch/android/android_glw.c \



${BUILDDIR}/src/arch/android/%.o : CFLAGS = ${OPTFLAGS} \
	-Wall -Werror -Wwrite-strings -Wno-deprecated-declarations \
			-Wno-multichar -std=gnu99


${TOPDIR}/android/Showtime/libs/armeabi/libshowtime.so: ${LIB}.so
	@mkdir -p $(dir $@)
	${STRIP} -o $@ $<

install: ${TOPDIR}/android/Showtime/libs/armeabi/libshowtime.so

