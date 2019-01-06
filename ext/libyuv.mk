include ${BUILDDIR}/config.mak

BD=${BUILDDIR}/libyuv/build

CFLAGS = ${OPTFLAGS} -g -funsigned-char -D_FILE_OFFSET_BITS=64 ${CFLAGS_arch}
CFLAGS += -Wno-unused-result
CFLAGS += -I${C}/ext/libyuv/include

SRCS += \
	ext/libyuv/source/compare.cc \
	ext/libyuv/source/compare_common.cc \
	ext/libyuv/source/compare_gcc.cc \
	ext/libyuv/source/compare_mmi.cc \
	ext/libyuv/source/compare_msa.cc \
	ext/libyuv/source/compare_neon64.cc \
	ext/libyuv/source/compare_neon.cc \
	ext/libyuv/source/compare_win.cc \
	ext/libyuv/source/convert_argb.cc \
	ext/libyuv/source/convert.cc \
	ext/libyuv/source/convert_from_argb.cc \
	ext/libyuv/source/convert_from.cc \
	ext/libyuv/source/convert_jpeg.cc \
	ext/libyuv/source/convert_to_argb.cc \
	ext/libyuv/source/convert_to_i420.cc \
	ext/libyuv/source/cpu_id.cc \
	ext/libyuv/source/mjpeg_decoder.cc \
	ext/libyuv/source/mjpeg_validate.cc \
	ext/libyuv/source/planar_functions.cc \
	ext/libyuv/source/rotate_any.cc \
	ext/libyuv/source/rotate_argb.cc \
	ext/libyuv/source/rotate.cc \
	ext/libyuv/source/rotate_common.cc \
	ext/libyuv/source/rotate_gcc.cc \
	ext/libyuv/source/rotate_mmi.cc \
	ext/libyuv/source/rotate_msa.cc \
	ext/libyuv/source/rotate_neon64.cc \
	ext/libyuv/source/rotate_neon.cc \
	ext/libyuv/source/rotate_win.cc \
	ext/libyuv/source/row_any.cc \
	ext/libyuv/source/row_common.cc \
	ext/libyuv/source/row_gcc.cc \
	ext/libyuv/source/row_mmi.cc \
	ext/libyuv/source/row_msa.cc \
	ext/libyuv/source/row_neon64.cc \
	ext/libyuv/source/row_neon.cc \
	ext/libyuv/source/row_win.cc \
	ext/libyuv/source/scale_any.cc \
	ext/libyuv/source/scale_argb.cc \
	ext/libyuv/source/scale.cc \
	ext/libyuv/source/scale_common.cc \
	ext/libyuv/source/scale_gcc.cc \
	ext/libyuv/source/scale_mmi.cc \
	ext/libyuv/source/scale_msa.cc \
	ext/libyuv/source/scale_neon64.cc \
	ext/libyuv/source/scale_neon.cc \
	ext/libyuv/source/scale_win.cc \
	ext/libyuv/source/video_common.cc \

OBJS=    $(SRCS:%.cc=$(BUILDDIR)/%.o)
DEPS=    ${OBJS:%.o=%.d}


${BD}/include/libyuv.h: ext/libyuv/include/libyuv.h
	@mkdir -p $(dir $@)/libyuv
	cp ext/libyuv/include/libyuv/* $(dir $@)/libyuv/
	cp $< $@

${BD}/lib/libyuv.a: $(OBJS)
	@mkdir -p $(dir $@)
	$(AR) rc $@ $(OBJS)
	$(RANLIB) $@

${BUILDDIR}/%.o: ${C}/%.cc
	@mkdir -p $(dir $@)
	$(CXX) -MD -MP $(CFLAGS) -c -o $@ $<

build: ${BD}/lib/libyuv.a ${BD}/include/libyuv.h
	@mkdir -p ${EXT_INSTALL_DIR}/include/libyuv
	cp ext/libyuv/include/libyuv/* ${EXT_INSTALL_DIR}/include/libyuv/
	cp ext/libyuv/include/libyuv.h ${EXT_INSTALL_DIR}/include/
	cp -pP ${BD}/lib/libyuv.* ${EXT_INSTALL_DIR}/lib/
