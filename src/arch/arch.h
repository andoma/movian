/*
 *  System specific stuff
 *
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

#pragma once

#include <stdint.h>
#include <time.h>

void arch_init(void);

void arch_sd_init(void); // Arch specific service discovery

void arch_exit(int code) __attribute__((noreturn));

void arch_set_default_paths(int argc, char **argv);

int64_t arch_cache_avail_bytes(void);

void trap_init(void);

void arch_preload_fonts(void);

void my_localtime(const time_t *timep, struct tm *tm);
