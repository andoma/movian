.DEFAULT_GOAL := ${PROG}


#
# OS specific sources and flags
#
SRCS += src/arch/linux/linux_main.c \
	src/arch/linux/linux_misc.c \
	src/arch/linux/linux_trap.c \
	src/arch/posix/posix.c \
	src/arch/posix/posix_threads.c \
	src/networking/net_posix.c \
	src/fileaccess/fa_opencookie.c \

SRCS-$(CONFIG_LIBPULSE)  += src/audio2/pulseaudio.c
SRCS-$(CONFIG_LIBASOUND) += src/audio2/alsa.c src/audio2/alsa_default.c 

${BUILDDIR}/src/arch/linux/%.o : CFLAGS = $(CFLAGS_GTK) ${OPTFLAGS} \
-Wall -Werror -Wmissing-prototypes -Wno-cast-qual -Wno-deprecated-declarations


DVDCSS_CFLAGS = -DHAVE_LINUX_DVD_STRUCT -DDVD_STRUCT_IN_LINUX_CDROM_H -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE


#
# Install
#


MAN = man/showtime.1
DESKTOP = support/gnome/showtime.desktop
ICON = support/gnome/showtime.svg

INSTDESKTOP= ${DESTDIR}$(prefix)/share/applications
INSTICON= ${DESTDIR}$(prefix)/share/icons/hicolor/scalable/apps


install: ${PROG}.datadir ${MAN} ${DESKTOP} ${ICON}
	install -D ${PROG}.datadir ${bindir}/showtime
	install -D ${MAN} ${mandir}/showtime.1

	install -D ${DESKTOP} ${INSTDESKTOP}/showtime.desktop
	install -D ${ICON} ${INSTICON}/showtime.svg

	for bundle in ${BUNDLES}; do \
		mkdir -p ${datadir}/$$bundle ;\
		cp -r $$bundle/*  ${datadir}/$$bundle ;\
	done

ifdef LIBSPOTIFY_PATH
	mkdir -p ${libdir}
	cp -d ${LIBSPOTIFY_PATH}/lib/libspotify.* ${libdir}
endif

uninstall:
	rm -f ${libdir}/libspotify.*
	rm -f ${bindir}/showtime
	rm -f ${mandir}/showtime.1
	rm -f ${INSTDESKTOP}/showtime.desktop
	rm -f ${INSTICON}/showtime.svg

#	gtk-update-icon-cache $(prefix)/share/icons/hicolor/

