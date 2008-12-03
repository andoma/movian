# Makefile for libglw

-include ../../config.mak

SRCS = glw.c

SRCS += glw_opengl.c

SRCS += glw_model.c glw_model_lexer.c glw_model_parser.c \
	glw_model_eval.c glw_model_preproc.c glw_model_support.c \
	glw_model_attrib.c

SRCS += glw_container.c glw_text.c glw_text_bitmap.c \
	glw_bitmap.c glw_tex_loader.c \
	glw_helpers.c glw_external.c \
	glw_form.c glw_rotator.c \
	glw_list.c glw_cubestack.c glw_deck.c glw_zstack.c \
	glw_expander.c glw_scaler.c glw_event.c \
	glw_mirror.c glw_animator.c glw_transitions.c \
	glw_fx_texrot.c



L = libglw

LIBS_INSTALL = $(LIBS_INSTALL_BASE)
INCLUDES_INSTALL = $(INCLUDES_INSTALL_BASE)/libglw

HEADERS = glw.h

CFLAGS =  -I$(INCLUDES_INSTALL_BASE) $(HTS_CFLAGS)
CFLAGS += -Wmissing-prototypes

# for opengl

CFLAGS += -DGL_GLEXT_PROTOTYPES  

# for ffmpeg

CFLAGS += -Wno-deprecated-declarations 

# freetype 2

CFLAGS += ${LIBFREETYPE2_CFLAGS}

# exif

CFLAGS += ${LIBEXIF_CFLAGS}

include ../../build/lib.mk
