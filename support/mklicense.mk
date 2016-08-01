LICENSEDEPS = \
	support/mklicense.mk \
	src/LICENSE \
	ext/duktape/LICENSE.txt \
	ext/libav/LICENSE \
	licenses/gumbo.txt \
	licenses/freetype.txt \
	licenses/bspatch.txt \

ifeq ($(CONFIG_LIBNTFS), yes)
LICENSEDEPS += ext/libntfs_ext/LICENSE_LIBNTFS
endif

${BUILDDIR}/LICENSE: ${LICENSEDEPS}
	echo >$@ "=================================================="
	echo >>$@ "${APPNAMEUSER} License"
	echo >>$@ "=================================================="

	cat >>$@ src/LICENSE
	echo >>$@ ""
	echo >>$@ "${APPNAMEUSER} is made possible by the following list of free/open source software"

	echo >>$@ "\f"
	cat >>$@ ext/duktape/LICENSE.txt
	cat >>$@ ext/duktape/AUTHORS.rst

	echo >>$@ "\f"
	cat >>$@ licenses/freetype.txt

	echo >>$@ "\f\n============================"
	echo >>$@ "nanosvg\n============================"
	cat >>$@ licenses/nanosvg.txt

	echo >>$@ "\f"
	cat >>$@ ext/libav/LICENSE

ifeq ($(CONFIG_GUMBO), yes)
	echo >>$@ "\f\n============================"
	echo >>$@ "gumbo\n============================"
	cat >>$@ licenses/gumbo.txt
endif
ifeq ($(CONFIG_LIBNTFS), yes)
	echo >>$@ "\f\n============================"
	echo >>$@ "libntfs\n============================"
	cat >>$@ ext/libntfs_ext/LICENSE_LIBNTFS
endif
ifeq ($(CONFIG_BSPATCH), yes)
	echo >>$@ "\f\n============================"
	echo >>$@ "bspatch\n============================"
	cat >>$@ licenses/bspatch.txt
endif
	echo >>$@ "-- End of License --"



${BUILDDIR}/LICENSE.ps: ${BUILDDIR}/LICENSE support/mklicense.mk
	paps --font="Monospace 10" $< >$@

${BUILDDIR}/LICENSE.pdf: ${BUILDDIR}/LICENSE.ps support/mklicense.mk
	ps2pdfwr -sPAPERSIZE=a4 -dOptimize=true -dEmbedAllFonts=true $< $@


