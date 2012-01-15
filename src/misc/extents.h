/*
 *  Extent allocator
 *  Copyright (C) 2011 Andreas Öman
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

typedef struct extent_pool extent_pool_t;

extent_pool_t *extent_create(int start, int size);

void extent_destroy(extent_pool_t *ep);

void extent_stats(const extent_pool_t *ep,
		  int *totalp, int *availp, int *fragmentsp);

int extent_alloc_aligned(extent_pool_t *ep, int size, int alignment);

int extent_alloc(extent_pool_t *ep, int size);

int extent_free(extent_pool_t *ep, int pos, int size);

void extent_dump(extent_pool_t *ep);

