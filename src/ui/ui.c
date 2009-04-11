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
#include <arch/threads.h>

#include "showtime.h"
#include "ui.h"
#include "keymapper.h"


static hts_mutex_t ui_mutex;
static struct ui_list uis;
static struct uii_list uiis;
//static uii_t *primary_uii;

static int ui_event_handler(event_t *e, void *opaque);

/**
 *
 */
void
ui_exit_showtime(int retcode)
{
  exit(retcode);
}

/**
 *
 */
void
uii_register(uii_t *uii)
{
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

#ifdef CONFIG_GU
  link_ui(gu);
#endif
#ifdef CONFIG_GLW_FRONTEND_X11
  link_ui(glw_x11);
#endif
#ifdef CONFIG_GLW_FRONTEND_WII
  link_ui(glw_wii);
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
  ub->ui->ui_start(ub->ui, ub->argc, ub->argv);
  return NULL;
}

/**
 * Start the user interface(s)
 */
int
ui_start(int argc, const char *argv[], const char *argv00)
{
  ui_t *ui;
  int i;
  char *argv0 = strdup(argv00);
  struct uiboot *ub, *prim = NULL;

  TAILQ_HEAD(, uiboot) ubs;

  hts_mutex_init(&ui_mutex);
  keymapper_init();
  ui_register();
  event_handler_register("uimain", ui_event_handler, EVENTPRI_MAIN, NULL);


  if(argc == 0) {
    /* No UI arguments, simple case */
    ui = ui_default();

    if(ui == NULL) {
      fprintf(stderr, "No default user interface specified, exiting\n");
      return 2;
    }

    return ui->ui_start(ui, 1, &argv0);
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
      fprintf(stderr, "User interface \"%s\" not found\n", ub->argv[0]);
      continue;
    }

    ub->ui = ui;

    if(ui->ui_flags & UI_MAINTHREAD) {
      if(prim != NULL) {
	fprintf(stderr, 
		"User interface \"%s\" can not start because it must run "
		"in main thread but \"%s\" already do\n",
		argv[i], prim->ui->ui_title);
	continue;
      }
      prim = ub;
      ui->ui_num_instances++;
      continue;

    } else if(ui->ui_flags & UI_SINGLETON) {
      if(ui->ui_num_instances > 0) {
	fprintf(stderr, 
		"User interface \"%s\" not starting, only one instance "
		"is allowed to run\n", argv[i]);
	continue;
      }
    }

    ui->ui_num_instances++;
    TAILQ_INSERT_TAIL(&ubs, ub, link);
  }
  
  if(prim == NULL) {
    prim = TAILQ_FIRST(&ubs);
    TAILQ_REMOVE(&ubs, prim, link);
  }

  TAILQ_FOREACH(ub, &ubs, link)
    hts_thread_create_detached(ui_trampoline, ub);

  return prim->ui->ui_start(prim->ui, prim->argc, prim->argv);
}


/**
 *
 */
void
ui_dispatch_event(event_t *e, const char *buf, uii_t *uii)
{
  int r, l;
  event_keydesc_t *ek;

  if(buf != NULL) {
    l = strlen(buf);
    ek = event_create(EVENT_KEYDESC, sizeof(event_keydesc_t) + l + 1);
    memcpy(ek->desc, buf, l + 1);
    ui_dispatch_event(&ek->h, NULL, uii);

    keymapper_resolve(buf, uii);
  }

  if(e == NULL)
    return;

  if(uii != NULL && uii->uii_ui->ui_dispatch_event != NULL) {
    r = uii->uii_ui->ui_dispatch_event(uii, e);
  } else {

    r = 0;
    
    LIST_FOREACH(uii, &uiis, uii_link) {
      if(uii->uii_ui->ui_dispatch_event != NULL) 
	if((r = uii->uii_ui->ui_dispatch_event(uii, e)) != 0)
	  break;
    }
  }

  if(r == 0) {
    /* Not consumed, drop it into the main event dispatcher */
    event_post(e);
  } else {
    event_unref(e);
  }
}


/**
 * Catch events used for exiting
 */
static int
ui_event_handler(event_t *e, void *opaque)
{
  int v = 0;
  switch(e->e_type) {
  default:
    return 0;

  case EVENT_CLOSE:
    v = 0;
    break;

  case EVENT_QUIT:
    v = 0;
    break;

  case EVENT_POWER:
    v = 10;
    break;
  }

  ui_exit_showtime(v);
  return 1;
}

