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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>
#include <unistd.h>
#include <math.h>
#include <sys/time.h>

#include "arch/atomic.h"

#include "main.h"
#include "prop_i.h"
#include "misc/str.h"
#include "event.h"
#include "misc/pool.h"


int alarm_fired = 0;


/**
 *
 */
void
prop_courier_poll_with_alarm(prop_courier_t *pc, int maxtime)
{
  prop_notify_t *n, *next;

  if(!hts_mutex_trylock(&prop_mutex)) {
    TAILQ_MERGE(&pc->pc_dispatch_queue, &pc->pc_queue_exp, hpn_link);
    TAILQ_MERGE(&pc->pc_dispatch_queue, &pc->pc_queue_nor, hpn_link);

    for(n = TAILQ_FIRST(&pc->pc_free_queue); n != NULL; n = next) {
      next = TAILQ_NEXT(n, hpn_link);

      prop_sub_ref_dec_locked(n->hpn_sub);
      pool_put(notify_pool, n);
    }
    TAILQ_INIT(&pc->pc_free_queue);

    hts_mutex_unlock(&prop_mutex);
  }

  if(TAILQ_FIRST(&pc->pc_dispatch_queue) == NULL)
    return;

  alarm_fired = 0;

  struct itimerval it;
  it.it_value.tv_sec = 0;
  it.it_value.tv_usec = maxtime;
  it.it_interval.tv_sec = 0;
  it.it_interval.tv_usec = 0;
  setitimer(ITIMER_REAL, &it, NULL);


  while((n = TAILQ_FIRST(&pc->pc_dispatch_queue)) != NULL && !alarm_fired) {
    prop_dispatch_one(n, LOCKMGR_LOCK);
    TAILQ_REMOVE(&pc->pc_dispatch_queue, n, hpn_link);
    TAILQ_INSERT_TAIL(&pc->pc_free_queue, n, hpn_link);
  }

  it.it_value.tv_usec = 0;
  setitimer(ITIMER_REAL, &it, NULL);
}
