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
#include "config.h"

#define USAGE_SEG(n, ...) (const char *[]){n, ##__VA_ARGS__, NULL}

#if ENABLE_USAGEREPORT

void usage_start(void);

void usage_event(const char *key, int count, const char **segmentation);

void usage_page_open(int sync, const char *responder);

#else

#define usage_start()

#define usage_event(a, b, c)

#define usage_page_open(sync, responder)

#endif

