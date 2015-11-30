#
# Source modification and extra flags
#
SRCS += src/arch/osx/osx_app.m \
	src/arch/osx/osx_misc.c \
	src/arch/osx/GLWUI.m \
	src/arch/osx/GLWView.m \
	src/arch/darwin.c \
	src/arch/posix/posix.c \
	src/arch/posix/posix_threads.c \
	src/networking/net_posix.c \
	src/networking/asyncio_posix.c \
	src/networking/net_ifaddr.c \
	src/fileaccess/fa_funopen.c \
	src/fileaccess/fa_fs.c \
	src/ui/glw/glw_video_vda.c \


SRCS-$(CONFIG_WEBPOPUP) += src/arch/osx/osx_webpopup.m

SRCS-$(CONFIG_LIBAV) +=	src/audio2/mac_audio.c

DVDCSS_CFLAGS = -DDARWIN_DVD_IOCTL -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE

#
# Default runable (from current dir) target
#
.DEFAULT_GOAL := ${BUILDDIR}/$(APPNAMEUSER).app/Contents/MacOS/$(APPNAME)


${BUILDDIR}/$(APPNAMEUSER).app/Contents/MacOS/$(APPNAME): ${PROG} \
	${BUILDDIR}/$(APPNAMEUSER).app/Contents/Info.plist \
	${BUILDDIR}/$(APPNAMEUSER).app/Contents/Resources/hts.icns
	@echo "APP\t$@"
	@mkdir -p $(dir $@)
	@cp $< $@

${BUILDDIR}/$(APPNAMEUSER).app/Contents/%: support/$(APPNAME).app/Contents/%
	@mkdir -p $(dir $@)
	@cp $< $@


#
# Target for dist
#
${BUILDDIR}/dist/$(APPNAMEUSER).app/Contents/MacOS/$(APPNAME): ${PROG}.bin \
	${BUILDDIR}/dist/$(APPNAMEUSER).app/Contents/Info.plist \
	${BUILDDIR}/dist/$(APPNAMEUSER).app/Contents/Resources/hts.icns \
	${BUILDDIR}/trampoline
	@echo "APP\t$@"
	@mkdir -p $(dir $@)
	@cp ${PROG}.bin $@.bin
	@cp ${BUILDDIR}/trampoline $@

${BUILDDIR}/dist/$(APPNAMEUSER).app/Contents/%: support/$(APPNAME).app/Contents/%
	@mkdir -p $(dir $@)
	@cp $< $@

${PROG}.bin: ${PROG}.bundle
	${STRIP} $< -o $@



.PHONY: dist
dist: ${BUILDDIR}/dist/$(APPNAMEUSER).app/Contents/MacOS/$(APPNAME)
	support/osx_checkbundlelink.sh ${PROG}.bin
	support/mkdmg ${BUILDDIR}/dist/$(APPNAMEUSER).app $(APPNAMEUSER) support/$(APPNAMEUSER).app/Contents/Resources/hts.icns ${BUILDDIR}/$(APPNAMEUSER).dmg

#
#
#
clean: osx-clean

osx-clean:
	rm -rf ${BUILDDIR}/*.app ${BUILDDIR}/*.dmg 


${BUILDDIR}/trampoline:	support/osx/trampoline.c
	${CC} -mmacosx-version-min=10.8 -Wall -o $@ $<

