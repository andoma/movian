/*
 *  Text rendering
 *  Copyright (C) 2007 - 2011 Andreas Öman
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
 */

#include "config.h"
#include <stdint.h>

#pragma once

struct pixmap;

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

#define TR_RENDER_DEBUG         0x1
#define TR_RENDER_ELLIPSIZE     0x2
#define TR_RENDER_CHARACTER_POS 0x4

struct pixmap *
text_render(const uint32_t *uc, int len, int flags, int size,
	    int max_width, int max_lines, const char *font_family);


#if ENABLE_LIBFREETYPE
int freetype_init(void);

void freetype_load_font(const char *url);
#endif

#if ENABLE_LIBFONTCONFIG
int fontconfig_resolve(int uc, uint8_t style, const char *family,
		       char *urlbuf, size_t urllen, uint8_t *actualstylep);
#endif

#define TEXT_PARSE_TAGS          0x1
#define TEXT_PARSE_HTML_ENTETIES 0x2


int *text_parse(const char *str, int *lenp, int flags);

