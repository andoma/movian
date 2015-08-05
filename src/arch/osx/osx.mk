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
# Install
#
.DEFAULT_GOAL := ${BUILDDIR}/$(APPNAMEUSER).app/Contents/MacOS/$(APPNAME)

APPDIR        := ${BUILDDIR}/$(APPNAMEUSER).app
APPPROG       := ${PROG}
include src/arch/osx/osx-stage.mk

APPDIR        := ${BUILDDIR}/dist/$(APPNAMEUSER).app
APPPROG       := ${PROG}.osxapp
include src/arch/osx/osx-stage.mk

RESOUCES := ${APPDIR}/Contents/Resources

.PHONY: dist
dist: ${BUILDDIR}/dist/$(APPNAMEUSER).app/Contents/MacOS/$(APPNAME)

	for bundle in ${BUNDLES}; do \
		mkdir -p ${RESOUCES}/$$bundle ;\
		cp -r $$bundle/*  ${RESOUCES}/$$bundle ;\
	done

	support/osx_checkbundlelink.sh ${APPPROG}
	support/mkdmg ${APPDIR} $(APPNAMEUSER) support/$(APPNAMEUSER).app/Contents/Resources/hts.icns ${BUILDDIR}/$(APPNAMEUSER).dmg

#
#
#
clean: osx-clean

osx-clean:
	rm -rf ${BUILDDIR}/*.app ${BUILDDIR}/*.dmg 

