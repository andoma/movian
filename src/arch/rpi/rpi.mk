.DEFAULT_GOAL := ${PROG}.stripped

SRCS += src/arch/rpi/rpi_main.c \
	src/arch/rpi/omx.c \
	src/arch/rpi/rpi_audio.c \
	src/arch/rpi/rpi_video.c \
	src/arch/rpi/rpi_pixmap.c \
	src/arch/rpi/rpi_tv.c \
	src/ui/glw/glw_video_rpi.c \
	src/prop/prop_posix.c \
	src/prop/prop_glib_courier.c \

SRCS += src/arch/linux/linux_misc.c \
	src/arch/linux/linux_trap.c \
	src/arch/linux/linux_process_monitor.c \
	src/fileaccess/fa_opencookie.c \
	src/fileaccess/fa_fs.c \
	src/arch/posix/posix.c \
	src/arch/posix/posix_threads.c \
	src/networking/asyncio_posix.c \
	src/networking/net_posix.c \
	src/networking/net_ifaddr.c \
	src/ipc/devevent.c \
	src/arch/stos/stos_automount.c \

SRCS += src/htsmsg/persistent_file.c

#
# OS specific sources and flags
#
DVDCSS_CFLAGS = -DHAVE_LINUX_DVD_STRUCT -DDVD_STRUCT_IN_LINUX_CDROM_H -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE

${PROG}.stripped: ${PROG}.bundle
	${STRIP} -o $@ $<

stripped: ${PROG}.stripped

SQDIR=${BUILDDIR}/sqfs
SYMS=${BUILDDIR}/showtime.syms

${SYMS}: ${PROG}.bundle src/arch/rpi/rpi.mk
	${OBJDUMP} -t -j .text $< | awk '{print $$1 " " $$NF}'|sort >$@

${BUILDDIR}/showtime.sqfs: ${PROG}.stripped ${SYMS}
	rm -rf "${SQDIR}"
	mkdir -p "${SQDIR}/bin"
	mkdir -p "${SQDIR}/lib"
	cp ${PROG}.stripped "${SQDIR}/bin/showtime"
	cp ${SYMS} "${SQDIR}/bin/showtime.syms"

	mksquashfs "${SQDIR}" ${BUILDDIR}/showtime.sqfs  -noD -noF -noI -noappend

squashfs: ${BUILDDIR}/showtime.sqfs
