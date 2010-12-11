/*
 *  Property grouper
 *  Copyright (C) 2010 Andreas Öman
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

#ifndef PROP_GROUPER_H__
#define PROP_GROUPER_H__

#include "prop.h"

#define PROP_GROUPER_TAKE_DST_OWNERSHIP 0x1

typedef struct prop_grouper prop_grouper_t;

prop_grouper_t *prop_grouper_create(prop_t *dst, prop_t *src,
				    const char *group_key, int flags);

void prop_grouper_destroy(prop_grouper_t *pg);

#endif // PROP_NODEFILTER_H__
