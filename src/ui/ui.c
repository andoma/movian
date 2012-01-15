/*
 *  User interface top control
 *  Copyright (C) 2007 Andreas Öman
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

#include <string.h>
#include <stdio.h>
#include "arch/threads.h"

#include "showtime.h"
#include "event.h"
#include "ui.h"
#include "prop/prop.h"

static hts_mutex_t ui_mutex;
static struct ui_list uis;
static struct uii_list uiis;
static uii_t *primary_uii;

/**
 *
 */
void
uii_register(uii_t *uii, int primary)
{
  if(primary)
    primary_uii = uii;
  hts_mutex_lock(&ui_mutex);
  LIST_INSERT_HEAD(&uiis, uii, uii_link);
  hts_mutex_unlock(&ui_mutex);
}



/**
 *
 */
static void
ui_register(void)
{
#define link_ui(name) do {\
 extern ui_t name ## _ui;\
 LIST_INSERT_HEAD(&uis, &name ## _ui, ui_link);\
}while(0)

#ifdef CONFIG_APPLEREMOTE
  link_ui(appleremote);
#endif
#ifdef CONFIG_GU
  link_ui(gu);
#endif
#ifdef CONFIG_GLW
  link_ui(glw);
#endif

  
  
}

/**
 *
 */
static ui_t *
ui_by_name(const char *name)
{
  ui_t *ui;
  LIST_FOREACH(ui, &uis, ui_link)
    if(!strcmp(ui->ui_title, name))
      break;
  return ui;
}


/**
 *
 */
static ui_t *
ui_default(void)
{
#ifdef SHOWTIME_DEFAULT_UI
  return ui_by_name(SHOWTIME_DEFAULT_UI);
#else
  return NULL;
#endif
}


struct uiboot {
  TAILQ_ENTRY(uiboot) link;
  ui_t *ui;
  int argc;
  char *argv[32];
};


/**
 *
 */
static void *
ui_trampoline(void *aux)
{
  struct uiboot *ub = aux;
  prop_t *p = prop_create_root("ui");

  ub->ui->ui_start(ub->ui, p, ub->argc, ub->argv, 0);
  free(ub->argv[0]);
  free(ub);
  return NULL;
}

/**
 * Start the user interface(s)
 */
int
ui_start(int argc, const char *argv[], const char *argv00)
{
  ui_t *ui;
  int i, r;
  char *argv0 = mystrdupa(argv00);
  struct uiboot *ub, *prim = NULL;
  prop_t *p;

  TAILQ_HEAD(, uiboot) ubs;

  hts_mutex_init(&ui_mutex);
  ui_register();

  if(argc == 0) {
    /* No UI arguments, simple case */
    ui = ui_default();

    if(ui == NULL) {
      TRACE(TRACE_ERROR, "UI", "No default user interface specified, exiting");
      return 2;
    }
    p = prop_create_root("ui");
    return ui->ui_start(ui, p, 1, &argv0, 1);
  }

  TAILQ_INIT(&ubs);
  // This is not exactly beautiful, but works.
  for(i = 0; i < argc; i++) {
    char *o, *s = strdup(argv[i]);

    ub = malloc(sizeof(struct uiboot));

    ub->argc = 0;

    while(ub->argc < 32 && (o = strsep(&s, " ")) != NULL)
      ub->argv[ub->argc++] = o;

    if((ui = ui_by_name(ub->argv[0])) == NULL) {
      TRACE(TRACE_ERROR, "UI", 
	    "User interface \"%s\" not found", ub->argv[0]);
      continue;
    }

    ub->ui = ui;

    if(ui->ui_flags & UI_MAINTHREAD) {
      if(prim != NULL) {
	TRACE(TRACE_ERROR, "UI", 
	      "User interface \"%s\" can not start because it must run "
	      "in main thread but \"%s\" already do",
	      argv[i], prim->ui->ui_title);
	continue;
      }
      prim = ub;
      ui->ui_num_instances++;
      continue;

    } else if(ui->ui_flags & UI_SINGLETON) {
      if(ui->ui_num_instances > 0) {
	TRACE(TRACE_ERROR, "UI", 
	      "User interface \"%s\" not starting, only one instance "
	      "is allowed to run", argv[i]);
	continue;
      }
    }

    ui->ui_num_instances++;
    TAILQ_INSERT_TAIL(&ubs, ub, link);
  }
  
  if(prim == NULL) {
    prim = TAILQ_FIRST(&ubs);

    if(prim == NULL) {
      TRACE(TRACE_ERROR, "UI", "No user interface to start, exiting");
      return 1;
    }

    TAILQ_REMOVE(&ubs, prim, link);
  }

  TAILQ_FOREACH(ub, &ubs, link)
    hts_thread_create_detached(ub->ui->ui_title, ui_trampoline, ub,
			       THREAD_PRIO_NORMAL);

  p = prop_create_root("ui");
  r = prim->ui->ui_start(prim->ui, p, prim->argc, prim->argv, 1);
  free(prim->argv[0]);
  free(prim);
  return r;
}


/**
 *
 */
int
ui_shutdown(void)
{
  uii_t *uii = primary_uii;
  if(uii == NULL || uii->uii_ui->ui_stop == NULL)
    return -1;
  uii->uii_ui->ui_stop(uii);
  return 0;
}


/**
 * Deliver event to primary UI
 */
void
ui_primary_event(event_t *e)
{
  uii_t *uii = primary_uii;

  if(uii != NULL)
    uii->uii_ui->ui_dispatch_event(uii, e);
  else
    event_release(e);
}
