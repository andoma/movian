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

#include "config.h"

#if defined(linux) || defined(__APPLE__)
#include "posix/posix_threads.h"
#elif PS3
#include "ps3/ps3_threads.h"
#else
#error No threading support
#endif


#if ENABLE_EMU_THREAD_SPECIFICS
typedef int hts_key_t;
extern int hts_thread_key_create(unsigned int *k, void (*destrutor)(void *));
extern int hts_thread_key_delete(unsigned int k);
extern int hts_thread_set_specific(unsigned int k, void *p);
extern void *hts_thread_get_specific(unsigned int k);
extern void hts_thread_exit_specific(void);
#endif
