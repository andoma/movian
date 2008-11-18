-include ../config.mak

# core

SRCS = 	main.c navigator.c media.c event.c keyring.c settings.c prop.c

# file access subsys

VPATH += fileaccess
SRCS  += fileaccess.c fa_probe.c  fa_imageloader.c fa_rawloader.c
SRCS  += fa_fs.c fa_rar.c fa_smb.c


# video playback subsys

VPATH += video
SRCS  += video_playback.c 
SRCS  += video_decoder.c video_widget.c video_menu.c \
	 gl_dvdspu.c yadif.c subtitles.c


# audio subsys

VPATH += audio
SRCS  += audio.c audio_decoder.c audio_fifo.c audio_iec958.c \
	 audio_mixer.c

# ALSA Audio support
VPATH += audio/alsa
SRCS  += alsa_audio.c

# Dummy Audio support (no output)
VPATH += audio/dummy
SRCS  += dummy_audio.c

# Human Interface Devices

VPATH += hid
SRCS  += hid.c lircd.c imonpad.c keymapper.c

VPATH += backends
SRCS  += be_file.c be_page.c

# Main menu

#VPATH += apps/mainmenu
#SRCS  += mainmenu.c

# Launcher application

#VPATH += apps/launcher
#SRCS  += launcher.c

# Settings application

#VPATH += apps/settings
#SRCS  += settings.c settings_ui.c

# Browser application

#VPATH += apps/browser
#SRCS  += browser.c browser_view.c browser_probe.c \
#	 browser_slideshow.c useraction.c

# CD application

#VPATH += apps/cd
#SRCS  += cd.c 

# DVD application

#VPATH += apps/dvdplayer
#SRCS  += dvd.c dvd_mpeg.c

# Playlist application

#VPATH += apps/playlist
#SRCS  += playlist.c playlist_player.c playlist_scanner.c

# RSS browser applcation

#VPATH += apps/rss
#SRCS  += rss.c rssbrowser.c

# Radio application

#VPATH += apps/radio
#SRCS  += radio.c

# TV & headend com

#VPATH += apps/tv 
#SRCS +=	 htsp.c tv.c

# Apple Movie Trailer Application

#VPATH += apps/apple_movie_trailers
#SRCS  += movt.c

VPATH += ui
SRCS += ui.c

# glw
VPATH += ui/glw
SRCS += glw.c

SRCS += glw_x11.c

SRCS += glw_opengl.c


SRCS += glw_model.c glw_model_lexer.c glw_model_parser.c \
	glw_model_eval.c glw_model_preproc.c glw_model_support.c \
	glw_model_attrib.c
SRCS += glw_container.c glw_text.c glw_text_bitmap.c \
	glw_bitmap.c glw_tex_loader.c \
	glw_helpers.c glw_array.c \
	glw_form.c glw_rotator.c \
	glw_list.c glw_cubestack.c glw_deck.c glw_zstack.c \
	glw_expander.c glw_slideshow.c glw_scaler.c glw_event.c \
	glw_mirror.c glw_animator.c glw_transitions.c \
	glw_fx_texrot.c





PROG = showtime
MAN  = showtime.1
CFLAGS += -g -Wall -Werror -funsigned-char -O2 $(HTS_CFLAGS)

CFLAGS += -I/usr/local/include -I$(INCLUDES_INSTALL_BASE) -I$(CURDIR)
CFLAGS += -Wno-deprecated-declarations -Wmissing-prototypes
CFLAGS += -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64

LDFLAGS += -L/usr/local/lib -L$(LIBS_INSTALL_BASE) -L/usr/X11R6/lib 

#
# Locally compiled libs
# 

SLIBS += ${LIBHTS_SLIBS} ${LIBDVDNAV_SLIBS}
DLIBS += ${LIBHTS_DLIBS} ${LIBDVDNAV_DLIBS}

#
# libsmbclient
#

SLIBS += ${LIBSMBCLIENT_SLIBS}
DLIBS += ${LIBSMBCLIENT_DLIBS}

#
# curl
#

SLIBS 	+= ${LIBCURL_SLIBS}
DLIBS 	+= ${LIBCURL_DLIBS}
CFLAGS	+= ${LIBCURL_CFLAGS}

#
# freetype2
#

SLIBS 	+= ${LIBFREETYPE2_SLIBS}
DLIBS 	+= ${LIBFREETYPE2_DLIBS}
CFLAGS	+= ${LIBFREETYPE2_CFLAGS}

# CD audio

SLIBS 	+= ${LIBCDIO_CDDA_SLIBS}
DLIBS 	+= ${LIBCDIO_CDDA_DLIBS}
CFLAGS 	+= ${LIBCDIO_CDDA_CFLAGS}

# CD database

SLIBS 	+= ${LIBCDDB_SLIBS}
DLIBS 	+= ${LIBCDDB_DLIBS}
CFLAGS 	+= ${LIBCDDB_CFLAGS}

# asound (alsa)

SLIBS 	+= ${LIBASOUND_SLIBS}
DLIBS 	+= ${LIBASOUND_DLIBS}
CFLAGS 	+= ${LIBASOUND_CFLAGS}

# XML

SLIBS 	+= ${LIBXML2_SLIBS}
DLIBS 	+= ${LIBXML2_DLIBS}
CFLAGS 	+= ${LIBXML2_CFLAGS}

# EXIF

SLIBS 	+= ${LIBEXIF_SLIBS}
DLIBS 	+= ${LIBEXIF_DLIBS}
CFLAGS 	+= ${LIBEXIF_CFLAGS}


#
# ffmpeg
#

DLIBS  += $(FFMPEG_DLIBS)
SLIBS  += $(FFMPEG_SLIBS)
CFLAGS += $(FFMPEG_CFLAGS)

# OpenGL 
CFLAGS += -DGL_GLEXT_PROTOTYPES -DGLX_GLXEXT_PROTOTYPES
DLIBS  += $(LIBGL_DLIBS)

# Other X11
CFLAGS += $(LIBXRANDR_CFLAGS)
SLIBS  += $(LIBXRANDR_SLIBS) $(LIBNVCTRL_SLIBS)

# Misc

DLIBS += -lm -lz -ldl -lpthread

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
