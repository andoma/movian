run:
	${STRIP} -o ${BUILDDIR}/showtime.stripped ${BUILDDIR}/showtime
	wiisupport/devkitpro/devkitPPC/bin/wiiload ${BUILDDIR}/showtime.stripped ${WIIRUNOPTS}

${BUILDDIR}/%.o: %.S
	$(CC) -MD -MP -D_LANGUAGE_ASSEMBLY -Wa,-mgekko -c -o $@ $(CURDIR)/$<
