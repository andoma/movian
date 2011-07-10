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

#include "prop/prop.h"
#include "fileaccess.h"
#include "metadata.h"
#include <libavformat/avformat.h>

metadata_t *fa_probe_dir(const char *url);

int fa_probe_iso(struct metadata *md, AVIOContext *avio);

metadata_t *fa_metadata_from_fctx(AVFormatContext *fctx, const char *url);

metadata_t *fa_probe_metadata(const char *url, char *errbuf, size_t errsize);

#endif /* FA_PROBE_H */
