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

typedef struct cancellable {
  int cancelled;
  void (*cancel)(void *opaque);
  void *opaque;
} cancellable_t;


static inline int cancellable_is_cancelled(const cancellable_t *c)
{
  return c != NULL && c->cancelled;
}

void cancellable_bind(cancellable_t *c, void (*fn)(void *opaque),
                      void *opaque);

void cancellable_unbind(cancellable_t *c);

void cancellable_cancel(cancellable_t *c);

static inline void cancellable_reset(cancellable_t *c)
{
  c->cancelled = 0;
  c->cancel = NULL;
}
