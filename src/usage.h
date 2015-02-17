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

#if ENABLE_USAGEREPORT

void usage_init(void);

void usage_fini(void);

void usage_report_send(int stored);

void usage_inc_counter(const char *id, int value);

void usage_inc_plugin_counter(const char *plugin, const char *id, int value);

#else

#define usage_init()

#define usage_fini()

#define usage_report_send(stored)

#define usage_inc_counter(id, value)

#define usage_inc_plugin_counter(plugin, id, value)

#endif

