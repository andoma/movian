#
#  Showtime mediacenter
#  Copyright (C) 2007-2011 Andreas Öman
#
#  This program is free software: you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, either version 3 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program.  If not, see <http://www.gnu.org/licenses/>.

.SUFFIXES:
SUFFIXES=

include ${CURDIR}/config.default

OPTFLAGS ?= -O2

BUILDDIR = build.${BUILD}

include ${BUILDDIR}/config.mak

CFLAGS  = -Wall -Werror -Wwrite-strings -Wno-deprecated-declarations 
CFLAGS += -Wmissing-prototypes -Iext/dvd ${OPTFLAGS}

include sources.mk


# File specific CFLAGS

${BUILDDIR}/ext/sqlite/sqlite3.o : CFLAGS = -O2 ${SQLITE_CFLAGS_cfg} \
 -DSQLITE_THREADSAFE=2 \
 -DSQLITE_OMIT_UTF16 \
 -DSQLITE_OMIT_AUTOINIT \
 -DSQLITE_OMIT_COMPLETE \
 -DSQLITE_OMIT_DECLTYPE \
 -DSQLITE_OMIT_DEPRECATED \
 -DSQLITE_OMIT_EXPLAIN \
 -DSQLITE_OMIT_GET_TABLE \
 -DSQLITE_OMIT_TCL_VARIABLE \
 -DSQLITE_OMIT_LOAD_EXTENSION \
 -DSQLITE_DEFAULT_FOREIGN_KEYS=1 \
 -DSQLITE_ENABLE_UNLOCK_NOTIFY \


${BUILDDIR}/src/sd/avahi.o : CFLAGS = $(CFLAGS_AVAHI) -Wall -Werror  ${OPTFLAGS}

${BUILDDIR}/src/ui/gu/%.o : CFLAGS = $(CFLAGS_GTK) ${OPTFLAGS} \
-Wall -Werror -Wmissing-prototypes -Wno-cast-qual -Wno-deprecated-declarations

${BUILDDIR}/ext/librtmp/%.o : CFLAGS = ${OPTFLAGS}

${BUILDDIR}/src/backend/rtmp/rtmp.o : CFLAGS = ${OPTFLAGS} -Wall -Werror -Iext

ifeq ($(PLATFORM), osx)
DVDCSS_CFLAGS = -DDARWIN_DVD_IOCTL -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE
else
ifeq ($(PLATFORM), linux)
DVDCSS_CFLAGS = -DHAVE_LINUX_DVD_STRUCT -DDVD_STRUCT_IN_LINUX_CDROM_H -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE
endif
endif


${BUILDDIR}/ext/dvd/dvdcss/%.o : CFLAGS = ${OPTFLAGS} \
 -DHAVE_LIMITS_H -DHAVE_UNISTD_H -DHAVE_ERRNO_H -DVERSION="0" $(DVDCSS_CFLAGS)

${BUILDDIR}/ext/dvd/libdvdread/%.o : CFLAGS = ${OPTFLAGS} \
 -DHAVE_DVDCSS_DVDCSS_H -DDVDNAV_COMPILE -Wno-strict-aliasing  -Iext/dvd 

${BUILDDIR}/ext/dvd/dvdnav/%.o : CFLAGS = ${OPTFLAGS} \
 -DVERSION=\"showtime\" -DDVDNAV_COMPILE -Wno-strict-aliasing -Iext/dvd \
 -Iext/dvd/dvdnav

${BUILDDIR}/ext/spidermonkey/%.o : CFLAGS = \
	-Iext/spidermonkey -Isrc/arch/nspr

CFLAGS_com += -DXP_UNIX -DJS_HAS_XML_SUPPORT -DJS_THREADSAFE

${BUILDDIR}/ext/polarssl-0.14.0/library/%.o : CFLAGS = -Wall ${OPTFLAGS}

ifeq ($(CONFIG_POLARSSL), yes)
CFLAGS_com += -Iext/polarssl-0.14.0/include
endif


# Various transformations
SRCS  += $(SRCS-yes)
DLIBS += $(DLIBS-yes)
SLIBS += $(SLIBS-yes)
SSRCS  = $(sort $(SRCS))
OBJS4=   $(SSRCS:%.cpp=$(BUILDDIR)/%.o)
OBJS3=   $(OBJS4:%.S=$(BUILDDIR)/%.o)
OBJS2=   $(OBJS3:%.c=$(BUILDDIR)/%.o)
OBJS=    $(OBJS2:%.m=$(BUILDDIR)/%.o)
DEPS=    ${OBJS:%.o=%.d}
OBJDIRS= $(sort $(dir $(OBJS)))

# File bundles
BUNDLES += $(sort $(BUNDLES-yes))
BUNDLE_SRCS=$(BUNDLES:%=$(BUILDDIR)/bundles/%.c)
BUNDLE_DEPS=$(BUNDLE_SRCS:%.c=%.d)
BUNDLE_OBJS=$(BUNDLE_SRCS:%.c=%.o)
OBJDIRS+= $(sort $(dir $(BUNDLE_OBJS)))
.PRECIOUS: ${BUNDLE_SRCS}

# Common CFLAGS for all files
CFLAGS_com += -g -funsigned-char ${OPTFLAGS} ${CFLAGS_dbg}
CFLAGS_com += -D_FILE_OFFSET_BITS=64
CFLAGS_com += -iquote${BUILDDIR} -iquote${CURDIR}/src -iquote${CURDIR}

# Tools

MKBUNDLE = $(CURDIR)/support/mkbundle

ifndef V
ECHO   = printf "$(1)\t%s\n" $(2)
BRIEF  = CC MKBUNDLE CXX STRIP
MSG    = $@
$(foreach VAR,$(BRIEF), \
    $(eval $(VAR) = @$$(call ECHO,$(VAR),$$(MSG)); $($(VAR))))
endif

all:	makever ${PROG}

.PHONY:	clean distclean makever

${PROG}: ${FFBUILDDEP} $(OBJDIRS) $(OBJS) $(BUNDLE_OBJS) Makefile src/version.c
	$(CC) -o $@ $(OBJS) $(BUNDLE_OBJS) $(LDFLAGS) ${LDFLAGS_cfg}

$(OBJDIRS):
	@mkdir -p $@

${BUILDDIR}/%.o: %.c ${BUILDDIR}/config.mak Makefile
	$(CC) -MD -MP $(CFLAGS_com) $(CFLAGS) $(CFLAGS_cfg) -c -o $@ $(CURDIR)/$<

${BUILDDIR}/%.o: %.m ${BUILDDIR}/config.mak Makefile
	$(CC) -MD -MP $(CFLAGS_com) $(CFLAGS) $(CFLAGS_cfg) -c -o $@ $(CURDIR)/$<

${BUILDDIR}/%.o: %.cpp ${BUILDDIR}/config.mak Makefile
	$(CXX) -MD -MP $(CFLAGS_com) $(CFLAGS_cfg) -c -o $@ $(CURDIR)/$<

clean:
	rm -rf ${BUILDDIR}/src ${BUILDDIR}/ext ${BUILDDIR}/bundles
	find . -name "*~" | xargs rm -f

distclean: clean
	rm -rf build.*

reconfigure:
	$(CURDIR)/configure.${CONFIGURE_POSTFIX} $(CONFIGURE_ARGS)

showconfig:
	@echo $(CONFIGURE_ARGS)

${PROG}.stripped: ${PROG}
	${STRIP} -o $@ $<

strip: ${PROG}.stripped


# Create showtimeversion.h
src/version.c: $(BUILDDIR)/showtimeversion.h

makever:
	@$(CURDIR)/support/version.sh $(CURDIR) $(BUILDDIR)/showtimeversion.h


# Include dependency files if they exist.
-include $(DEPS) $(BUNDLE_DEPS)

# Include Platform specific targets
include support/${PLATFORM}.mk

# Bundle files
$(BUILDDIR)/bundles/%.o: $(BUILDDIR)/bundles/%.c Makefile
	$(CC) -I${CURDIR}/src/fileaccess -c -o $@ $<

$(BUILDDIR)/bundles/%.c: % $(CURDIR)/support/mkbundle Makefile
	$(MKBUNDLE) -o $@ -s $< -d ${BUILDDIR}/bundles/$<.d -p $<
