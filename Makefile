-include ../config.mak

# core

SRCS = 	main.c app.c input.c media.c mpeg_support.c play_file.c miw.c \
	mediaprobe.c coms.c settings.c subtitles.c

# audio subsys

VPATH += audio
SRCS  += audio.c audio_decoder.c audio_fifo.c audio_mixer.c audio_ui.c \
	 audio_compressor.c audio_iec958.c

# ALSA Audio support
VPATH += audio/alsa
SRCS  += alsa_audio.c

# layout engine(s)

VPATH += layout
SRCS  += layout.c layout_forms.c layout_world.c layout_switcher.c

# OpenGL support

VPATH += gl
SRCS  += video_decoder.c video_widget.c video_menues.c
SRCS  += gl_dvdspu.c yadif.c

VPATH += gl/sysglue
SRCS  += gl_common.c sysglue_$(GL_GLUE).c

# Human Interface Devices

VPATH += hid
SRCS  += hid.c lircd.c imonpad.c lcdd.c 

# Launcher application

VPATH += apps/launcher
SRCS  += launcher.c

# Clock application

VPATH += apps/clock
SRCS  += clock.c


# Browser application

VPATH += apps/browser
SRCS  += browser.c navigator.c browser_file.c browser_view.c

# CD application

#VPATH += apps/cd
#SRCS  += cd.c 

# DVD application

#VPATH += apps/dvdplayer
#SRCS  += dvd.c 

# Playlist application

#VPATH += apps/playlist
#SRCS  += playlist.c

# RSS browser applcation

#VPATH += apps/rss
#SRCS  += rss.c rssbrowser.c

# Radio application

#VPATH += apps/radio
#SRCS  += radio.c

# TV & headend com

#VPATH += apps/tv
#SRCS +=	tv_headend.c tv_playback.c pvr.c

# Apple Movie Trailer Application

#VPATH += apps/apple_movie_trailers
#SRCS  += movt.c



PROG = showtime
CFLAGS += -g -Wall -Werror -funsigned-char -O2 $(HTS_CFLAGS)

CFLAGS += -I/usr/local/include -I$(INCLUDES_INSTALL_BASE) -I$(CURDIR)
CFLAGS += -Wno-deprecated-declarations -Wmissing-prototypes

LDFLAGS += -L/usr/local/lib -L$(LIBS_INSTALL_BASE) -L/usr/X11R6/lib 

#
# Locally compiled libs
# 

SLIBS += ${LIBHTS_SLIBS} ${LIBGLW_SLIBS} ${LIBDVDNAV_SLIBS}
DLIBS += ${LIBHTS_DLIBS} ${LIBGLW_DLIBS} ${LIBDVDNAV_DLIBS}

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
CFLAGS += -DGL_GLEXT_PROTOTYPES
DLIBS  += $(LIBGL_DLIBS)

# Misc

DLIBS += -lm -lz -ldl -lpthread


.OBJDIR=        obj
DEPFLAG = -M

OBJS = $(patsubst %.c,%.o, $(SRCS))
DEPS= ${OBJS:%.o=%.d}

prefix ?= $(INSTALLPREFIX)
INSTDIR= $(prefix)/bin

all:	$(PROG)

install:
	mkdir -p $(INSTDIR)
	cd $(.OBJDIR) && install -s ${PROG} $(INSTDIR)

${PROG}: $(.OBJDIR) $(OBJS) Makefile
	cd $(.OBJDIR) && $(CC) $(LDFLAGS) -o $@ $(OBJS) \
	$(STATIC_LINKFLAGS) $(SLIBS) $(DYNAMIC_LINKFLAGS) $(DLIBS) 

$(.OBJDIR):
	mkdir $(.OBJDIR)

.c.o:	Makefile
	cd $(.OBJDIR) && $(CC) -MD $(CFLAGS) -c -o $@ $(CURDIR)/$<


clean:
	rm -rf *~ core $(.OBJDIR)

vpath %.o ${.OBJDIR}
vpath %.S ${.OBJDIR}
vpath ${PROG} ${.OBJDIR}
vpath ${PROGBIN} ${.OBJDIR}

# include dependency files if they exist
$(addprefix ${.OBJDIR}/, ${DEPS}): ;
-include $(addprefix ${.OBJDIR}/, ${DEPS})
