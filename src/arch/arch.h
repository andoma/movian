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
#include <stdint.h>
#include <time.h>
#include "compiler.h"

void arch_exit(void) attribute_noreturn;

int arch_pipe(int pipefd[2]);

void arch_sync_path(const char *path);

int arch_stop_req(void);

// Mainloop will stop
#define ARCH_STOP_IS_PROGRESSING      0

// Mainloop will suspend but app will continue (faked stop)
// This can be used to fake shutdown and turn off TV, but still let
// the app run in a "standby mode"
#define ARCH_STOP_IS_NOT_HANDLED      1

// Caller (shutdown thread) should do full exit procedure
#define ARCH_STOP_CALLER_MUST_HANDLE  2

void arch_localtime(const time_t *timep, struct tm *tm);

void stackdump(const char *logprefix);

// Return size of allocation at 'ptr'
size_t arch_malloc_size(void *ptr);

void arch_get_random_bytes(void *ptr, size_t size);
