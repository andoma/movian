include ../config.mak

# core

SRCS = 	main.c app.c input.c media.c mpeg_support.c play_file.c miw.c \
	mediaprobe.c coms.c menu.c settings.c

# audio subsys

VPATH += audio
SRCS  += audio_ui.c audio_sched.c

# layout engine(s)

VPATH += layout
SRCS  += layout_cube.c

# OpenGL support

VPATH += gl
SRCS  += gl_video.c gl_dvdspu.c gl_input.c

# Human Interface Devices

VPATH += hid
SRCS  += hid.c lircd.c imonpad.c lcdd.c 

# ALSA Audio support

VPATH += audio/alsa
SRCS  += alsa_mixer.c alsa_audio.c

# Browser application

VPATH += apps/browser
SRCS  += browser.c

# CD application

VPATH += apps/cd
SRCS  += cd.c 

# DVD application

VPATH += apps/dvdplayer
SRCS  += dvd.c 

# Playlist application

VPATH += apps/playlist
SRCS  += playlist.c

# RSS browser applcation

VPATH += apps/rss
SRCS  += rss.c rssbrowser.c

# Radio application

VPATH += apps/radio
SRCS  += radio.c

# TV & headend com

VPATH += apps/tv
SRCS +=	tv_headend.c tv_playback.c pvr.c

# Apple Movie Trailer Application

VPATH += apps/apple_movie_trailers
SRCS  += movt.c



PROG = showtime
CFLAGS += -g -Wall -Werror -funsigned-char -O2

CFLAGS += -I/usr/local/include -I$(CURDIR)/../install/include -I$(CURDIR)
CFLAGS += -Wno-deprecated-declarations

LDFLAGS += -L/usr/local/lib
LDFLAGS += -L$(CURDIR)/../install/lib
LDFLAGS += -L/usr/X11R6/lib 

#
# Locally compiled libs
# 

DLIBS += -lglw -lhts -ldvdnav 

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
DLIBS += -lGL -lglut


# Misc

DLIBS += -lm -lz -ldl -lpthread


.OBJDIR=        obj
DEPFLAG = -M

OBJS = $(patsubst %.c,%.o, $(SRCS))
DEPS= ${OBJS:%.o=%.d}
INSTDIR= $(CURDIR)/../install/bin
all:	$(PROG)

install:
	mkdir -p $(INSTDIR)
	cd $(.OBJDIR) && install ${PROG} $(INSTDIR)

${PROG}: $(.OBJDIR) $(OBJS) Makefile
	cd $(.OBJDIR) && $(CC) $(LDFLAGS) -o $@ $(OBJS) \
	-Wl,-Bstatic $(SLIBS) -Wl,-Bdynamic $(DLIBS)

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
