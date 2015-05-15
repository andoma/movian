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

#include "arch/atomic.h"

typedef struct cancellable cancellable_t;

int cancellable_is_cancelled(const cancellable_t *c);

cancellable_t *cancellable_bind(cancellable_t *c,
                                void (*cancel)(void *opaque),
                                void *opaque)  attribute_unused_result;

void cancellable_unbind(cancellable_t *c, void *opaque);

void cancellable_cancel(cancellable_t *c);

void cancellable_cancel_locked(cancellable_t *c);

void cancellable_reset(cancellable_t *c);

cancellable_t *cancellable_create(void);

void cancellable_release(cancellable_t *c);

cancellable_t *cancellable_retain(cancellable_t *c);
