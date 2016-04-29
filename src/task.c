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
#include <assert.h>

#include "main.h"
#include "arch/threads.h"

#include "task.h"
#include "misc/queue.h"

#define MAX_TASK_THREADS 16
#define MAX_IDLE_TASK_THREADS 2

TAILQ_HEAD(task_queue, task);
TAILQ_HEAD(task_group_queue, task_group);

struct task_group {
  atomic_t tg_refcount;
  struct task_queue tg_tasks;
  TAILQ_ENTRY(task_group) tg_link;
};


typedef struct task {
  TAILQ_ENTRY(task) t_link;
  task_fn_t *t_fn;
  void *t_opaque;
  task_group_t *t_group;
} task_t;


static struct task_queue tasks = TAILQ_HEAD_INITIALIZER(tasks);
static struct task_group_queue task_groups =TAILQ_HEAD_INITIALIZER(task_groups);
static unsigned int num_task_threads;
static unsigned int num_task_threads_avail;
static hts_mutex_t task_mutex;
static hts_cond_t task_cond;


/**
 *
 */
static void
task_group_release(task_group_t *tg)
{
  if(atomic_dec(&tg->tg_refcount))
    return;
  assert(TAILQ_FIRST(&tg->tg_tasks) == NULL);
  free(tg);
}


/**
 *
 */
static void *
task_thread(void *aux)
{
  task_t *t;
  task_group_t *tg;

  hts_mutex_lock(&task_mutex);
  while(1) {
    t = TAILQ_FIRST(&tasks);
    tg = TAILQ_FIRST(&task_groups);

    if(t == NULL && tg == NULL) {
      if(num_task_threads_avail == MAX_IDLE_TASK_THREADS)
        break;

      num_task_threads_avail++;
      hts_cond_wait(&task_cond, &task_mutex);
      num_task_threads_avail--;
      continue;
    }

    if(t != NULL) {
      TAILQ_REMOVE(&tasks, t, t_link);
      hts_mutex_unlock(&task_mutex);
      t->t_fn(t->t_opaque);
      free(t);
      hts_mutex_lock(&task_mutex);
      // Released lock, must recheck for task groups
      tg = TAILQ_FIRST(&task_groups);
    }

    if(tg != NULL) {
      // Remove task group while processing as we don't want anyone
      // else to dispatch from this group
      TAILQ_REMOVE(&task_groups, tg, tg_link);

      t = TAILQ_FIRST(&tg->tg_tasks);
      hts_mutex_unlock(&task_mutex);
      t->t_fn(t->t_opaque);
      hts_mutex_lock(&task_mutex);

      // Note that we remove _after_ execution because we don't want
      // any newly inserted task in this group to cause the group
      // to activate (ie, get inserted in task_groups)
      TAILQ_REMOVE(&tg->tg_tasks, t, t_link);
      free(t);

      if(TAILQ_FIRST(&tg->tg_tasks) != NULL) {
        // Still more tasks to work on in this group
        // Reinsert group at tail to maintain fairness between groups
        TAILQ_INSERT_TAIL(&task_groups, tg, tg_link);
      }

      // Decrease refcount owned by task
      task_group_release(tg);
    }
  }

  num_task_threads--;
  hts_mutex_unlock(&task_mutex);
  return NULL;
}


/**
 *
 */
static void
task_schedule()
{
  if(num_task_threads_avail > 0) {
    hts_cond_signal(&task_cond);
  } else {
    if(num_task_threads < MAX_TASK_THREADS) {
      num_task_threads++;
      hts_thread_create_detached("tasks", task_thread, NULL,
                                 THREAD_PRIO_BGTASK);
    }
  }
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
  task_schedule();
  hts_mutex_unlock(&task_mutex);
}



/**
 *
 */
task_group_t *
task_group_create(void)
{
  task_group_t *tg = calloc(1, sizeof(task_group_t));
  atomic_set(&tg->tg_refcount, 1);
  TAILQ_INIT(&tg->tg_tasks);
  return tg;
}


/**
 *
 */
void
task_group_destroy(task_group_t *tg)
{
  task_group_release(tg);
}


/**
 *
 */
void
task_run_in_group(task_fn_t *fn, void *opaque, task_group_t *tg)
{
  task_t *t = calloc(1, sizeof(task_t));
  t->t_fn = fn;
  t->t_opaque = opaque;
  t->t_group = tg;
  atomic_inc(&tg->tg_refcount);
  hts_mutex_lock(&task_mutex);
  if(TAILQ_FIRST(&tg->tg_tasks) == NULL)
    TAILQ_INSERT_TAIL(&task_groups, tg, tg_link);

  TAILQ_INSERT_TAIL(&tg->tg_tasks, t, t_link);
  task_schedule();
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
