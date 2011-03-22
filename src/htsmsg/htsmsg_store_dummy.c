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

#include <sys/types.h>
#include <stddef.h>

#include "htsmsg_store.h"

/**
 *
 */
void
htsmsg_store_init(const char *programname, const char *path)
{
}

/**
 *
 */
void
htsmsg_store_save(htsmsg_t *record, const char *pathfmt, ...)
{

}

/**
 *
 */
htsmsg_t *
htsmsg_store_load(const char *pathfmt, ...)
{
  return NULL;
}

/**
 *
 */
void
htsmsg_store_remove(const char *pathfmt, ...)
{
}
