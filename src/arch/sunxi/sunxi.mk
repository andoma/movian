.DEFAULT_GOAL := ${PROG}.bundle


#
# OS specific sources and flags
#
SRCS += src/arch/sunxi/sunxi_main.c \
	src/arch/sunxi/sunxi.c \
	src/arch/linux/linux_misc.c \
	src/arch/linux/linux_trap.c \
	src/fileaccess/fa_opencookie.c \
	src/networking/asyncio_posix.c \
	src/arch/posix/posix.c \
	src/arch/posix/posix_threads.c \
	src/networking/net_posix.c \
	src/networking/net_ifaddr.c \
	src/ipc/devevent.c \
	src/audio2/alsa.c \
	src/arch/sunxi/sunxi_alsa.c \
	ext/tlsf/tlsf.c \
	src/prop/prop_glib_courier.c \

${PROG}.stripped: ${PROG}.bundle
	${STRIP} -o $@ $<

strip: ${PROG}.stripped



install: ${PROG}.stripped
	mkdir -p ${INSTDIR}/bin
	mkdir -p ${INSTDIR}/lib

	cp ${PROG}.stripped ${INSTDIR}/bin/
	cp -a ${GLLIBS}/*.so ${INSTDIR}/lib/
	cp -a ${LIBAV_INSTALL_DIR}/lib/lib* ${INSTDIR}/lib/
	cp -a ${LIBSPOTIFY_PATH}/lib/lib* ${INSTDIR}/lib/
	${STRIP} ${INSTDIR}/lib/*.so

