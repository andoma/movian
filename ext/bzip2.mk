include ${BUILDDIR}/config.mak

build:
	${MAKE} -C ${BZIP2_BUILD_DIR} libbz2.a
	cp ${BZIP2_BUILD_DIR}/libbz2.a ${BUILDDIR}/ext/lib
	cp ${BZIP2_BUILD_DIR}/bzlib.h  ${BUILDDIR}/ext/include
