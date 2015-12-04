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

#include <unistd.h>


#include "main.h"
#include "fileaccess.h"
#include "fa_filepicker.h"
#include "task.h"

LIST_HEAD(filepicker_dir_list, filepicker_dir);

/**
 *
 */
typedef struct filepicker_dir {
  LIST_ENTRY(filepicker_dir) fd_link;
  prop_t *fd_root;
} filepicker_dir_t;


/**
 *
 */
typedef struct filepicker {
  rstr_t *fp_path;
  prop_t *fp_pages;
  struct filepicker_dir_list fp_dirstack;
  int fp_run;
  int fp_flags;
} filepicker_t;



static int
isdir(int type)
{
  return
    type == CONTENT_DIR ||
    type == CONTENT_SHARE ||
    type == CONTENT_ARCHIVE;
}

/**
 *
 */
static void
popdir(filepicker_t *fp)
{
  filepicker_dir_t *fd = LIST_FIRST(&fp->fp_dirstack);
  LIST_REMOVE(fd, fd_link);
  prop_destroy(fd->fd_root);
  free(fd);
  fd = LIST_FIRST(&fp->fp_dirstack);
  if(fd != NULL) {
    prop_select(fd->fd_root);
  } else {
    fp->fp_run = 0;
  }
}


/**
 *
 */
static void
filpicker_scandir(const char *path, prop_t *model, int flags)
{
  char errbuf[512];
  fa_dir_t *fd = fa_scandir(path, errbuf, sizeof(errbuf));
  if(fd == NULL) {
    prop_set(model, "error", PROP_SET_STRING, errbuf);
    return;
  }
  prop_set(model, "loading", PROP_SET_INT, 0);

  prop_t *nodes = prop_create_r(model, "nodes");
  fa_dir_entry_t *fde;

  RB_FOREACH(fde, &fd->fd_entries, fde_link) {

    if(!fde->fde_statdone)
      fa_stat_ex(rstr_get(fde->fde_url), &fde->fde_stat, NULL, 0,
                 FA_NON_INTERACTIVE);

    if(!isdir(fde->fde_type) && !(flags & FILEPICKER_FILES))
      continue;

    prop_t *node = prop_create_root(NULL);
    prop_set(node, "url", PROP_SET_RSTRING, fde->fde_url);
    prop_set(node, "name", PROP_SET_RSTRING, fde->fde_filename);
    prop_set(node, "type", PROP_SET_STRING, content2type(fde->fde_type));
    prop_set(node, "dir", PROP_SET_INT, isdir(fde->fde_type));
    if(prop_set_parent(node, nodes))
      prop_destroy(node);
  }
  fa_dir_free(fd);
}


/**
 *
 */
static void
filepicker_load_dir(filepicker_t *fp, const char *path, const char *title)
{
  char tmp[512];
  filepicker_dir_t *fd = calloc(1, sizeof(filepicker_dir_t));

  fd->fd_root = prop_ref_inc(prop_create_root(NULL));

  if(prop_set_parent(fd->fd_root, fp->fp_pages)) {
    // popup destroyed, bail out
    prop_destroy(fd->fd_root);
    prop_ref_dec(fd->fd_root);
    free(fd);
    fp->fp_run = 0;
    return;
  }
  prop_select(fd->fd_root);

  prop_set(fd->fd_root, "canGoBack", PROP_SET_INT,
           LIST_FIRST(&fp->fp_dirstack) != NULL);

  LIST_INSERT_HEAD(&fp->fp_dirstack, fd, fd_link);

  if(title == NULL) {
    fa_url_get_last_component(tmp, sizeof(tmp), path);
    title = tmp;
  }

  prop_t *model = prop_create_r(fd->fd_root, "model");
  prop_set(model, "loading", PROP_SET_INT, 1);
  prop_set(model, "title", PROP_SET_STRING, title);
  filpicker_scandir(path, model, fp->fp_flags);
  prop_ref_dec(model);
}



/**
 *
 */
static void
pick_url(filepicker_t *fp, const char *url, const char *how)
{
  fa_stat_t fs;
  char errbuf[512];
  if(fa_stat(url, &fs, errbuf, sizeof(errbuf))) {
    TRACE(TRACE_ERROR, "filepicker", "%s", errbuf);
    return;
  }

  int do_descend = isdir(fs.fs_type);

  if(fp->fp_flags & FILEPICKER_DIRECTORIES)
    do_descend = how != NULL && !strcmp(how, "descend");


  if(do_descend) {
    filepicker_load_dir(fp, url, NULL);
  } else {
    fp->fp_path = rstr_alloc(url);
    fp->fp_run = 0;
  }
}


/**
 *
 */
static void
filepicker_event(filepicker_t *fp, event_t *e)
{
  if(event_is_type(e, EVENT_OPENURL)) {
    const event_openurl_t *eo = (const event_openurl_t *)e;

    pick_url(fp, eo->url, eo->how);
  } else if(event_is_action(e, ACTION_NAV_BACK)) {
    popdir(fp);

  } else {
    printf("filepicker not handling event %s\n", event_sprint(e));

  }
}

/**
 *
 */
static void
eventsink(void *opaque, prop_event_t event, ...)
{
  filepicker_t *fp = opaque;
  va_list ap;
  va_start(ap, event);

  switch(event) {
  default:
    break;

  case PROP_DESTROYED:
    fp->fp_run = 0;
    break;


  case PROP_EXT_EVENT:
    filepicker_event(fp, va_arg(ap, event_t *));
    break;
  }
  va_end(ap);
}


/**
 *
 */
static rstr_t *
filepicker_pick(const char *title, int flags)
{
  filepicker_t fp = {0};
  filepicker_dir_t *fd;

  fp.fp_run = 1;
  fp.fp_flags = flags;

  prop_t *p = prop_create_root(NULL);
  prop_set(p, "type", PROP_SET_STRING, "filepicker");

  if(title == NULL)
    prop_set(p, "title", PROP_ADOPT_RSTRING, _("Select a file"));
  else
    prop_set(p, "title", PROP_SET_STRING, title);

  if(flags & FILEPICKER_DIRECTORIES)
    prop_set(p, "dirmode", PROP_SET_INT, 1);

  fp.fp_pages = prop_create_r(p, "pages");

  prop_courier_t *pc = prop_courier_create_waitable();

  prop_sub_t *s = prop_subscribe(PROP_SUB_TRACK_DESTROY,
				 PROP_TAG_CALLBACK, eventsink, &fp,
				 PROP_TAG_NAMED_ROOT, p, "node",
				 PROP_TAG_COURIER, pc,
                                 PROP_TAG_NAME("node", "eventSink"),
				 NULL);

  if(prop_set_parent(p, prop_create(prop_get_global(), "popups"))) {
    /* popuproot is a zombie, this is an error */
    abort();
  }

  filepicker_load_dir(&fp, "vfs:///", NULL);

  while(fp.fp_run) {
    prop_courier_wait_and_dispatch(pc);
  }

  prop_unsubscribe(s);
  prop_courier_destroy(pc);

  while((fd = LIST_FIRST(&fp.fp_dirstack)) != NULL) {
    LIST_REMOVE(fd, fd_link);
    prop_destroy(fd->fd_root);
    free(fd);
  }
  prop_destroy(p);

  return fp.fp_path;
}


/**
 *
 */
typedef struct fpaux {
  char *title;
  prop_t *target;
  char *url;
  int flags;
} fpaux_t;


/**
 *
 */
static void
filepicker_pick_to_prop_task(void *aux)
{
  fpaux_t *a = aux;
  rstr_t *res = filepicker_pick(a->title, a->flags);

  if(res != NULL) {
    prop_set_rstring(a->target, res);
    rstr_release(res);
  }
  free(a->title);
  prop_ref_dec(a->target);
  free(a->url);
  free(a);
}


/**
 *
 */
void
filepicker_pick_to_prop(const char *title, prop_t *target, const char *current,
                        int flags)
{
  fpaux_t *a = calloc(1, sizeof(fpaux_t));
  a->title = title ? strdup(title) : NULL;
  a->target = prop_ref_inc(target);
  a->url = current ? strdup(current) : NULL;
  a->flags = flags;
  task_run(filepicker_pick_to_prop_task, a);
}
