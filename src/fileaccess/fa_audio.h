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
#pragma once
#include "media/media.h"
struct backend;
struct fa_handle;


event_t *be_file_playaudio(const char *url, media_pipe_t *mp,
			   char *errbuf, size_t errlen, int hold,
			   const char *mimetype, void *opaque);

#if ENABLE_LIBGME
event_t *fa_gme_playfile(media_pipe_t *mp, struct fa_handle *fh,
			 char *errbuf, size_t errlen, int hold,
			 const char *url);
#endif

#if 1
event_t *fa_xmp_playfile(media_pipe_t *mp, FILE *f,
			 char *errbuf, size_t errlen, int hold,
			 const char *url, size_t size);
#endif
