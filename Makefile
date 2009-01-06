-include ../config.mak

# core

SRCS = 	main.c navigator.c media.c event.c keyring.c settings.c prop.c

VPATH += arch
SRCS  += arch_${ARCHITECTURE}.c


#
# File access subsys
#
VPATH += fileaccess
SRCS  += fileaccess.c fa_probe.c  fa_imageloader.c fa_rawloader.c
SRCS  += fa_fs.c fa_rar.c fa_smb.c fa_http.c

#
# Networking
#
VPATH += networking
SRCS-$(CONFIG_POSIX_NETWORKING) += net_posix.c


#
# Video support
#
VPATH += video
SRCS  += yadif.c

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
SRCS  += be_file.c be_page.c

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
			glw_rotator.c  \
			glw_animator.c \
			glw_transitions.c \
			glw_navigation.c \
			glw_texture_loader.c \
			glw_scaler.c  \
			glw_bitmap.c \
			glw_text_bitmap.c \
			glw_cursor.c \

SRCS-$(CONFIG_GLW_FRONTEND_X11)	+= glw_x11.c

SRCS-$(CONFIG_GLW_BACKEND_OPENGL) += glw_opengl.c
SRCS-$(CONFIG_GLW_BACKEND_OPENGL) += glw_texture_opengl.c
SRCS-$(CONFIG_GLW_BACKEND_OPENGL) += glw_mirror.c
SRCS-$(CONFIG_GLW_BACKEND_OPENGL) += glw_video.c glw_video_decoder.c
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

prefix ?= $(INSTALLPREFIX)
INSTBIN= $(prefix)/bin
INSTMAN= $(prefix)/share/man1
INSTSHARE= $(prefix)/share/hts/showtime

install: ${PROG}
	mkdir -p $(INSTBIN)
	cd $(.OBJDIR) && install -s ${PROG} $(INSTBIN)

	mkdir -p $(INSTMAN)
	cd man && install ${MAN} $(INSTMAN)

	find themes -type d |grep -v .svn | awk '{print "$(INSTSHARE)/"$$0}' | xargs mkdir -p 
	find themes -type f |grep -v .svn | awk '{print $$0 " $(INSTSHARE)/"$$0}' | xargs -n2 cp

uninstall:
	rm -f $(INSTBIN)/${PROG}
	rm -f $(INSTMAN)/${MAN}
	rm -rf $(INSTSHARE)
