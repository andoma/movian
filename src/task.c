/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2014 Lonelycoder AB
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

#include "showtime.h"
#include "arch/threads.h"

#include "task.h"
#include "misc/queue.h"

#define MAX_TASK_THREADS 16
#define MAX_IDLE_TASK_THREADS 2

TAILQ_HEAD(task_queue, task);

typedef struct task {
  TAILQ_ENTRY(task) t_link;
  task_fn_t *t_fn;
  void *t_opaque;
} task_t;

static struct task_queue tasks = TAILQ_HEAD_INITIALIZER(tasks);
static unsigned int num_task_threads;
static unsigned int num_task_threads_avail;
static hts_mutex_t task_mutex;
static hts_cond_t task_cond;


/**
 *
 */
static void *
task_thread(void *aux)
{
  task_t *t;

  hts_mutex_lock(&task_mutex);
  while(1) {
    t = TAILQ_FIRST(&tasks);
    if(t == NULL) {

      if(num_task_threads_avail == MAX_IDLE_TASK_THREADS)
        break;

      num_task_threads_avail++;
      hts_cond_wait(&task_cond, &task_mutex);
      num_task_threads_avail--;
      continue;
    }
    TAILQ_REMOVE(&tasks, t, t_link);

    hts_mutex_unlock(&task_mutex);
    t->t_fn(t->t_opaque);
    free(t);
    hts_mutex_lock(&task_mutex);
  }

  num_task_threads--;
  hts_mutex_unlock(&task_mutex);
  return NULL;
}


/**
 *
 */
static void
task_launch_thread(void)
{
  num_task_threads++;
  hts_thread_create_detached("tasks", task_thread, NULL, THREAD_PRIO_BGTASK);
}


/**
 *
 */
void
task_run(task_fn_t *fn, void *opaque)
{
  task_t *t = calloc(1, sizeof(task_t));
  t->t_fn = fn;
  t->t_opaque = opaque;
  hts_mutex_lock(&task_mutex);
  TAILQ_INSERT_TAIL(&tasks, t, t_link);

  if(num_task_threads_avail > 0) {
    hts_cond_signal(&task_cond);
  } else {
    if(num_task_threads < MAX_TASK_THREADS) {
      task_launch_thread();
    }
  }
  hts_mutex_unlock(&task_mutex);
}


/**
 *
 */
INITIALIZER(taskinit)
{
  hts_mutex_init(&task_mutex);
  hts_cond_init(&task_cond, &task_mutex);
}
