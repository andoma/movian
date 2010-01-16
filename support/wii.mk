STRIPBIN=${BUILDDIR}/showtime.stripped

${STRIPBIN}: ${BUILDDIR}/showtime
	${STRIP} -o $@ $<

run: ${STRIPBIN}
	wiisupport/devkitpro/devkitPPC/bin/wiiload ${STRIPBIN} ${WIIRUNOPTS}

${BUILDDIR}/%.o: %.S
	$(CC) -MD -MP -D_LANGUAGE_ASSEMBLY -Wa,-mgekko -c -o $@ $(CURDIR)/$<

PKGVER=$(shell ${TOPDIR}/support/getver.sh)

homebrew: ${STRIPBIN}
	@mkdir -p ${BUILDDIR}/bundle/showtime
	@rm    -f ${BUILDDIR}/bundle/showtime/*
	@rm    -f ${BUILDDIR}/bundle/showtime*.zip
	@cp ${TOPDIR}/support/homebrew/icon.png ${BUILDDIR}/bundle/showtime/
	@sed <${TOPDIR}/support/homebrew/meta.xml.in >${BUILDDIR}/bundle/showtime/meta.xml \
		s/@@VERSION@@/${PKGVER}/
	@cp ${STRIPBIN} ${BUILDDIR}/bundle/showtime/boot.elf

	@cd ${BUILDDIR}/bundle && zip -r showtime-${PKGVER}.zip showtime
	@echo "Zip archive created: ${BUILDDIR}/bundle/showtime-${PKGVER}.zip"
