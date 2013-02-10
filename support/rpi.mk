.DEFAULT_GOAL := ${PROG}.bundle

SRCS += src/arch/rpi/rpi_main.c

SRCS += src/arch/linux/linux_misc.c \
	src/arch/linux/linux_trap.c \
	src/arch/posix/posix.c \
	src/arch/posix/posix_threads.c \
	src/networking/net_posix.c \

#
# OS specific sources and flags
#
DVDCSS_CFLAGS = -DHAVE_LINUX_DVD_STRUCT -DDVD_STRUCT_IN_LINUX_CDROM_H -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE

${PROG}.stripped: ${PROG}.bundle
	${STRIP} -o $@ $<

stripped: ${PROG}.stripped
