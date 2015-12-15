.DEFAULT_GOAL := stage

SRCS += src/arch/nacl/nacl_main.c \
	src/arch/nacl/nacl_misc.c \
	src/arch/nacl/nacl_fs.c \
	src/arch/nacl/nacl_audio.c \
	src/arch/nacl/nacl_video.c \
	src/arch/nacl/nacl_threads.c \
	src/arch/nacl/nacl_dnd.c \
	src/networking/net_pepper.c \
	src/networking/asyncio_pepper.c \
	src/ui/glw/glw_video_yuvp.c \
	src/ui/glw/glw_video_tex.c \

${PROG}.pexe: ${PROG}.bundle
	${FINALIZE} -o $@ $<

${BUILDDIR}/stage/%: support/nacl/%
	@mkdir -p $(dir $@)
	cp $< $@

${BUILDDIR}/stage/app.pexe: ${PROG}.pexe
	@mkdir -p $(dir $@)
	cp $< $@

${BUILDDIR}/stage/app.bundle: ${PROG}.bundle
	@mkdir -p $(dir $@)
	cp $< $@

${BUILDDIR}/stage/manifest.json: support/nacl/manifest.json
	@mkdir -p $(dir $@)
	sed <$< >$@ -e "s/__VERSION__/$(shell git describe | sed -e 's/-g.*//' -e 's/-/./g')/"

STAGEFILES = \
	${BUILDDIR}/stage/app.pexe \
	${BUILDDIR}/stage/index.html \
	${BUILDDIR}/stage/app.css \
	${BUILDDIR}/stage/app.js \
	${BUILDDIR}/stage/app.nmf \
	${BUILDDIR}/stage/manifest.json \
	${BUILDDIR}/stage/background.js \
	${BUILDDIR}/stage/st128.png \
	${BUILDDIR}/stage/st16.png \


.PHONY: stage dbgstage dist
stage:	${STAGEFILES}

dbgstage:	${STAGEFILES} ${BUILDDIR}/stage/app.bundle

VERSION := $(shell support/getver.sh)

DISTARCHIVE := ${BUILDDIR}/${APPNAMEUSER}-${VERSION}.zip

${DISTARCHIVE}: ${STAGEFILES}
	rm -f ${DISTARCHIVE}
	zip -j ${DISTARCHIVE} ${BUILDDIR}/stage/*

dist: ${DISTARCHIVE}
