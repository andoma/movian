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

/**
 * libogc threads
 */

/**
 * Mutexes
 */

#include <ogc/mutex.h>
typedef mutex_t hts_mutex_t;

extern void hts_mutex_init(hts_mutex_t *m);
#define hts_mutex_lock(m)     LWP_MutexLock(*(m))
#define hts_mutex_unlock(m)   LWP_MutexUnlock(*(m))
#define hts_mutex_destroy(m)  LWP_MutexDestroy(*(m))
#define hts_mutex_assert(m)

/**
 * Condition variables
 */
#include <ogc/cond.h>
typedef cond_t hts_cond_t;

extern void hts_cond_init(hts_cond_t *c);
#define hts_cond_signal(c)             LWP_CondSignal(*(c))
#define hts_cond_broadcast(c)          LWP_CondBroadcast(*(c))
#define hts_cond_wait(c, m)            LWP_CondWait(*(c), *(m))
#define hts_cond_destroy(c)            LWP_CondDestroy(*(c))
extern int hts_cond_wait_timeout(hts_cond_t *c, hts_mutex_t *m, int delta);

/**
 * Threads
 */
#include <ogc/lwp.h>

typedef lwp_t hts_thread_t;

extern void hts_thread_create_detached(const char *, void *(*)(void *), void *);

extern void hts_thread_create_joinable(const char *, hts_thread_t *p, 
				       void *(*)(void *), void *);

#define hts_thread_join(t)       LWP_JoinThread(*(t), NULL)

#define hts_thread_detach(t)

#define hts_thread_current()     LWP_GetSelf()
