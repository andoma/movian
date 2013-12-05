#pragma once
/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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

typedef struct json_deserializer {
  void *(*jd_create_map)(void *jd_opaque);
  void *(*jd_create_list)(void *jd_opaque);

  void (*jd_destroy_obj)(void *jd_opaque, void *obj);

  void (*jd_add_obj)(void *jd_opaque, void *parent,
		     const char *name, void *child);

  // str must be free'd by callee
  void (*jd_add_string)(void *jd_opaque, void *parent,
			const char *name, char *str);

  void (*jd_add_long)(void *jd_opaque, void *parent,
		      const char *name, long v);

  void (*jd_add_double)(void *jd_opaque, void *parent,
			const char *name, double d);

  void (*jd_add_bool)(void *jd_opaque, void *parent,
		      const char *name, int v);

  void (*jd_add_null)(void *jd_opaque, void *parent,
		      const char *name);

} json_deserializer_t;

void *json_deserialize(const char *src, const json_deserializer_t *jd,
		       void *opaque, char *errbuf, size_t errlen);
