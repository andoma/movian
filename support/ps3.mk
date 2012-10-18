.DEFAULT_GOAL := ${PROG}

#
# Source modification and extra flags
#
SRCS += src/arch/ps3/ps3_main.c \
	src/arch/ps3/ps3_threads.c \
	src/arch/ps3/ps3_trap.c \
	src/networking/net_psl1ght.c \
	src/audio/ps3/ps3_audio.c \
	ext/tlsf/tlsf_ps3.c \

#
# Install
# 

TITLE := Showtime
VERSION := $(shell support/getver.sh)
SFO := $(PSL1GHT)/host/bin/sfo.py
PKG := $(PSL1GHT)/host/bin/pkg.py
ICON0 := $(TOPDIR)/support/ps3icon.png
APPID		:=	HTSS00003
CONTENTID	:=	UP0001-$(APPID)_00-0000000000000000

SFOXML          := $(TOPDIR)/support/sfo.xml

EBOOT=${BUILDDIR}/EBOOT.BIN

ELF=${BUILDDIR}/showtime.elf
SELF=${BUILDDIR}/showtime.self
SYMS=${BUILDDIR}/showtime.syms
ZS=${BUILDDIR}/showtime.zs

${EBOOT}: support/ps3/eboot.c support/ps3.mk
	$(CC) $(CFLAGS_com) $(CFLAGS) $(CFLAGS_cfg)  -o $@ $< ${LDFLAGS_EBOOT}
	${STRIP} $@
	sprxlinker $@

${ELF}: ${BUILDDIR}/showtime.ziptail support/ps3.mk
	${STRIP} -o $@ $<
	sprxlinker $@

${SYMS}: ${BUILDDIR}/showtime.ziptail support/ps3.mk
	${OBJDUMP} -t -j .text $< | awk '{print $$1 " " $$NF}'|sort >$@

${ZS}:  ${BUILDDIR}/zipbundles/bundle.zip ${SYMS} support/ps3.mk
	cp $< $@
	zip -9j ${ZS} ${SYMS}

${SELF}: ${ELF} ${ZS} support/ps3.mk 
	make_self $< $@
	cat ${ZS} >>$@

$(BUILDDIR)/pkg/USRDIR/showtime.self: ${SELF}  support/ps3.mk
	cp $< $@

$(BUILDDIR)/pkg/USRDIR/EBOOT.BIN: ${EBOOT}  support/ps3.mk
	@mkdir -p $(BUILDDIR)/pkg/USRDIR
	make_self_npdrm $< $@ $(CONTENTID)

$(BUILDDIR)/showtime.pkg: $(BUILDDIR)/pkg/USRDIR/EBOOT.BIN $(BUILDDIR)/pkg/USRDIR/showtime.self
	cp $(ICON0) $(BUILDDIR)/pkg/ICON0.PNG
	$(SFO) --title "$(TITLE)" --appid "$(APPID)" -f $(SFOXML) $(BUILDDIR)/pkg/PARAM.SFO
	$(PKG) --contentid $(CONTENTID) $(BUILDDIR)/pkg/ $@

$(BUILDDIR)/showtime_geohot.pkg: $(BUILDDIR)/showtime.pkg  support/ps3.mk
	cp $< $@
	package_finalize $@

pkg: $(BUILDDIR)/showtime.pkg $(BUILDDIR)/showtime_geohot.pkg
self: ${SELF}

install: $(BUILDDIR)/showtime.pkg
	cp $< $(PS3INSTALL)/showtime.pkg
	sync

$(BUILDDIR)/dist/showtime-$(VERSION).self: ${SELF}
	@mkdir -p $(dir $@)
	cp $< $@

$(BUILDDIR)/dist/showtime-$(VERSION).pkg: $(BUILDDIR)/showtime.pkg
	@mkdir -p $(dir $@)
	cp $< $@

$(BUILDDIR)/dist/showtime_geohot-$(VERSION).pkg: $(BUILDDIR)/showtime_geohot.pkg
	@mkdir -p $(dir $@)
	cp $< $@

dist:  $(BUILDDIR)/dist/showtime-$(VERSION).self $(BUILDDIR)/dist/showtime-$(VERSION).pkg $(BUILDDIR)/dist/showtime_geohot-$(VERSION).pkg

upgrade: ${SELF}
	curl --data-binary @$< http://$(PS3HOST):42000/showtime/replace
