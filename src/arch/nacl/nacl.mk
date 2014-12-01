.DEFAULT_GOAL := ${PROG}.pexe

SRCS += src/arch/nacl/nacl_main.c \
	src/arch/nacl/nacl_misc.c \
	src/arch/nacl/nacl_fs.c \
	src/arch/nacl/nacl_threads.c \
	src/audio2/dummy_audio.c \
	src/networking/net_pepper.c \
	src/networking/asyncio_pepper.c \

${PROG}.pexe: ${PROG}.bundle
	${FINALIZE} -o $@ $<
