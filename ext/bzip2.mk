include ${BUILDDIR}/config.mak

build:
	${MAKE} -C ${BZIP2_BUILD_DIR} libbz2.a
	cp ${BZIP2_BUILD_DIR}/libbz2.a ${EXT_INSTALL_DIR}/lib
	cp ${BZIP2_BUILD_DIR}/bzlib.h  ${EXT_INSTALL_DIR}/include
