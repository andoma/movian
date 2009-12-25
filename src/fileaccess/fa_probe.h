/*
 *  Functions for probing file contents
 *  Copyright (C) 2008 Andreas Öman
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

#ifndef FA_PROBE_H
#define FA_PROBE_H

#include "prop.h"
#include "fileaccess.h"
#include <libavformat/avformat.h>

struct metadata;

unsigned int fa_probe(prop_t *proproot, const char *url,
		      char *newurl, size_t newurlsize,
		      char *errbuf, size_t errsize,
		      struct stat *st);

unsigned int fa_probe_dir(prop_t *proproot, const char *url);

int fa_probe_iso(struct metadata *md, fa_handle_t *fh);

void fa_probe_load_metaprop(prop_t *p, AVFormatContext *fctx, const char *url);

void fa_probe_init(void);

#endif /* FA_PROBE_H */
