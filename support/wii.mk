run:
	${STRIP} -o ${BUILDDIR}/showtime.stripped ${BUILDDIR}/showtime
	wiiload ${BUILDDIR}/showtime.stripped
