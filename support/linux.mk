#
# OS specific sources and flags
#
SRCS += src/arch/linux.c \
	src/arch/trap_linux.c \
	src/arch/arch_posix.c \
	src/networking/net_posix.c \


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

