#
# Source modification and extra flags
#
SRCS += src/arch/osx/osx_app.m \
	src/arch/osx/GLWUI.m \
	src/arch/osx/GLWView.m \
	src/arch/darwin.c \
	src/arch/posix/posix.c \
	src/arch/posix/posix_threads.c \
	src/networking/net_posix.c \
	src/fileaccess/fa_funopen.c \

SRCS-$(CONFIG_WEBPOPUP) += src/arch/osx/osx_webpopup.m

SRCS-$(CONFIG_LIBAV) +=	src/audio2/mac_audio.c

DVDCSS_CFLAGS = -DDARWIN_DVD_IOCTL -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE

#
# Install
#
.DEFAULT_GOAL := ${BUILDDIR}/Showtime.app/Contents/MacOS/showtime

APPDIR        := ${BUILDDIR}/Showtime.app
APPPROG       := ${PROG}
include support/osx-stage.mk

APPDIR        := ${BUILDDIR}/dist/Showtime.app
APPPROG       := ${PROG}.osxapp
include support/osx-stage.mk

RESOUCES := ${APPDIR}/Contents/Resources

.PHONY: dist
dist: ${BUILDDIR}/dist/Showtime.app/Contents/MacOS/showtime

	for bundle in ${BUNDLES}; do \
		mkdir -p ${RESOUCES}/$$bundle ;\
		cp -r $$bundle/*  ${RESOUCES}/$$bundle ;\
	done

	support/osx_checkbundlelink.sh ${APPPROG}
	support/mkdmg ${APPDIR} Showtime support/Showtime.app/Contents/Resources/hts.icns ${BUILDDIR}/Showtime.dmg

#
#
#
clean: osx-clean

osx-clean:
	rm -rf ${BUILDDIR}/*.app ${BUILDDIR}/*.dmg 

