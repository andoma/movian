PLATFORM ?= Linux.i686
BUILDDIR = build.${PLATFORM}

include ${BUILDDIR}/config.mak

PROG=showtime

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
	src/arch/arch_${ARCHITECTURE}.c \
	src/ui/ui.c \
	src/ui/keymapper.c \


#
# HTSMSG
#
SRCS +=	src/htsmsg/htsbuf.c \
	src/htsmsg/htsmsg.c \
	src/htsmsg/htsmsg_json.c \
	src/htsmsg/htsmsg_xml.c \
	src/htsmsg/htsmsg_binary.c \
	src/htsmsg/htsmsg_store_${ARCHITECTURE}.c \

#
# Virtual FS system
#
SRCS += src/fileaccess/fileaccess.c \
	src/fileaccess/fa_probe.c \
	src/fileaccess/fa_imageloader.c \
	src/fileaccess/fa_rawloader.c \
	src/fileaccess/fa_backend.c \
	src/fileaccess/fa_video.c \
	src/fileaccess/fa_audio.c \
	src/fileaccess/fa_fs.c \
	src/fileaccess/fa_rar.c \
	src/fileaccess/fa_smb.c \
	src/fileaccess/fa_http.c \
	src/fileaccess/fa_zip.c \
	src/fileaccess/fa_zlib.c \
	src/fileaccess/fa_embedded.c \

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

#
# Audio subsys
#
SRCS += src/audio/audio.c \
	src/audio/audio_decoder.c \
	src/audio/audio_fifo.c \
	src/audio/audio_iec958.c \
	src/audio/audio_mixer.c \

SRCS-$(CONFIG_LIBASOUND)  += src/audio/alsa/alsa_audio.c
SRCS-$(CONFIG_LIBOGC)     += src/audio/wii/wii_audio.c
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

${BUILDDIR}/src/dvd/dvdcss/%.o : CFLAGS = \
 -DHAVE_LIMITS_H -DHAVE_UNISTD_H -DHAVE_ERRNO_H -DHAVE_LINUX_DVD_STRUCT \
 -DDVD_STRUCT_IN_LINUX_CDROM_H -DVERSION="0"

#
# libdvdread
#
SRCS-$(CONFIG_DVD) += 	src/dvd/libdvdread/dvd_input.c \
			src/dvd/libdvdread/dvd_reader.c \
			src/dvd/libdvdread/dvd_udf.c \
			src/dvd/libdvdread/ifo_print.c \
			src/dvd/libdvdread/ifo_read.c \
			src/dvd/libdvdread/md5.c \
			src/dvd/libdvdread/nav_print.c \
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
			src/ui/glw/glw_stack.c \
			src/ui/glw/glw_list.c \
			src/ui/glw/glw_deck.c \
			src/ui/glw/glw_layer.c \
			src/ui/glw/glw_expander.c \
			src/ui/glw/glw_slider.c \
			src/ui/glw/glw_rotator.c  \
			src/ui/glw/glw_animator.c \
			src/ui/glw/glw_transitions.c \
			src/ui/glw/glw_navigation.c \
			src/ui/glw/glw_texture_loader.c \
			src/ui/glw/glw_scaler.c  \
			src/ui/glw/glw_image.c \
			src/ui/glw/glw_text_bitmap.c \
			src/ui/glw/glw_cursor.c \

SRCS-$(CONFIG_GLW_FRONTEND_X11)	  += src/ui/glw/glw_x11.c
SRCS-$(CONFIG_GLW_BACKEND_OPENGL) += src/ui/glw/glw_opengl.c
SRCS-$(CONFIG_GLW_BACKEND_OPENGL) += src/ui/glw/glw_texture_opengl.c
SRCS-$(CONFIG_GLW_BACKEND_OPENGL) += src/ui/glw/glw_render_opengl.c
SRCS-$(CONFIG_GLW_BACKEND_OPENGL) += src/ui/glw/glw_mirror.c
SRCS-$(CONFIG_GLW_BACKEND_OPENGL) += src/ui/glw/glw_video.c
SRCS-$(CONFIG_GLW_BACKEND_OPENGL) += src/ui/glw/glw_fx_texrot.c
SRCS-$(CONFIG_GLW_FRONTEND_WII)	  += src/ui/glw/glw_wii.c
SRCS-$(CONFIG_GLW_BACKEND_GX)     += src/ui/glw/glw_texture_gx.c
SRCS-$(CONFIG_GLW_BACKEND_GX)     += src/ui/glw/glw_render_gx.c
SRCS-$(CONFIG_GLW_BACKEND_GX)     += src/ui/glw/glw_gx.c


# Various transformations
SRCS  += $(SRCS-yes)
DLIBS += $(DLIBS-yes)
SLIBS += $(SLIBS-yes)
OBJS=    $(SRCS:%.c=$(BUILDDIR)/%.o)
DEPS=    ${OBJS:%.o=%.d}
OBJDIRS= $(sort $(dir $(OBJS)))

# Common CFLAGS for all files
CFLAGS_com  = -g -funsigned-char -O2 
CFLAGS_com += -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64
CFLAGS_com += -I${BUILDDIR} -I${CURDIR}/src -I${CURDIR}

all:	${PROG}

.PHONY:	clean distclean ffmpeg

${PROG}: ${BUILDDIR}/ffmpeg/install $(OBJDIRS) $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS) ${LDFLAGS_cfg}

$(OBJDIRS):
	@mkdir -p $@

${BUILDDIR}/%.o: %.c
	$(CC) -MD $(CFLAGS_com) $(CFLAGS) $(CFLAGS_cfg) -c -o $@ $(CURDIR)/$<

${BUILDDIR}/ffmpeg/install ffmpeg:
	cd ${BUILDDIR}/ffmpeg/build && ${MAKE} all
	cd ${BUILDDIR}/ffmpeg/build && ${MAKE} install

clean:
	rm -rf ${BUILDDIR}/src
	find . -name "*~" | xargs rm -f

distclean: clean
	rm -rf build.*

# Create showtimeversion.h
$(BUILDDIR)/showtimeversion.h:
	$(CURDIR)/support/version.sh $(CURDIR) $(BUILDDIR)/showtimeversion.h

src/version.c: $(BUILDDIR)/showtimeversion.h

# Include dependency files if they exist.
-include $(DEPS)

# Include architecture specific targets
include support/${ARCHITECTURE}.mk

