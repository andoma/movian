
BUILDDIR=/home/andoma/hts/showtime/build.Linux.i686

include ${BUILDDIR}/config.mak

# core
VPATH += src/

SRCS = 	main.c navigator.c media.c event.c keyring.c settings.c prop.c \
	bookmarks.c notifications.c

# arch
VPATH += src/arch
SRCS  += arch_${ARCHITECTURE}.c
SRCS  += settings.c

# support
VPATH += src/htsmsg
SRCS  += htsbuf.c htsmsg.c htsmsg_json.c htsmsg_xml.c htsmsg_binary.c


#
# File access subsys
#
VPATH += src/fileaccess
SRCS  += fileaccess.c fa_probe.c  fa_imageloader.c fa_rawloader.c fa_backend.c
SRCS  += fa_video.c fa_audio.c
SRCS  += fa_fs.c fa_rar.c fa_smb.c fa_http.c fa_zip.c fa_zlib.c fa_embedded.c

SRCS-$(CONFIG_EMBEDDED_THEME)  += embedded_theme.c

#
# Networking
#
VPATH += src/networking
SRCS += net_common.c
SRCS-$(CONFIG_POSIX_NETWORKING) += net_posix.c
SRCS-$(CONFIG_LIBOGC) += net_libogc.c


#
# Video support
#
VPATH += src/video
SRCS  += video_playback.c video_decoder.c yadif.c
SRCS-$(CONFIG_DVDNAV) += video_dvdspu.c

#
# Audio subsys
#
VPATH += src/audio
SRCS  += audio.c audio_decoder.c audio_fifo.c audio_iec958.c audio_mixer.c

# ALSA Audio support
VPATH += src/audio/alsa
SRCS-$(CONFIG_LIBASOUND)  += alsa_audio.c

# Wii Audio support (no output)
VPATH += src/audio/wii
SRCS-$(CONFIG_LIBOGC)     += wii_audio.c

# Dummy Audio support (no output)
VPATH += src/audio/dummy
SRCS  += dummy_audio.c

#
# Playqueue
#
SRCS  += playqueue.c

#
# DVD
#
VPATH += src/dvd
SRCS-$(CONFIG_DVDNAV)  += dvd.c

# HTSP
#
VPATH += src/tv
SRCS  += htsp.c

#
# User interface common
#
VPATH += src/ui
SRCS += ui.c  keymapper.c

#
# LIRC
#
#VPATH += ui/lirc
#SRCS  += lirc.c lircd.c imonpad.c

#
# GLW user interface
#
VPATH += src/ui/glw

SRCS-$(CONFIG_GLW)     += glw.c \
			glw_event.c \
			glw_model.c \
		     	glw_model_lexer.c \
		     	glw_model_parser.c \
			glw_model_eval.c \
			glw_model_preproc.c \
			glw_model_support.c \
			glw_model_attrib.c \
			glw_container.c \
			glw_stack.c \
			glw_list.c \
			glw_deck.c \
			glw_layer.c \
			glw_expander.c \
			glw_slider.c \
			glw_rotator.c  \
			glw_animator.c \
			glw_transitions.c \
			glw_navigation.c \
			glw_texture_loader.c \
			glw_scaler.c  \
			glw_image.c \
			glw_text_bitmap.c \
			glw_cursor.c \

SRCS-$(CONFIG_GLW_FRONTEND_X11)	+= glw_x11.c

SRCS-$(CONFIG_GLW_BACKEND_OPENGL) += glw_opengl.c
SRCS-$(CONFIG_GLW_BACKEND_OPENGL) += glw_texture_opengl.c
SRCS-$(CONFIG_GLW_BACKEND_OPENGL) += glw_render_opengl.c
SRCS-$(CONFIG_GLW_BACKEND_OPENGL) += glw_mirror.c
SRCS-$(CONFIG_GLW_BACKEND_OPENGL) += glw_video.c
SRCS-$(CONFIG_GLW_BACKEND_OPENGL) += glw_fx_texrot.c



SRCS-$(CONFIG_GLW_FRONTEND_WII)	+= glw_wii.c

SRCS-$(CONFIG_GLW_BACKEND_GX) += glw_texture_gx.c
SRCS-$(CONFIG_GLW_BACKEND_GX) += glw_render_gx.c
SRCS-$(CONFIG_GLW_BACKEND_GX) += glw_gx.c


PROG = showtime
MAN  = showtime.1
CFLAGS += -g -Wall -Werror -funsigned-char -O2 
CFLAGS += -Wwrite-strings
CFLAGS += -Wno-deprecated-declarations -Wmissing-prototypes
CFLAGS += -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64
CFLAGS += -I${BUILDDIR} -I${CURDIR}/src -I${CURDIR}

SRCS += $(SRCS-yes)
DLIBS += $(DLIBS-yes)
SLIBS += $(SLIBS-yes)

.OBJDIR= obj
DEPFLAG= -M

OBJS=    $(patsubst %.c,  %.o,   $(SRCS))

DEPS= ${OBJS:%.o=%.d}

SRCS += version.c

PROGPATH ?= $(TOPDIR)

all:	$(PROG)

.PHONY: version.h

version.h:
	$(TOPDIR)/version.sh $(PROGPATH) $(PROGPATH)/version.h


${PROG}: version.h $(OBJS) Makefile
	cd $(.OBJDIR) && $(CC) -o $@ $(OBJS) $(LDFLAGS) 

.c.o:
	mkdir -p $(.OBJDIR) && cd $(.OBJDIR) && $(CC) -MD $(CFLAGS) -c -o $@ $(CURDIR)/$<

clean:
	rm -rf core* obj version.h
	find . -name "*~" | xargs rm -f

vpath %.o ${.OBJDIR}
vpath %.S ${.OBJDIR}
vpath ${PROG} ${.OBJDIR}
vpath ${PROGBIN} ${.OBJDIR}

# include dependency files if they exist
$(addprefix ${.OBJDIR}/, ${DEPS}): ;
-include $(addprefix ${.OBJDIR}/, ${DEPS})


include mk/${ARCHITECTURE}.mk


#
# Embedded theme
#
embedded_theme.c: ../config.mak
	find $(EMBEDDED_THEME_PATH) -type f | grep -v .svn|awk '{print "embedded_theme.c: " $$0}' > ${.OBJDIR}/embedded_theme_files.d
	rm -f $(HTS_BUILD_ROOT)/showtime/${.OBJDIR}/embedded_theme.zip
	cd $(EMBEDDED_THEME_PATH) && zip -9 -X -r $(HTS_BUILD_ROOT)/showtime/${.OBJDIR}/embedded_theme.zip . -x \*.svn\*
	@echo >$@ unsigned char embedded_theme[]={
	@cat ${.OBJDIR}/embedded_theme.zip | od -v -An -b | sed s/^\ */0/ | sed s/\ *$$/,/| sed s/\ /,\ 0/g >>$@
	@echo >>$@ "};"
	@echo >>$@ "int embedded_theme_size = sizeof(embedded_theme);"

-include obj/embedded_theme_files.d
