-include ../config.mak

# core

SRCS = 	main.c navigator.c media.c event.c keyring.c settings.c prop.c

VPATH += arch
SRCS  += arch_${ARCHITECTURE}.c


#
# File access subsys
#
VPATH += fileaccess
SRCS  += fileaccess.c fa_probe.c  fa_imageloader.c fa_rawloader.c fa_backend.c
SRCS  += fa_fs.c fa_rar.c fa_smb.c fa_http.c fa_zip.c fa_zlib.c fa_embedded.c

SRCS-$(CONFIG_EMBEDDED_THEME)  += embedded_theme.c

#
# Networking
#
VPATH += networking
SRCS-$(CONFIG_POSIX_NETWORKING) += net_posix.c


#
# Video support
#
VPATH += video
SRCS  += video_playback.c video_decoder.c yadif.c

#
# Audio subsys
#
VPATH += audio
SRCS  += audio.c audio_decoder.c audio_fifo.c audio_iec958.c audio_mixer.c

# ALSA Audio support
VPATH += audio/alsa
SRCS-$(CONFIG_LIBASOUND)  += alsa_audio.c

# Dummy Audio support (no output)
VPATH += audio/dummy
SRCS  += dummy_audio.c


#
# Various backends
#
VPATH += backends
SRCS  += be_page.c

#
# Playqueue
#
SRCS  += playqueue.c

#
# User interface common
#
VPATH += ui
SRCS += ui.c  keymapper.c

#
# LIRC
#
#VPATH += ui/lirc
#SRCS  += lirc.c lircd.c imonpad.c

#
# GLW user interface
#
VPATH += ui/glw

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


PROG = showtime
MAN  = showtime.1
CFLAGS += -g -Wall -Werror -funsigned-char -O2
CFLAGS += -Wno-deprecated-declarations -Wmissing-prototypes
CFLAGS += -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64
CFLAGS += -I$(CURDIR) -I$(INCLUDES_INSTALL_BASE)
LDFLAGS += -L$(LIBS_INSTALL_BASE)
#
# 
#
DLIBS  += ${SHOWTIME_DLIBS}  ${HTS_DLIBS}
SLIBS  += ${SHOWTIME_SLIBS}  ${HTS_SLIBS}
CFLAGS += ${SHOWTIME_CFLAGS} ${HTS_CFLAGS}

include ../build/prog.mk

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
