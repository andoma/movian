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
#include <stdarg.h>

void htsmsg_store_save(htsmsg_t *record, const char *pathfmt);

htsmsg_t *htsmsg_store_load(const char *pathfmt);

void htsmsg_store_remove(const char *pathfmt);

void htsmsg_store_flush(void);

void htsmsg_store_set(const char *store, const char *key, int value_type, ...);

int htsmsg_store_get_int(const char *store, const char *key, int def);

rstr_t *htsmsg_store_get_str(const char *store, const char *key);

