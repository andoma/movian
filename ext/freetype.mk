FREETYPE_VER="2.4.9"
BD=${BUILDDIR}/freetype/build

build:
	${MAKE} -C ${BD}
	${MAKE} -C ${BD} install

configure:
	