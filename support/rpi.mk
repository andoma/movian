.DEFAULT_GOAL := ${PROG}.stripped

SRCS += src/arch/rpi/rpi_main.c \
	src/arch/rpi/omx.c \
	src/arch/rpi/rpi_audio.c \
	src/arch/rpi/rpi_video.c \
	src/ui/glw/glw_video_rpi.c \
	src/ui/background.c \
	src/prop/prop_posix.c \

SRCS += src/arch/linux/linux_misc.c \
	src/arch/linux/linux_trap.c \
	src/fileaccess/fa_opencookie.c \
	src/arch/posix/posix.c \
	src/arch/posix/posix_threads.c \
	src/networking/net_posix.c \
	src/ipc/devevent.c \

#
# OS specific sources and flags
#
DVDCSS_CFLAGS = -DHAVE_LINUX_DVD_STRUCT -DDVD_STRUCT_IN_LINUX_CDROM_H -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE

${PROG}.stripped: ${PROG}.bundle
	${STRIP} -o $@ $<

stripped: ${PROG}.stripped
