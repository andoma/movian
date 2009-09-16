run:
	${STRIP} -o ${BUILDDIR}/showtime.stripped ${BUILDDIR}/showtime
	wiisupport/devkitpro/devkitPPC/bin/wiiload ${BUILDDIR}/showtime.stripped
