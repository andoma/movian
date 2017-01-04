.DEFAULT_GOAL := ${PROG}


#
# OS specific sources and flags
#
SRCS += src/arch/linux/linux_main.c \
	src/arch/linux/linux_misc.c \
	src/arch/linux/linux_trap.c \
	src/arch/posix/posix.c \
	src/arch/posix/posix_threads.c \
	src/networking/asyncio_posix.c \
	src/networking/net_posix.c \
	src/networking/net_ifaddr.c \
	src/fileaccess/fa_opencookie.c \
	src/fileaccess/fa_fs.c \
	src/prop/prop_glib_courier.c \
	src/arch/linux/linux_process_monitor.c \

SRCS += src/htsmsg/persistent_file.c

SRCS-$(CONFIG_LIBPULSE)  += src/arch/linux/pulseaudio.c
SRCS-$(CONFIG_LIBASOUND) += src/audio2/alsa.c src/audio2/alsa_default.c 
SRCS-$(CONFIG_WEBPOPUP) += src/arch/linux/linux_webpopup.c
SRCS-$(CONFIG_DVD) += src/backend/dvd/linux_dvd.c

${BUILDDIR}/src/arch/linux/%.o : CFLAGS = $(CFLAGS_GTK) ${OPTFLAGS} \
-Wall -Werror -Wmissing-prototypes -Wno-cast-qual -Wno-deprecated-declarations

${BUILDDIR}/src/prop/prop_glib_courier.o : CFLAGS = $(CFLAGS_GTK) ${OPTFLAGS} \
-Wall -Werror -Wmissing-prototypes -Wno-cast-qual -Wno-deprecated-declarations


DVDCSS_CFLAGS = -DHAVE_LINUX_DVD_STRUCT -DDVD_STRUCT_IN_LINUX_CDROM_H -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE


#
# Install
#


MAN = man/movian.1
DESKTOP = support/gnome/movian.desktop
ICON = support/gnome/movian.svg

INSTDESKTOP= ${DESTDIR}$(prefix)/share/applications
INSTICON= ${DESTDIR}$(prefix)/share/icons/hicolor/scalable/apps


install: ${PROG}.datadir ${MAN} ${DESKTOP} ${ICON}
	install -D ${PROG}.datadir ${bindir}/movian
	install -D ${MAN} ${mandir}/movian.1

	install -D ${DESKTOP} ${INSTDESKTOP}/movian.desktop
	install -D ${ICON} ${INSTICON}/movian.svg

	for bundle in ${BUNDLES}; do \
		mkdir -p ${datadir}/$$bundle ;\
		cp -r $$bundle/*  ${datadir}/$$bundle ;\
	done

	gtk-update-icon-cache $(prefix)/share/icons/hicolor/

uninstall:
	rm -f ${bindir}/movian
	rm -f ${mandir}/movian.1
	rm -f ${INSTDESKTOP}/movian.desktop
	rm -f ${INSTICON}/movian.svg

# remove old showtime
	rm -f ${bindir}/showtime
	rm -f ${mandir}/showtime.1
	rm -f ${INSTDESKTOP}/showtime.desktop
	rm -f ${INSTICON}/showtime.svg


