include ${BUILDDIR}/config.mak

BD=${BUILDDIR}/xmp/build

CFLAGS = ${OPTFLAGS} -g -funsigned-char -D_FILE_OFFSET_BITS=64
CFLAGS += -Wno-unused-result
CFLAGS += -I${C}/ext/xmp/include
CFLAGS += -I${C}/ext/xmp/src
CFLAGS += ${CFLAGS_ext}

SRCS += \
	ext/xmp/src/adlib.c \
	ext/xmp/src/effects.c \
	ext/xmp/src/format.c \
	ext/xmp/src/load_helpers.c \
	ext/xmp/src/med_synth.c \
	ext/xmp/src/mixer.c \
	ext/xmp/src/period.c \
	ext/xmp/src/scan.c \
	ext/xmp/src/virtual.c \
	ext/xmp/src/control.c \
	ext/xmp/src/envelope.c \
	ext/xmp/src/fmopl.c \
	ext/xmp/src/lfo.c \
	ext/xmp/src/misc.c \
	ext/xmp/src/mkstemp.c \
	ext/xmp/src/player.c \
	ext/xmp/src/spectrum.c \
	ext/xmp/src/vorbis.c \
	ext/xmp/src/dataio.c \
	ext/xmp/src/filter.c \
	ext/xmp/src/fnmatch.c \
	ext/xmp/src/load.c \
	ext/xmp/src/md5.c \
	ext/xmp/src/mix_all.c \
	ext/xmp/src/oxm.c \
	ext/xmp/src/read_event.c \
	ext/xmp/src/synth_null.c \
	ext/xmp/src/ym2149.c \
	ext/xmp/src/loaders/669_load.c \
	ext/xmp/src/loaders/alm_load.c \
	ext/xmp/src/loaders/amd_load.c \
	ext/xmp/src/loaders/amf_load.c \
	ext/xmp/src/loaders/arch_load.c \
	ext/xmp/src/loaders/asif.c \
	ext/xmp/src/loaders/asylum_load.c \
	ext/xmp/src/loaders/coco_load.c \
	ext/xmp/src/loaders/common.c \
	ext/xmp/src/loaders/dbm_load.c \
	ext/xmp/src/loaders/digi_load.c \
	ext/xmp/src/loaders/dmf_load.c \
	ext/xmp/src/loaders/dt_load.c \
	ext/xmp/src/loaders/dtt_load.c \
	ext/xmp/src/loaders/emod_load.c \
	ext/xmp/src/loaders/far_load.c \
	ext/xmp/src/loaders/flt_load.c \
	ext/xmp/src/loaders/fnk_load.c \
	ext/xmp/src/loaders/gal4_load.c \
	ext/xmp/src/loaders/gal5_load.c \
	ext/xmp/src/loaders/gdm_load.c \
	ext/xmp/src/loaders/gtk_load.c \
	ext/xmp/src/loaders/hsc_load.c \
	ext/xmp/src/loaders/ice_load.c \
	ext/xmp/src/loaders/iff.c \
	ext/xmp/src/loaders/imf_load.c \
	ext/xmp/src/loaders/ims_load.c \
	ext/xmp/src/loaders/it_load.c \
	ext/xmp/src/loaders/itsex.c \
	ext/xmp/src/loaders/liq_load.c \
	ext/xmp/src/loaders/masi_load.c \
	ext/xmp/src/loaders/mdl_load.c \
	ext/xmp/src/loaders/med2_load.c \
	ext/xmp/src/loaders/med3_load.c \
	ext/xmp/src/loaders/med4_load.c \
	ext/xmp/src/loaders/mfp_load.c \
	ext/xmp/src/loaders/mgt_load.c \
	ext/xmp/src/loaders/mmd1_load.c \
	ext/xmp/src/loaders/mmd3_load.c \
	ext/xmp/src/loaders/mmd_common.c \
	ext/xmp/src/loaders/mod_load.c \
	ext/xmp/src/loaders/mtm_load.c \
	ext/xmp/src/loaders/no_load.c \
	ext/xmp/src/loaders/okt_load.c \
	ext/xmp/src/loaders/polly_load.c \
	ext/xmp/src/loaders/psm_load.c \
	ext/xmp/src/loaders/pt3_load.c \
	ext/xmp/src/loaders/ptm_load.c \
	ext/xmp/src/loaders/rad_load.c \
	ext/xmp/src/loaders/rtm_load.c \
	ext/xmp/src/loaders/s3m_load.c \
	ext/xmp/src/loaders/sample.c \
	ext/xmp/src/loaders/sfx_load.c \
	ext/xmp/src/loaders/ssmt_load.c \
	ext/xmp/src/loaders/stc_load.c \
	ext/xmp/src/loaders/stim_load.c \
	ext/xmp/src/loaders/st_load.c \
	ext/xmp/src/loaders/stm_load.c \
	ext/xmp/src/loaders/stx_load.c \
	ext/xmp/src/loaders/tcb_load.c \
	ext/xmp/src/loaders/ult_load.c \
	ext/xmp/src/loaders/umx_load.c \
	ext/xmp/src/loaders/voltable.c \
	ext/xmp/src/loaders/xm_load.c \

OBJS=    $(SRCS:%.c=$(BUILDDIR)/%.o)
DEPS=    ${OBJS:%.o=%.d}


${BD}/include/xmp.h: ext/xmp/include/xmp.h
	@mkdir -p $(dir $@)
	cp $< $@

${BD}/lib/libxmp.a: $(OBJS)
	@mkdir -p $(dir $@)
	$(AR) rc $@ $(OBJS)
	$(RANLIB) $@

${BUILDDIR}/%.o: ${C}/%.c
	@mkdir -p $(dir $@)
	$(CC) -MD -MP $(CFLAGS) -c -o $@ $<

build: ${BD}/lib/libxmp.a ${BD}/include/xmp.h
	cp ${BD}/include/xmp.h ${EXT_INSTALL_DIR}/include/
	cp -pP ${BD}/lib/libxmp.* ${EXT_INSTALL_DIR}/lib/
