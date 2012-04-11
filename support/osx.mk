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

