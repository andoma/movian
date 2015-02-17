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
#include "htsmsg/htsmsg.h"

typedef void (bencode_pase_cb_t)(void *opaque, const char *name,
                                 const void *start, size_t len);


htsmsg_t *bencode_deserialize(const char *src, const char *stop,
                              char *errbuf, size_t errlen,
                              bencode_pase_cb_t *cb, void *opaque,
                              int *consumed_bytes);

buf_t *bencode_serialize(htsmsg_t *src);
