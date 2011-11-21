/*
 *  Functions for storing program settings
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

#ifndef HTSMSG_STORE_H__
#define HTSMSG_STORE_H__

#include "htsmsg/htsmsg.h"
#include <stdarg.h>

void htsmsg_store_init(void);

void htsmsg_store_save(htsmsg_t *record, const char *pathfmt, ...);

htsmsg_t *htsmsg_store_load(const char *pathfmt, ...);

void htsmsg_store_remove(const char *pathfmt, ...);

void htsmsg_store_flush(void);

#endif /* HTSMSG_STORE_H__ */ 
