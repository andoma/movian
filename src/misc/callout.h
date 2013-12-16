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

#ifndef CALLOUT_H__
#define CALLOUT_H__

#include <stdint.h>
#include "queue.h"

struct callout;
typedef void (callout_callback_t)(struct callout *c, void *opaque);

typedef struct callout {
  LIST_ENTRY(callout) c_link;
  callout_callback_t *c_callback;
  void *c_opaque;
  uint64_t c_deadline;
} callout_t;

void callout_arm(callout_t *c, callout_callback_t *callback,
		  void *opaque, int delta);

void callout_arm_hires(callout_t *d, callout_callback_t *callback,
		       void *opaque, uint64_t delta);

void callout_disarm(callout_t *c);

void callout_init(void);

#define callout_isarmed(c) ((c)->c_callback != NULL)

#endif /* CALLOUT_H__ */
