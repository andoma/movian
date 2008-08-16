-include ../config.mak

# core

SRCS = 	main.c app.c media.c coms.c event.c

# file access subsys

VPATH += fileaccess
SRCS  += fileaccess.c fa_probe.c fa_tags.c fa_imageloader.c fa_rawloader.c
SRCS  += fa_fs.c fa_rar.c fa_smb.c

# Display

VPATH += display

VPATH += display
SRCS  += gl_common.c display_$(GL_GLUE).c

# video playback subsys

VPATH += video
SRCS  += video_playback.c 
SRCS  += video_decoder.c video_widget.c video_menu.c \
	 gl_dvdspu.c yadif.c subtitles.c


# audio subsys

VPATH += audio
SRCS  += audio.c audio_decoder.c audio_fifo.c audio_ui.c audio_iec958.c \
	 audio_mixer.c

# ALSA Audio support
VPATH += audio/alsa
SRCS  += alsa_audio.c

# layout engine(s)

VPATH += layout
SRCS  += layout.c layout_world.c

# Human Interface Devices

VPATH += hid
SRCS  += hid.c lircd.c imonpad.c lcdd.c keymapper.c

# Main menu

VPATH += apps/mainmenu
SRCS  += mainmenu.c

# Launcher application

VPATH += apps/launcher
SRCS  += launcher.c

# Settings application

VPATH += apps/settings
SRCS  += settings.c settings_ui.c

# Clock application

#VPATH += apps/clock
#SRCS  += clock.c


# Browser application

VPATH += apps/browser
SRCS  += browser.c navigator.c browser_view.c browser_probe.c \
	 browser_slideshow.c useraction.c

# CD application

#VPATH += apps/cd
#SRCS  += cd.c 

# DVD application

VPATH += apps/dvdplayer
SRCS  += dvd.c dvd_mpeg.c

# Playlist application

VPATH += apps/playlist
SRCS  += playlist.c playlist_player.c playlist_scanner.c

# RSS browser applcation

#VPATH += apps/rss
#SRCS  += rss.c rssbrowser.c

# Radio application

#VPATH += apps/radio
#SRCS  += radio.c

# TV & headend com

#VPATH += apps/tv 
#SRCS +=	tv.c htsp.c tv_playback.c

# Apple Movie Trailer Application

#VPATH += apps/apple_movie_trailers
#SRCS  += movt.c



PROG = showtime
CFLAGS += -g -Wall -Werror -funsigned-char -O0 $(HTS_CFLAGS)

CFLAGS += -I/usr/local/include -I$(INCLUDES_INSTALL_BASE) -I$(CURDIR)
CFLAGS += -Wno-deprecated-declarations -Wmissing-prototypes
CFLAGS += -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64

LDFLAGS += -L/usr/local/lib -L$(LIBS_INSTALL_BASE) -L/usr/X11R6/lib 

#
# Locally compiled libs
# 

SLIBS += ${LIBHTS_SLIBS} ${LIBGLW_SLIBS} ${LIBDVDNAV_SLIBS}
DLIBS += ${LIBHTS_DLIBS} ${LIBGLW_DLIBS} ${LIBDVDNAV_DLIBS}

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
