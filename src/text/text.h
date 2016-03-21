/*
 *  Copyright (C) 2007-2015 Lonelycoder AB
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  This program is also available under a commercial proprietary license.
 *  For more information, contact andreas@lonelycoder.com
 */
#include "config.h"
#include <stdint.h>

#pragma once

struct pixmap;
struct rstr;
struct prop;
struct fa_resolver;

#define FONT_DOMAIN_FALLBACK 0
#define FONT_DOMAIN_DEFAULT  1

#define TR_STYLE_BOLD   0x1
#define TR_STYLE_ITALIC 0x2

#define TR_CODE_START      0x7f000001
#define TR_CODE_NEWLINE    0x7f000002
#define TR_CODE_CENTER_ON  0x7f000003
#define TR_CODE_CENTER_OFF 0x7f000004
#define TR_CODE_BOLD_ON    0x7f000005
#define TR_CODE_BOLD_OFF   0x7f000006
#define TR_CODE_ITALIC_ON  0x7f000007
#define TR_CODE_ITALIC_OFF 0x7f000008
#define TR_CODE_HR         0x7f000009
#define TR_CODE_FONT_RESET 0x7f00000a
#define TR_CODE_SET_MARGIN 0x7f000010

#define TR_CODE_ALPHA         0x7f000100  // Low 8 bit is alpha
#define TR_CODE_SHADOW_ALPHA  0x7f000200  // Low 8 bit is alpha
#define TR_CODE_OUTLINE_ALPHA 0x7f000300  // Low 8 bit is alpha

#define TR_CODE_SIZE_PX    0x7f010000  // Low 16 bit is the size in pixels
#define TR_CODE_SHADOW     0x7f020000  // Low 16 bit is displacement in pixels
#define TR_CODE_OUTLINE    0x7f030000  // Low 16 bit is thickness in pixels
#define TR_CODE_FONT_SIZE  0x7f040000  /* HTML kinda legacy size 
					  1: smallest, 7: biggest
				       */
#define TR_CODE_SHADOW_US  0x7f050000  // Unscaled version
#define TR_CODE_OUTLINE_US 0x7f060000  // Unscaled version


#define TR_CODE_COLOR      0x7e000000  // Low 24 bit is BGR

#define TR_CODE_FONT_FAMILY 0x7d000000  // Low 24 bit is family

#define TR_CODE_SHADOW_COLOR  0x7c000000  // Low 24 bit is BGR
#define TR_CODE_OUTLINE_COLOR 0x7b000000  // Low 24 bit is BGR

#define TR_RENDER_DEBUG         0x1
#define TR_RENDER_ELLIPSIZE     0x2
#define TR_RENDER_CHARACTER_POS 0x4
#define TR_RENDER_BOLD          0x8
#define TR_RENDER_ITALIC        0x10
#define TR_RENDER_SHADOW        0x20
#define TR_RENDER_OUTLINE       0x40
#define TR_RENDER_NO_OUTPUT     0x80
#define TR_RENDER_SUBS          0x100  // Render for subtitles

#define TR_ALIGN_AUTO      0
#define TR_ALIGN_LEFT      1
#define TR_ALIGN_CENTER    2
#define TR_ALIGN_RIGHT     3
#define TR_ALIGN_JUSTIFIED 4

struct image *
text_render(const uint32_t *uc, int len, int flags, int default_size,
	    float scale, int alignment,
	    int max_width, int max_lines, const char *font_family,
	    int font_domain, int min_size, struct fa_resolver *far);


#if ENABLE_LIBFREETYPE

struct fa_handle;
struct buf;

void *freetype_load_dynamic_font_buf(struct buf *b, int font_domain,
                                     char *errbuf, size_t errlen);

void *freetype_load_dynamic_font_fh(struct fa_handle *fh, const char *url,
				    int font_domain,
				    char *errbuf, size_t errlen);

void *freetype_load_dynamic_font(const char *url, int font_domain,
				 char *errbuf, size_t errlen);

void freetype_load_default_font(const char *url, int prio);

void freetype_unload_font(void *ref);

int freetype_family_id(const char *str, int context);

int freetype_get_context(void);   // rename context -> font_domain

struct rstr *freetype_get_family(void *handle);

struct rstr *freetype_get_identifier(void *handle);

#endif

void fontstash_props_from_title(struct prop *p, const char *url,
				const char *title);

#if ENABLE_LIBFONTCONFIG
int fontconfig_resolve(int uc, uint8_t style, const char *family,
		       char *urlbuf, size_t urllen);
#endif

#define TEXT_PARSE_HTML_TAGS     0x1
#define TEXT_PARSE_HTML_ENTITIES 0x2
#define TEXT_PARSE_SLOPPY_TAGS   0x4 // Unknown tags are parsed as normal text
#define TEXT_PARSE_SUB_TAGS      0x8
#define TEXT_PARSE_SLASH_PREFIX  0x10

uint32_t *text_parse(const char *str, int *lenp, int flags,
		     const uint32_t *prefix, int prefixlen,
		     int context);

