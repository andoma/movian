#
#  Showtime mediacenter
#  Copyright (C) 2007-2009 Andreas Öman
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
#

include ${CURDIR}/config.default

BUILDDIR = build.${PLATFORM}

include ${BUILDDIR}/config.mak

CFLAGS  = -Wall -Werror -Wwrite-strings -Wno-deprecated-declarations 
CFLAGS += -Wmissing-prototypes -Isrc/dvd


#
# Core
#
SRCS += src/main.c \
	src/version.c \
	src/navigator.c \
	src/media.c \
	src/event.c \
	src/keyring.c \
	src/settings.c \
	src/prop.c \
	src/bookmarks.c \
	src/notifications.c \
	src/playqueue.c \
	src/arch/arch_${OSENV}.c \
	src/ui/ui.c \


#
# Misc support
#
SRCS +=	src/misc/ptrvec.c \
	src/misc/callout.c \
	src/misc/rstr.c \
	src/misc/pixmap.c \
	src/misc/jpeg.c \

#
# HTSMSG
#
SRCS +=	src/htsmsg/htsbuf.c \
	src/htsmsg/htsmsg.c \
	src/htsmsg/htsmsg_json.c \
	src/htsmsg/htsmsg_xml.c \
	src/htsmsg/htsmsg_binary.c \
	src/htsmsg/htsmsg_store_${OSENV}.c \

#
# Virtual FS system
#
SRCS += src/fileaccess/fileaccess.c \
	src/fileaccess/fa_probe.c \
	src/fileaccess/fa_imageloader.c \
	src/fileaccess/fa_rawloader.c \
	src/fileaccess/fa_backend.c \
	src/fileaccess/fa_scanner.c \
	src/fileaccess/fa_video.c \
	src/fileaccess/fa_audio.c \
	src/fileaccess/fa_fs.c \
	src/fileaccess/fa_rar.c \
	src/fileaccess/fa_smb.c \
	src/fileaccess/fa_http.c \
	src/fileaccess/fa_zip.c \
	src/fileaccess/fa_zlib.c \
	src/fileaccess/fa_bundle.c \

SRCS-$(CONFIG_TINYSMB)  += src/fileaccess/fa_tinysmb.c

#
# Service Discovery
#

SRCS 			+= src/sd/sd.c \

SRCS-$(CONFIG_AVAHI) 	+= src/sd/avahi.c \

${BUILDDIR}/src/sd/avahi.o : CFLAGS = \
 $(shell pkg-config --cflags avahi-client) -Wall -Werror

BUNDLES += resources/tvheadend

SRCS-$(CONFIG_BONJOUR) 	+= src/sd/bonjour.c

#
# Scrapping
#

SRCS                   += src/scrappers/scrappers.c

#
# Networking
#
SRCS += src/networking/net_common.c \

SRCS-$(CONFIG_POSIX_NETWORKING) += src/networking/net_posix.c
SRCS-$(CONFIG_LIBOGC) += src/networking/net_libogc.c

#
# Video support
#
SRCS += src/video/video_playback.c \
	src/video/video_decoder.c \
	src/video/yadif.c \

SRCS-$(CONFIG_DVD) += src/video/video_dvdspu.c

# Temporary fix for http://gcc.gnu.org/bugzilla/show_bug.cgi?id=11203
# -OO will result in compiler error
ifeq ($(PLATFORM), osx)
${BUILDDIR}/src/video/yadif.o : CFLAGS = -O2
endif


#
# Audio subsys
#
SRCS += src/audio/audio.c \
	src/audio/audio_decoder.c \
	src/audio/audio_fifo.c \
	src/audio/audio_iec958.c \

SRCS-$(CONFIG_LIBASOUND)  += src/audio/alsa/alsa_audio.c
SRCS-$(CONFIG_LIBPULSE)   += src/audio/pulseaudio/pulseaudio.c
SRCS-$(CONFIG_LIBOGC)     += src/audio/wii/wii_audio.c
SRCS-$(CONFIG_COREAUDIO)  += src/audio/coreaudio/coreaudio.c
SRCS                      += src/audio/dummy/dummy_audio.c

#
# DVD
#
SRCS-$(CONFIG_DVD)  += src/dvd/dvd.c \

#
# dvdcss
#
SRCS-$(CONFIG_DVD) += 	src/dvd/dvdcss/css.c \
			src/dvd/dvdcss/device.c \
			src/dvd/dvdcss/error.c \
			src/dvd/dvdcss/ioctl.c \
			src/dvd/dvdcss/libdvdcss.c

ifeq ($(PLATFORM), osx)
DVDCSS_CFLAGS = -DDARWIN_DVD_IOCTL
elif ($(PLATFORM), linux)
DVDCSS_CFLAGS = -DHAVE_LINUX_DVD_STRUCT -DDVD_STRUCT_IN_LINUX_CDROM_H
endif


${BUILDDIR}/src/dvd/dvdcss/%.o : CFLAGS = \
 -DHAVE_LIMITS_H -DHAVE_UNISTD_H -DHAVE_ERRNO_H -DVERSION="0" $(DVDCSS_CFLAGS)

#
# libdvdread
#
SRCS-$(CONFIG_DVD) += 	src/dvd/libdvdread/dvd_input.c \
			src/dvd/libdvdread/dvd_reader.c \
			src/dvd/libdvdread/dvd_udf.c \
			src/dvd/libdvdread/ifo_read.c \
			src/dvd/libdvdread/md5.c \
			src/dvd/libdvdread/nav_read.c \
			src/dvd/libdvdread/bitreader.c

${BUILDDIR}/src/dvd/libdvdread/%.o : CFLAGS = \
 -DHAVE_DVDCSS_DVDCSS_H -DDVDNAV_COMPILE -Wno-strict-aliasing  -Isrc/dvd 

#
# libdvdread
#
SRCS-$(CONFIG_DVD) += 	src/dvd/dvdnav/dvdnav.c \
			src/dvd/dvdnav/highlight.c \
			src/dvd/dvdnav/navigation.c \
			src/dvd/dvdnav/read_cache.c \
			src/dvd/dvdnav/remap.c \
			src/dvd/dvdnav/settings.c \
			src/dvd/dvdnav/vm/vm.c \
			src/dvd/dvdnav/vm/decoder.c \
			src/dvd/dvdnav/vm/vmcmd.c \
			src/dvd/dvdnav/searching.c

${BUILDDIR}/src/dvd/dvdnav/%.o : CFLAGS = \
 -DVERSION=\"showtime\" -DDVDNAV_COMPILE -Wno-strict-aliasing -Isrc/dvd \
 -Isrc/dvd/dvdnav

#
# TV
#
SRCS  += src/tv/htsp.c \

#
# Spotify
#
SRCS-${CONFIG_SPOTIFY} += src/spotify/spotify.c
BUNDLES-$(CONFIG_SPOTIFY) += resources/spotify
#
# GLW user interface
#
SRCS-$(CONFIG_GLW)   += src/ui/glw/glw.c \
			src/ui/glw/glw_event.c \
			src/ui/glw/glw_model.c \
		     	src/ui/glw/glw_model_lexer.c \
		     	src/ui/glw/glw_model_parser.c \
			src/ui/glw/glw_model_eval.c \
			src/ui/glw/glw_model_preproc.c \
			src/ui/glw/glw_model_support.c \
			src/ui/glw/glw_model_attrib.c \
			src/ui/glw/glw_container.c \
			src/ui/glw/glw_list.c \
			src/ui/glw/glw_array.c \
			src/ui/glw/glw_deck.c \
			src/ui/glw/glw_layer.c \
			src/ui/glw/glw_expander.c \
			src/ui/glw/glw_slider.c \
			src/ui/glw/glw_rotator.c  \
			src/ui/glw/glw_animator.c \
			src/ui/glw/glw_slideshow.c \
			src/ui/glw/glw_freefloat.c \
			src/ui/glw/glw_transitions.c \
			src/ui/glw/glw_navigation.c \
			src/ui/glw/glw_texture_loader.c \
			src/ui/glw/glw_scaler.c  \
			src/ui/glw/glw_image.c \
			src/ui/glw/glw_text_bitmap.c \
			src/ui/glw/glw_cursor.c \
			src/ui/glw/glw_fx_texrot.c \
			src/ui/glw/glw_bloom.c \
			src/ui/glw/glw_cube.c \

SRCS-$(CONFIG_GLW_FRONTEND_X11)	  += src/ui/glw/glw_x11.c \
				     src/ui/linux/screensaver_inhibitor.c
SRCS-$(CONFIG_GLW_FRONTEND_COCOA) += src/ui/glw/glw_cocoa.m
SRCS-$(CONFIG_GLW_BACKEND_OPENGL) += src/ui/glw/glw_opengl.c
SRCS-$(CONFIG_GLW_BACKEND_OPENGL) += src/ui/glw/glw_texture_opengl.c
SRCS-$(CONFIG_GLW_BACKEND_OPENGL) += src/ui/glw/glw_render_opengl.c
SRCS-$(CONFIG_GLW_BACKEND_OPENGL) += src/ui/glw/glw_mirror.c
SRCS-$(CONFIG_GLW_BACKEND_OPENGL) += src/ui/glw/glw_video_opengl.c
SRCS-$(CONFIG_GLW_FRONTEND_WII)	  += src/ui/glw/glw_wii.c
SRCS-$(CONFIG_GLW_BACKEND_GX)     += src/ui/glw/glw_texture_gx.c
SRCS-$(CONFIG_GLW_BACKEND_GX)     += src/ui/glw/glw_render_gx.c
SRCS-$(CONFIG_GLW_BACKEND_GX)     += src/ui/glw/glw_gx.c
SRCS-$(CONFIG_GLW_BACKEND_GX)     += src/ui/glw/glw_video_gx.c
SRCS-$(CONFIG_GLW_BACKEND_GX)     += src/ui/glw/glw_gxasm.S

SRCS-$(CONFIG_NVCTRL)             += src/ui/linux/nvidia.c

#
# GTK based interface
#
SRCS-$(CONFIG_GU) +=    src/ui/gu/gu.c \
			src/ui/gu/gu_helpers.c \
			src/ui/gu/gu_pixbuf.c \
			src/ui/gu/gu_popup.c \
			src/ui/gu/gu_menubar.c \
			src/ui/gu/gu_toolbar.c \
			src/ui/gu/gu_statusbar.c \
			src/ui/gu/gu_playdeck.c \
			src/ui/gu/gu_pages.c \
			src/ui/gu/gu_home.c \
			src/ui/gu/gu_directory.c \
			src/ui/gu/gu_directory_list.c \
			src/ui/gu/gu_directory_album.c \
			src/ui/gu/gu_directory_albumcollection.c \

${BUILDDIR}/src/ui/gu/%.o : CFLAGS = $(shell pkg-config --cflags gtk+-2.0) \
-Wall -Werror -Wmissing-prototypes -Wno-cast-qual -Wno-deprecated-declarations 

#
# LIRC UI
#
SRCS-$(CONFIG_LIRC) +=  src/ui/lirc/imonpad.c \
			src/ui/lirc/lircd.c

#
# IPC
#
SRCS                +=  src/ipc/ipc.c

SRCS-$(CONFIG_DBUS) +=  src/ipc/dbus/dbus.c \
			src/ipc/dbus/mpris.c \
			src/ipc/dbus/mpkeys.c

${BUILDDIR}/src/ipc/dbus/%.o : CFLAGS =  $(shell pkg-config --cflags dbus-1) \
-Wall -Werror -Wmissing-prototypes -Wno-cast-qual


#
# Apple remote and keyspan front row remote
#
SRCS-$(CONFIG_APPLEREMOTE) += \
			src/ui/appleremote/AppleRemote.m \
			src/ui/appleremote/GlobalKeyboardDevice.m \
			src/ui/appleremote/HIDRemoteControlDevice.m \
			src/ui/appleremote/KeyspanFrontRowControl.m \
			src/ui/appleremote/MultiClickRemoteBehavior.m \
			src/ui/appleremote/RemoteControl.m \
			src/ui/appleremote/RemoteControlContainer.m \
			src/ui/appleremote/ShowtimeMainController.m


# Various transformations
SRCS  += $(SRCS-yes)
DLIBS += $(DLIBS-yes)
SLIBS += $(SLIBS-yes)
OBJS3=   $(SRCS:%.S=$(BUILDDIR)/%.o)
OBJS2=   $(OBJS3:%.c=$(BUILDDIR)/%.o)
OBJS=    $(OBJS2:%.m=$(BUILDDIR)/%.o)
DEPS=    ${OBJS:%.o=%.d}
OBJDIRS= $(sort $(dir $(OBJS)))

# File bundles
BUNDLES += $(BUNDLES-yes)
BUNDLE_SRCS=$(BUNDLES:%=$(BUILDDIR)/bundles/%.c)
BUNDLE_DEPS=$(BUNDLE_SRCS:%.c=%.d)
BUNDLE_OBJS=$(BUNDLE_SRCS:%.c=%.o)
OBJDIRS+= $(sort $(dir $(BUNDLE_OBJS)))
.PRECIOUS: ${BUNDLE_SRCS}

# Common CFLAGS for all files
CFLAGS_com  = -g -funsigned-char -O2
CFLAGS_com += -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64
CFLAGS_com += -I${BUILDDIR} -I${CURDIR}/src -I${CURDIR}

all:	${PROG}

.PHONY:	clean distclean ffmpeg

${PROG}: ${BUILDDIR}/ffmpeg/install $(OBJDIRS) $(OBJS) $(BUNDLE_OBJS) Makefile
	$(CC) -o $@ $(OBJS) $(BUNDLE_OBJS) $(LDFLAGS) ${LDFLAGS_cfg}

$(OBJDIRS):
	@mkdir -p $@

${BUILDDIR}/%.o: %.[cm] ${BUILDDIR}/ffmpeg/install
	$(CC) -MD -MP $(CFLAGS_com) $(CFLAGS) $(CFLAGS_cfg) -c -o $@ $(CURDIR)/$<

${BUILDDIR}/ffmpeg/install ffmpeg:
	cd ${BUILDDIR}/ffmpeg/build && ${MAKE} all
	cd ${BUILDDIR}/ffmpeg/build && ${MAKE} install

clean:
	rm -rf ${BUILDDIR}/src ${BUILDDIR}/bundles
	find . -name "*~" | xargs rm -f

distclean: clean
	rm -rf build.*

reconfigure:
	$(CURDIR)/configure $(CONFIGURE_ARGS)

# Create showtimeversion.h
$(BUILDDIR)/showtimeversion.h:
	$(CURDIR)/support/version.sh $(CURDIR) $(BUILDDIR)/showtimeversion.h

src/version.c: $(BUILDDIR)/showtimeversion.h

# Include dependency files if they exist.
-include $(DEPS) $(BUNDLE_DEPS)

# Include Platform specific targets
include support/${PLATFORM}.mk

# Bundle files
$(BUILDDIR)/bundles/%.o: $(BUILDDIR)/bundles/%.c
	$(CC) -I${CURDIR}/src/fileaccess -c -o $@ $<

$(BUILDDIR)/bundles/%.c: % $(CURDIR)/support/mkbundle
	$(CURDIR)/support/mkbundle \
		-o $@ -s $< -d ${BUILDDIR}/bundles/$<.d -p $<
