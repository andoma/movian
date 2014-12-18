.DEFAULT_GOAL := dist

SRCS += src/arch/nacl/nacl_main.c \
	src/arch/nacl/nacl_misc.c \
	src/arch/nacl/nacl_fs.c \
	src/arch/nacl/nacl_audio.c \
	src/arch/nacl/nacl_threads.c \
	src/networking/net_pepper.c \
	src/networking/asyncio_pepper.c \
	src/ui/glw/glw_video_tex.c \

${PROG}.pexe: ${PROG}.bundle
	${FINALIZE} -o $@ $<

${BUILDDIR}/dist/%: support/nacl/%
	@mkdir -p $(dir $@)
	cp $< $@

${BUILDDIR}/dist/showtime.pexe: ${PROG}.pexe
	@mkdir -p $(dir $@)
	cp $< $@

${BUILDDIR}/dist/showtime.bundle: ${PROG}.bundle
	@mkdir -p $(dir $@)
	cp $< $@

DISTFILES = \
	${BUILDDIR}/dist/showtime.pexe \
	${BUILDDIR}/dist/index.html \
	${BUILDDIR}/dist/showtime.css \
	${BUILDDIR}/dist/showtime.js \
	${BUILDDIR}/dist/README \
	${BUILDDIR}/dist/showtime.nmf \


.PHONY: dist dbgdist
dist:	${DISTFILES}

dbgdist:	${DISTFILES} ${BUILDDIR}/dist/showtime.bundle
