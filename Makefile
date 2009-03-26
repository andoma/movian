BUILDDIR=build.Linux.i686

include ${BUILDDIR}/config.mak

PROG=showtime

CFLAGS  = -Wall -Werror -Wwrite-strings -Wno-deprecated-declarations 
CFLAGS += -Wmissing-prototypes

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

SRCS-$(CONFIG_DVDNAV) += src/video/video_dvdspu.c

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
SRCS-$(CONFIG_DVDNAV)  += src/dvd/dvd.c \

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

${PROG}: $(OBJDIRS) $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS) ${LDFLAGS_cfg}

$(OBJDIRS):
	@mkdir -p $@

${BUILDDIR}/%.o: %.c
	$(CC) -MD $(CFLAGS_com) $(CFLAGS) $(CFLAGS_cfg) -c -o $@ $(CURDIR)/$<


clean:
	rm -rf ${BUILDDIR}/src
	find . -name "*~" | xargs rm -f

# Create showtimeversion.h
$(BUILDDIR)/showtimeversion.h:
	$(CURDIR)/support/version.sh $(CURDIR) $(BUILDDIR)/showtimeversion.h

src/version.c: $(BUILDDIR)/showtimeversion.h

# Include dependency files if they exist.
-include $(DEPS)

# Include architecture specific targets
include support/${ARCHITECTURE}.mk

