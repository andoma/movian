
SYSROOT=${NPSDK}/../ext/vmir/sysroot

BUILDDIR=build

all: ${BUILDDIR}/${PROG}.opt

CLANG=${LLVM_TOOLCHAIN}clang
CLANGXX=${LLVM_TOOLCHAIN}clang++
LINK=${LLVM_TOOLCHAIN}llvm-link
OPT=${LLVM_TOOLCHAIN}opt

OBJS2=   $(SRCS:%.cpp=$(BUILDDIR)/%.bc)
OBJS=    $(OBJS2:%.c=$(BUILDDIR)/%.bc)
DEPS=    ${OBJS:%.bc=%.d}

CFLAGS += --sysroot=${SYSROOT} -ffreestanding \
	-I${SYSROOT}/usr/include \
	-emit-llvm -target le32-unknown-nacls \
	-I${NPSDK}/include

${BUILDDIR}/${PROG}: ${OBJS}
	${LINK} -o $@ ${OBJS}

${BUILDDIR}/${PROG}.opt: ${BUILDDIR}/${PROG}
	${OPT} -O3 -adce -argpromotion -constmerge -globaldce -globalopt -disable-slp-vectorization -disable-loop-vectorization -o $@ $<

${BUILDDIR}/%.bc: %.cc Makefile
	@mkdir -p $(dir $@)
	${CLANGXX} ${CFLAGS} ${CXXFLAGS} -MD -MP -c $< -o $@

${BUILDDIR}/%.bc: %.cpp Makefile
	@mkdir -p $(dir $@)
	${CLANGXX} ${CFLAGS} ${CXXFLAGS} -MD -MP -c $< -o $@

${BUILDDIR}/%.bc: %.c Makefile
	@mkdir -p $(dir $@)
	${CLANG} ${CFLAGS} -MD -MP -c $< -o $@

clean:
	rm -rf "${BUILDDIR}"

-include $(DEPS)
