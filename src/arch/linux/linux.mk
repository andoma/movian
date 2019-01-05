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

uninstall:
	rm -f ${bindir}/showtime
	rm -f ${mandir}/showtime.1
	rm -f ${INSTDESKTOP}/showtime.desktop
	rm -f ${INSTICON}/showtime.svg

#	gtk-update-icon-cache $(prefix)/share/icons/hicolor/

#
#
#

SNAPROOT=$(BUILDDIR)/snaproot

SNAPDEPS = \
	$(SNAPROOT)/bin/movian \
	$(SNAPROOT)/meta/snap.yaml \
	$(SNAPROOT)/meta/gui/movian.desktop \
	$(SNAPROOT)/usr/share/movian/icons/movian-128.png \
	$(SNAPROOT)/command-movian.wrapper \
	$(SNAPROOT)/lib/libXss.so.1 \

$(SNAPROOT)/meta/snap.yaml: support/snap.yaml
	@mkdir -p $(dir $@)
	sed >$@ -e s/@@VERSION@@/${VERSION}/g -e s/@@APPNAME@@/${APPNAMEUSER}/g -e s/@@VERCODE@@/${NUMVER}/g $<


$(SNAPROOT)/lib/%: /usr/lib/x86_64-linux-gnu/%
	@mkdir -p $(dir $@)
	cp $< $@

$(SNAPROOT)/bin/movian: $(BUILDDIR)/movian.sbundle
	@mkdir -p $(dir $@)
	cp $< $@

$(SNAPROOT)/command-movian.wrapper: support/command-movian.wrapper
	@mkdir -p $(dir $@)
	cp $< $@

$(SNAPROOT)/meta/gui/movian.desktop: support/movian.desktop
	@mkdir -p $(dir $@)
	cp $< $@

$(SNAPROOT)/usr/share/movian/icons/movian-128.png: support/artwork/movian-128.png
	@mkdir -p $(dir $@)
	cp $< $@


$(BUILDDIR)/movian.snap: $(SNAPDEPS)
	rm -f $@
	mksquashfs $(SNAPROOT) $@ -noappend -comp xz -all-root -no-xattrs -no-fragments

snap: $(BUILDDIR)/movian.snap

