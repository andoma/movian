.DEFAULT_GOAL := ${LIB}.so

SRCS += src/arch/android/android.c \
	src/arch/posix/posix_threads.c \
	src/networking/net_posix.c \
	src/fileaccess/fa_funopen.c \
