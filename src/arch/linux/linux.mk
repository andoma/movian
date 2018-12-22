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
	$(SNAPROOT)/bin/xdg-user-dir \
	$(SNAPROOT)/meta/snap.yaml \
	$(SNAPROOT)/meta/gui/movian.desktop \
	$(SNAPROOT)/usr/share/movian/icons/movian-128.png \
	$(SNAPROOT)/command-movian.wrapper \
	$(SNAPROOT)/lib/libfontconfig.so.1 \
	$(SNAPROOT)/lib/libX11.so.6 \
	$(SNAPROOT)/lib/libX11-xcb.so.1 \
	$(SNAPROOT)/lib/libelf.so.1 \
	$(SNAPROOT)/lib/libLLVM-6.0.so.1 \
	$(SNAPROOT)/lib/libdrm.so.2 \
	$(SNAPROOT)/lib/libdrm_radeon.so.1 \
	$(SNAPROOT)/lib/libdrm_nouveau.so.2 \
	$(SNAPROOT)/lib/libdrm_intel.so.1 \
	$(SNAPROOT)/lib/libdrm_amdgpu.so.1 \
	$(SNAPROOT)/lib/libxcb.so.1 \
	$(SNAPROOT)/lib/libxcb-glx.so.0 \
	$(SNAPROOT)/lib/libxcb-dri2.so.0 \
	$(SNAPROOT)/lib/libxcb-dri3.so.0 \
	$(SNAPROOT)/lib/libxcb-present.so.0 \
	$(SNAPROOT)/lib/libxcb-sync.so.1 \
	$(SNAPROOT)/lib/libxshmfence.so.1 \
	$(SNAPROOT)/lib/libglapi.so.0 \
	$(SNAPROOT)/lib/libXdamage.so.1 \
	$(SNAPROOT)/lib/libXfixes.so.3 \
	$(SNAPROOT)/lib/libXau.so.6 \
	$(SNAPROOT)/lib/libXdmcp.so.6 \
	$(SNAPROOT)/lib/libXext.so.6 \
	$(SNAPROOT)/lib/libpulse.so.0 \
	$(SNAPROOT)/lib/libXss.so.1 \
	$(SNAPROOT)/lib/libXxf86vm.so.1 \
	$(SNAPROOT)/lib/libsndfile.so.1 \
	$(SNAPROOT)/lib/libasyncns.so.0 \
	$(SNAPROOT)/lib/libFLAC.so.8 \
	$(SNAPROOT)/lib/libogg.so.0 \
	$(SNAPROOT)/lib/libvorbisenc.so.2 \
	$(SNAPROOT)/lib/libvorbis.so.0 \
	$(SNAPROOT)/lib/libsensors.so.4 \
	$(SNAPROOT)/lib/libpciaccess.so.0 \
	$(SNAPROOT)/lib/mesa/libGL.so.1 \
	$(SNAPROOT)/lib/mesa/dri/i915_dri.so \
	$(SNAPROOT)/lib/mesa/dri/i965_dri.so \
	$(SNAPROOT)/lib/mesa/dri/kms_swrast_dri.so \
	$(SNAPROOT)/lib/mesa/dri/nouveau_dri.so \
	$(SNAPROOT)/lib/mesa/dri/nouveau_vieux_dri.so \
	$(SNAPROOT)/lib/mesa/dri/r200_dri.so \
	$(SNAPROOT)/lib/mesa/dri/r300_dri.so \
	$(SNAPROOT)/lib/mesa/dri/r600_dri.so \
	$(SNAPROOT)/lib/mesa/dri/radeon_dri.so \
	$(SNAPROOT)/lib/mesa/dri/radeonsi_dri.so \
	$(SNAPROOT)/lib/mesa/dri/swrast_dri.so \
	$(SNAPROOT)/lib/mesa/dri/virtio_gpu_dri.so \
	$(SNAPROOT)/lib/mesa/dri/vmwgfx_dri.so \
	$(SNAPROOT)/lib/pulseaudio/libpulsecommon-8.0.so \

$(SNAPROOT)/meta/snap.yaml: support/snap.yaml
	@mkdir -p $(dir $@)
	sed >$@ -e s/@@VERSION@@/${VERSION}/g -e s/@@APPNAME@@/${APPNAMEUSER}/g -e s/@@VERCODE@@/${NUMVER}/g $<


$(SNAPROOT)/lib/%: /usr/lib/x86_64-linux-gnu/%
	@mkdir -p $(dir $@)
	cp $< $@

$(SNAPROOT)/lib/mesa/dri/%.so: /usr/lib/x86_64-linux-gnu/dri/%.so
	@mkdir -p $(dir $@)
	cp $< $@

$(SNAPROOT)/bin/movian: $(BUILDDIR)/movian.sbundle
	@mkdir -p $(dir $@)
	cp $< $@

$(SNAPROOT)/bin/xdg-user-dir: /usr/bin/xdg-user-dir
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
	mksquashfs $(SNAPROOT) $@ -comp xz

snap: $(BUILDDIR)/movian.snap

