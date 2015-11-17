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
#include "queue.h"
#include "lockmgr.h"

struct callout;
typedef void (callout_callback_t)(struct callout *c, void *opaque);

typedef struct callout {
  LIST_ENTRY(callout) c_link;
  callout_callback_t *c_callback;
  lockmgr_fn_t *c_lockmgr;
  void *c_opaque;
  uint64_t c_deadline;
  int64_t c_delta;
  const char *c_armed_by_file;
  int c_armed_by_line;

} callout_t;

void callout_arm_x(callout_t *c, callout_callback_t *callback,
                   void *opaque, int delta, const char *file, int line);

#define callout_arm(a,b,c,d) callout_arm_x(a,b,c,d,__FILE__,__LINE__);

void callout_arm_hires_x(callout_t *d, callout_callback_t *callback,
                         void *opaque, int64_t delta,
                         const char *file, int line);

#define callout_arm_hires(a,b,c,d) \
 callout_arm_hires_x(a,b,c,d,__FILE__,__LINE__);

void callout_arm_managed_x(callout_t *d, callout_callback_t *callback,
                           void *opaque, int64_t delta,
                           lockmgr_fn_t *lockmgr,
                           const char *file, int line);

#define callout_arm_managed(a,b,c,d,e) \
  callout_arm_managed_x(a,b,c,d,e,__FILE__,__LINE__);

void callout_rearm(callout_t *c, int64_t delta);

void callout_disarm(callout_t *c);

void callout_init(void);

void callout_update_clock_props(void);

#define callout_isarmed(c) ((c)->c_callback != NULL)
