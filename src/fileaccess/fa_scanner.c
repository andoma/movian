/*
 *  File backend directory scanner
 *  Copyright (C) 2008 - 2009 Andreas Öman
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


#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include "showtime.h"
#include "navigator.h"
#include "fileaccess.h"
#include "fa_probe.h"
#include "playqueue.h"
#include "misc/strtab.h"
#include "prop/prop_nodefilter.h"

extern int media_buffer_hungry;

typedef struct scanner {
  int s_refcount;

  char *s_url;
  char *s_playme;

  prop_t *s_nodes;
  prop_t *s_contents;
  prop_t *s_loading;
  prop_t *s_root;

  int s_stop;

  fa_dir_t *s_fd;

  void *s_ref;

} scanner_t;






/**
 *
 */
static void
set_type(prop_t *proproot, unsigned int type)
{
  const char *typestr;

  if((typestr = content2type(type))) {
    prop_t *p = prop_create_check(proproot, "type");
    if(p != NULL) {
      prop_set_string(p, typestr);
      prop_ref_dec(p);
    }
  }
}


/**
 *
 */
static rstr_t *
make_filename(const char *filename)
{
  char *s = mystrdupa(filename);
  char *p = strrchr(s, '.');
  if(p != NULL)
    *p = 0;

  return rstr_alloc(s);
}



/**
 *
 */
static void
make_prop(fa_dir_entry_t *fde)
{
  prop_t *p = prop_create_root(NULL);
  prop_t *metadata;
  rstr_t *fname;

  if(fde->fde_type == CONTENT_DIR) {
    fname = rstr_alloc(fde->fde_filename);
  } else {
    fname = make_filename(fde->fde_filename);
  }

  prop_set_string(prop_create(p, "url"), fde->fde_url);
  set_type(p, fde->fde_type);

  prop_set_rstring(prop_create(p, "filename"), fname);

  if(fde->fde_metadata != NULL) {

    metadata = fde->fde_metadata;

    if(prop_set_parent(metadata, p))
      abort();

    fde->fde_metadata = NULL;
  } else {
    metadata = prop_create(p, "metadata");
    prop_set_rstring(prop_create(metadata, "title"), fname);
  }

  rstr_release(fname);
  assert(fde->fde_prop == NULL);
  fde->fde_prop = prop_ref_inc(p);
}

/**
 *
 */
static struct strtab postfixtab[] = {
  { "iso",             CONTENT_DVD },
  
  { "jpeg",            CONTENT_IMAGE },
  { "jpg",             CONTENT_IMAGE },
  { "png",             CONTENT_IMAGE },
  { "gif",             CONTENT_IMAGE },

  { "mp3",             CONTENT_AUDIO },
  { "m4a",             CONTENT_AUDIO },
  { "flac",            CONTENT_AUDIO },
  { "aac",             CONTENT_AUDIO },
  { "wma",             CONTENT_AUDIO },
  { "ogg",             CONTENT_AUDIO },
  { "spc",             CONTENT_AUDIO },

  { "mkv",             CONTENT_VIDEO },
  { "avi",             CONTENT_VIDEO },
  { "mov",             CONTENT_VIDEO },
  { "m4v",             CONTENT_VIDEO },
  { "ts",              CONTENT_VIDEO },
  { "mpg",             CONTENT_VIDEO },
  { "wmv",             CONTENT_VIDEO },

  { "sid",             CONTENT_ALBUM },

  { "pdf",             CONTENT_UNKNOWN },
  { "nfo",             CONTENT_UNKNOWN },
  { "gz",              CONTENT_UNKNOWN },
  { "txt",             CONTENT_UNKNOWN },
};


/**
 *
 */
static int
type_from_filename(const char *filename)
{
  int type;
  const char *str;

  if((str = strrchr(filename, '.')) == NULL)
    return CONTENT_FILE;
  str++;
  
  if((type = str2val(str, postfixtab)) == -1)
    return CONTENT_FILE;
  return type;
}

/**
 *
 */
static void
quick_analyzer(fa_dir_t *fd, prop_t *contents)
{
  fa_dir_entry_t *fde;
  int images = 0;

  TAILQ_FOREACH(fde, &fd->fd_entries, fde_link) {

    if(fde->fde_probestatus != FDE_PROBE_NONE)
      continue;

    if(fde->fde_type == CONTENT_DIR)
      continue;
    
    if(fde->fde_type == CONTENT_FILE)
      fde->fde_type = type_from_filename(fde->fde_filename);

    fde->fde_probestatus = FDE_PROBE_FILENAME;

    if(fde->fde_type == CONTENT_IMAGE)
      images++;
  }

  if(images * 4 > fd->fd_count * 3)
    prop_set_string(contents, "images");
}


/**
 *
 */
static void
deep_probe(fa_dir_entry_t *fde)
{
  prop_t *metadata;

  fde->fde_probestatus = FDE_PROBE_DEEP;

  if(fde->fde_type != CONTENT_UNKNOWN) {

    metadata = prop_ref_inc(prop_create(fde->fde_prop, "metadata"));
    
    if(fde->fde_type == CONTENT_DIR) {
      fde->fde_type = fa_probe_dir(metadata, fde->fde_url);
    } else {
      fde->fde_type = fa_probe(metadata, fde->fde_url, NULL, 0,
			       NULL, 0,
			       fde->fde_statdone ? &fde->fde_stat : NULL, 1);
    }
    prop_ref_dec(metadata);
  }
  set_type(fde->fde_prop, fde->fde_type);
}



/**
 *
 */
static void
deep_analyzer(scanner_t *s)
{
  fa_dir_entry_t *fde;

  /* Empty */
  if(s->s_fd->fd_count == 0) {
    prop_set_string(s->s_contents, "empty");
    return;
  }

  /* Scan all entries */
  TAILQ_FOREACH(fde, &s->s_fd->fd_entries, fde_link) {

    while(media_buffer_hungry && !s->s_stop) 
      sleep(1);

    if(s->s_stop)
      break;

    if(fde->fde_probestatus != FDE_PROBE_DEEP)
      deep_probe(fde);
  }
}


/**
 *
 */
static void
scanner_unref(scanner_t *s)
{
  if(atomic_add(&s->s_refcount, -1) > 1)
    return;

  fa_unreference(s->s_ref);
  free(s);
}


/**
 *
 */
static int
scanner_checkstop(void *opaque)
{
  scanner_t *s = opaque;
  return !!s->s_stop;
}


/**
 *
 */
static void
scanner_entry_setup(scanner_t *s, fa_dir_entry_t *fde)
{
  if(fde->fde_type == CONTENT_FILE)
    fde->fde_type = type_from_filename(fde->fde_filename);

  make_prop(fde);

  deep_probe(fde);

  if(!prop_set_parent(fde->fde_prop, s->s_nodes))
    return; // OK
  
  prop_destroy(fde->fde_prop);
  fde->fde_prop = NULL;
}

/**
 *
 */
static void
scanner_entry_destroy(scanner_t *s, fa_dir_entry_t *fde)
{
  if(fde->fde_prop != NULL)
    prop_destroy(fde->fde_prop);
  fa_dir_entry_free(s->s_fd, fde);
}

/**
 *
 */
static void
scanner_notification(void *opaque, fa_notify_op_t op, const char *filename,
		     const char *url, int type)
{
  scanner_t *s = opaque;
  fa_dir_entry_t *fde;

  if(filename[0] == '.')
    return; /* Skip all dot-filenames */

  switch(op) {
  case FA_NOTIFY_DEL:
    TAILQ_FOREACH(fde, &s->s_fd->fd_entries, fde_link)
      if(!strcmp(filename, fde->fde_filename))
	break;

    if(fde != NULL)
      scanner_entry_destroy(s, fde);
    break;

  case FA_NOTIFY_ADD:
    scanner_entry_setup(s, fa_dir_add(s->s_fd, url, filename, type));
    break;
  }
  deep_analyzer(s);
}


/**
 * Very simple and O^2 diff
 *
 * We should also check mtime and trig change if needed
 */
static void
rescan(scanner_t *s)
{
  fa_dir_t *fd;
  fa_dir_entry_t *a, *b, *n;
  int changed = 0;

  if((fd = fa_scandir(s->s_url, NULL, 0)) == NULL)
    return; 

  for(a = TAILQ_FIRST(&s->s_fd->fd_entries); a != NULL; a = n) {
    n = TAILQ_NEXT(a, fde_link);
    TAILQ_FOREACH(b, &fd->fd_entries, fde_link)
      if(!strcmp(a->fde_url, b->fde_url))
	break;

    if(b != NULL) {
      // Exists in old and new set, all fine
      fa_dir_entry_free(fd, b);
    } else {
      changed = 1;
      // Exists in old but not in new
      scanner_entry_destroy(s, a);
    }
  }

  while((b = TAILQ_FIRST(&fd->fd_entries)) != NULL) {
    TAILQ_REMOVE(&fd->fd_entries, b, fde_link);
    TAILQ_INSERT_TAIL(&s->s_fd->fd_entries, b, fde_link);
    scanner_entry_setup(s, b);
    changed = 1;
  }

  if(changed)
    deep_analyzer(s);
}


/**
 *
 */
static void
doscan(scanner_t *s)
{
  fa_dir_entry_t *fde;
  fa_dir_t *fd = s->s_fd;
  prop_vec_t *pv;

  quick_analyzer(s->s_fd, s->s_contents);

  pv = prop_vec_create(fd->fd_count);

  TAILQ_FOREACH(fde, &fd->fd_entries, fde_link) {
    make_prop(fde);
    pv = prop_vec_append(pv, fde->fde_prop);
  }

  prop_set_parent_vector(pv, s->s_nodes, NULL, NULL);
  prop_vec_release(pv);

  TAILQ_FOREACH(fde, &fd->fd_entries, fde_link) {
    if(s->s_playme != NULL &&
       !strcmp(s->s_playme, fde->fde_url)) {
      playqueue_load_with_source(fde->fde_prop, s->s_root, 0);
      free(s->s_playme);
      s->s_playme = NULL;
    }
  }

  prop_set_int(s->s_loading, 0);

  deep_analyzer(s);

  if(!fa_notify(s->s_url, s, scanner_notification, scanner_checkstop))
    return;
  
  /* Can not do notifcations */

#ifdef WII
  // We don't want to keep threads running on wii 
  return;
#endif

  while(!s->s_stop) {
    sleep(3);

    if(!media_buffer_hungry)
      rescan(s);
  }
}

/**
 *
 */
static void *
scanner(void *aux)
{
  scanner_t *s = aux;

  if((s->s_fd = fa_scandir(s->s_url, NULL, 0)) != NULL) {
    doscan(s);
    fa_dir_free(s->s_fd);
  }

  prop_set_int(s->s_loading, 0);

  free(s->s_url);
  free(s->s_playme);

  prop_ref_dec(s->s_root);
  prop_ref_dec(s->s_nodes);
  prop_ref_dec(s->s_contents);
  prop_ref_dec(s->s_loading);

  scanner_unref(s);
  return NULL;
}


/**
 *
 */
static void
scanner_stop(void *opaque, prop_event_t event, ...)
{
  scanner_t *s = opaque;

  va_list ap;
  va_start(ap, event);

  if(event != PROP_DESTROYED) 
    return;

  (void)va_arg(ap, prop_t *);
  prop_unsubscribe(va_arg(ap, prop_sub_t *));

  s->s_stop = 1;
  scanner_unref(s);
}


/**
 *
 */
void
fa_scanner(const char *url, prop_t *model, const char *playme)
{
  scanner_t *s = calloc(1, sizeof(scanner_t));
  prop_t *source = prop_create(model, "source");

  struct prop_nf *pnf;

  pnf = prop_nf_create(prop_create(model, "nodes"),
		       source,
		       prop_create(model, "filter"),
		       "node.filename", PROP_NF_AUTODESTROY);
  
  prop_nf_pred_str_add(pnf, "node.type",
		       PROP_NF_CMP_EQ, "unknown", NULL, 
		       PROP_NF_MODE_EXCLUDE);

  prop_nf_release(pnf);

  prop_set_int(prop_create(model, "canFilter"), 1);

  s->s_url = strdup(url);
  s->s_playme = playme != NULL ? strdup(playme) : NULL;

  prop_set_int(prop_create(model, "loading"), 1);

  s->s_root = prop_ref_inc(model);
  s->s_nodes = prop_ref_inc(source);
  s->s_contents = prop_ref_inc(prop_create(model, "contents"));
  s->s_loading = prop_ref_inc(prop_create(model, "loading"));

  s->s_refcount = 2; // One held by scanner thread, one by the subscription

  s->s_ref = fa_reference(s->s_url);

  hts_thread_create_detached("fa scanner", scanner, s, THREAD_PRIO_LOW);

  prop_subscribe(PROP_SUB_TRACK_DESTROY,
		 PROP_TAG_CALLBACK, scanner_stop, s,
		 PROP_TAG_ROOT, s->s_root,
		 NULL);
}
