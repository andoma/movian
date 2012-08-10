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
#include "plugins.h"
#include "db/kvstore.h"

extern int media_buffer_hungry;

typedef struct scanner {
  int s_refcount;

  char *s_url;
  time_t s_mtime; // modifiaction time of s_url
  char *s_playme;

  prop_t *s_nodes;
  prop_t *s_loading;
  prop_t *s_root;
  prop_t *s_direct_close;

  int s_stop;

  fa_dir_t *s_fd;

  void *s_ref;

  void *s_metadb;

  struct prop_nf *s_pnf;

} scanner_t;

static void rescan(scanner_t *s);


/**
 *
 */
static void
closedb(scanner_t *s)
{
  if(s->s_metadb != NULL)
    metadb_close(s->s_metadb);
  s->s_metadb = NULL;
}


/**
 *
 */
static void *
getdb(scanner_t *s)
{
  if(s->s_metadb == NULL)
    s->s_metadb = metadb_get();
  return s->s_metadb;
}







/**
 *
 */
static void
set_type(prop_t *proproot, unsigned int type)
{
  const char *typestr;

  if((typestr = content2type(type)) != NULL)
    prop_set_string(prop_create(proproot, "type"), typestr);
}


/**
 *
 */
static void
make_prop(fa_dir_entry_t *fde)
{
  prop_t *p = prop_create_root(NULL);
  prop_t *metadata;

  prop_set_rstring(prop_create(p, "url"), fde->fde_url);
  prop_set_rstring(prop_create(p, "filename"), fde->fde_filename);
  set_type(p, fde->fde_type);

  if(fde->fde_metadata != NULL) {

    metadata = fde->fde_metadata;
    
    if(prop_set_parent(metadata, p))
      abort();

    fde->fde_metadata = NULL;
  } else {

    rstr_t *title;
    if(fde->fde_type == CONTENT_DIR) {
      title = rstr_dup(fde->fde_filename);
    } else {
      title = metadata_remove_postfix(rstr_get(fde->fde_filename), '.');
    }
    
    metadata = prop_create(p, "metadata");
    prop_set_rstring(prop_create(metadata, "title"), title);
    rstr_release(title);
  }

  if(fde->fde_statdone)
    prop_set(metadata, "timestamp", NULL, PROP_SET_INT, fde->fde_stat.fs_mtime);

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
  { "svg",             CONTENT_IMAGE },
  
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
  { "mp4",             CONTENT_VIDEO },
  { "mts",             CONTENT_VIDEO },

  { "sid",             CONTENT_ALBUM },

  { "pdf",             CONTENT_UNKNOWN },
  { "nfo",             CONTENT_UNKNOWN },
  { "gz",              CONTENT_UNKNOWN },
  { "txt",             CONTENT_UNKNOWN },
  { "srt",             CONTENT_UNKNOWN },
  { "smi",             CONTENT_UNKNOWN },
  { "ass",             CONTENT_UNKNOWN },
  { "ssa",             CONTENT_UNKNOWN },
  { "idx",             CONTENT_UNKNOWN },
  { "sub",             CONTENT_UNKNOWN },
  { "exe",             CONTENT_UNKNOWN },
  { "tmp",             CONTENT_UNKNOWN },
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
deep_probe(fa_dir_entry_t *fde, scanner_t *s)
{
  fde->fde_probestatus = FDE_PROBE_DEEP;

  if(fde->fde_type != CONTENT_UNKNOWN) {

    prop_t *meta = prop_create_r(fde->fde_prop, "metadata");
    

    if(!fde->fde_ignore_cache && !fa_dir_entry_stat(fde)) {
      if(fde->fde_md != NULL)
	metadata_destroy(fde->fde_md);
      fde->fde_md = metadb_metadata_get(getdb(s), rstr_get(fde->fde_url),
					fde->fde_stat.fs_mtime);
    }

    if(fde->fde_statdone)
      prop_set(meta, "timestamp", NULL, PROP_SET_INT,
	       fde->fde_stat.fs_mtime);

    if(fde->fde_md == NULL) {

      if(fde->fde_type == CONTENT_DIR)
        fde->fde_md = fa_probe_dir(rstr_get(fde->fde_url));
      else
	fde->fde_md = fa_probe_metadata(rstr_get(fde->fde_url), NULL, 0);
    }
    
    if(fde->fde_md != NULL) {
      fde->fde_type = fde->fde_md->md_contenttype;
      fde->fde_ignore_cache = 0;


      if(fde->fde_type == CONTENT_PLUGIN) {
	plugin_props_from_file(fde->fde_prop, rstr_get(fde->fde_url));
      } else {
	metadata_to_proptree(fde->fde_md, meta, 1);
      }
      
      if(fde->fde_md->md_cached == 0) {
	metadb_metadata_write(getdb(s), rstr_get(fde->fde_url),
			      fde->fde_stat.fs_mtime,
			      fde->fde_md, s->s_url, s->s_mtime);
      }
    }
    prop_ref_dec(meta);

    if(!fde->fde_bound_to_metadb) {
      fde->fde_bound_to_metadb = 1;
      metadb_bind_url_to_prop(getdb(s), rstr_get(fde->fde_url), fde->fde_prop);
    }
  }
  set_type(fde->fde_prop, fde->fde_type);
}


/**
 *
 */
static void
tryplay(scanner_t *s)
{
  fa_dir_entry_t *fde;

  if(s->s_playme == NULL)
    return;

  TAILQ_FOREACH(fde, &s->s_fd->fd_entries, fde_link) {
    if(!strcmp(s->s_playme, rstr_get(fde->fde_url))) {
      playqueue_load_with_source(fde->fde_prop, s->s_root, 0);
      free(s->s_playme);
      s->s_playme = NULL;
      return;
    }
  }
}


/**
 *
 */
static void
analyzer(scanner_t *s, int probe)
{
  fa_dir_entry_t *fde;

  /* Empty */
  if(s->s_fd->fd_count == 0)
    return;
  
  if(probe)
    tryplay(s);

  /* Scan all entries */
  TAILQ_FOREACH(fde, &s->s_fd->fd_entries, fde_link) {

    while(media_buffer_hungry && !s->s_stop) 
      sleep(1);

    if(s->s_stop)
      break;

    if(fde->fde_probestatus == FDE_PROBE_NONE) {
      if(fde->fde_type == CONTENT_FILE)
	fde->fde_type = type_from_filename(rstr_get(fde->fde_filename));

      fde->fde_probestatus = FDE_PROBE_FILENAME;
    }

    if(fde->fde_probestatus == FDE_PROBE_FILENAME && probe)
      deep_probe(fde, s);
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
  prop_nf_release(s->s_pnf);
  free(s->s_url);
  free(s);
}


/**
 *
 */
static int
scanner_checkstop(void *opaque)
{
  scanner_t *s = opaque;
  closedb(s);
  return !!s->s_stop;
}


/**
 *
 */
static int
scanner_entry_setup(scanner_t *s, fa_dir_entry_t *fde, const char *src)
{
  TRACE(TRACE_DEBUG, "FA", "%s: File %s added by %s",
	s->s_url, rstr_get(fde->fde_url), src);

  if(fde->fde_type == CONTENT_FILE)
    fde->fde_type = type_from_filename(rstr_get(fde->fde_filename));

  make_prop(fde);

  if(!prop_set_parent(fde->fde_prop, s->s_nodes))
    return 1; // OK
  
  prop_destroy(fde->fde_prop);
  fde->fde_prop = NULL;
  return 0;
}


/**
 *
 */
static void
scanner_entry_destroy(scanner_t *s, fa_dir_entry_t *fde, const char *src)
{
  TRACE(TRACE_DEBUG, "FA", "%s: File %s removed by %s",
	s->s_url, rstr_get(fde->fde_url), src);
  metadb_unparent_item(getdb(s), rstr_get(fde->fde_url));
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

  if(filename && filename[0] == '.')
    return; /* Skip all dot-filenames */

  switch(op) {
  case FA_NOTIFY_DEL:
    TAILQ_FOREACH(fde, &s->s_fd->fd_entries, fde_link)
      if(!strcmp(filename, rstr_get(fde->fde_filename)))
	break;

    if(fde != NULL)
      scanner_entry_destroy(s, fde, "notification");
    break;

  case FA_NOTIFY_ADD:
    scanner_entry_setup(s, fa_dir_add(s->s_fd, url, filename, type),
			"notification");
    break;

  case FA_NOTIFY_DIR_CHANGE:
    rescan(s);
    return;
  }
  analyzer(s, 1);
}


/**
 * Very simple and O^2 diff
 */
static void
rescan(scanner_t *s)
{
  fa_dir_t *fd;
  fa_dir_entry_t *a, *b, *n;
  int changed = 0;

  if((fd = fa_scandir(s->s_url, NULL, 0)) == NULL)
    return; 

  if(s->s_fd->fd_count != fd->fd_count) {
    TRACE(TRACE_DEBUG, "FA", "%s: Rescanning found %d items, previously %d",
	  s->s_url, fd->fd_count, s->s_fd->fd_count);
  }

  for(a = TAILQ_FIRST(&s->s_fd->fd_entries); a != NULL; a = n) {
    n = TAILQ_NEXT(a, fde_link);
    TAILQ_FOREACH(b, &fd->fd_entries, fde_link)
      if(!strcmp(rstr_get(a->fde_url), rstr_get(b->fde_url)))
	break;

    if(b != NULL) {
      // Exists in old and new set

      if(!fa_dir_entry_stat(b) && 
	 a->fde_stat.fs_mtime != b->fde_stat.fs_mtime) {
	// Modification time has changed,  trig deep probe
	a->fde_type = b->fde_type;
	a->fde_probestatus = FDE_PROBE_NONE;
	a->fde_stat = b->fde_stat;
	a->fde_ignore_cache = 1;
	changed = 1;
      }

      fa_dir_entry_free(fd, b);

    } else {
      changed = 1;
      // Exists in old but not in new
      scanner_entry_destroy(s, a, "rescan");
    }
  }

  while((b = TAILQ_FIRST(&fd->fd_entries)) != NULL) {
    TAILQ_REMOVE(&fd->fd_entries, b, fde_link);
    TAILQ_INSERT_TAIL(&s->s_fd->fd_entries, b, fde_link);
    s->s_fd->fd_count++;

    changed |= scanner_entry_setup(s, b, "rescan");
  }

  if(changed)
    analyzer(s, 1);

  fa_dir_free(fd);
}


/**
 *
 */
static void
doscan(scanner_t *s)
{
  fa_dir_entry_t *fde;
  prop_vec_t *pv;
  char errbuf[256];
  int pending_rescan = 0;

  s->s_fd = metadb_metadata_scandir(getdb(s), s->s_url, NULL);

  if(s->s_fd == NULL) {
    s->s_fd = fa_scandir(s->s_url, errbuf, sizeof(errbuf));
    if(s->s_fd != NULL)
      TRACE(TRACE_DEBUG, "FA", "%s: Found %d by directory scanning",
	    s->s_url, s->s_fd->fd_count);
  } else {
    TRACE(TRACE_DEBUG, "FA", "%s: Found %d items in cache",
	  s->s_url, s->s_fd->fd_count);
    pending_rescan = 1;
  }
  prop_set_int(s->s_loading, 0);

  if(s->s_fd != NULL) {

    analyzer(s, 0);

    pv = prop_vec_create(s->s_fd->fd_count);
    
    TAILQ_FOREACH(fde, &s->s_fd->fd_entries, fde_link) {
      make_prop(fde);
      pv = prop_vec_append(pv, fde->fde_prop);
    }
    
    prop_set_parent_vector(pv, s->s_nodes, NULL, NULL);
    prop_vec_release(pv);

    analyzer(s, 1);

  } else {
    TRACE(TRACE_INFO, "scanner",
	  "Unable to scan %s -- %s -- Retrying in background",
	  s->s_url, errbuf);
    s->s_fd = fa_dir_alloc();
  }

  if(pending_rescan)
    rescan(s);

  closedb(s);


  if(fa_notify(s->s_url, s, scanner_notification, scanner_checkstop)) {
    prop_set_int(s->s_direct_close, 1);
  }
  fa_dir_free(s->s_fd);
}

/**
 *
 */
static void *
scanner(void *aux)
{
  scanner_t *s = aux;

  doscan(s);

  closedb(s);

  prop_set_int(s->s_loading, 0);

  free(s->s_playme);

  prop_ref_dec(s->s_root);
  prop_ref_dec(s->s_nodes);
  prop_ref_dec(s->s_loading);
  prop_ref_dec(s->s_direct_close);
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

  prop_unsubscribe(va_arg(ap, prop_sub_t *));

  s->s_stop = 1;
  scanner_unref(s);
}


static void
set_sort_order(void *opaque, prop_event_t event, ...)
{
  scanner_t *s = opaque;
  va_list ap;
  prop_t *p;

  va_start(ap, event);

  switch(event) {
  case PROP_DESTROYED:
    prop_unsubscribe(va_arg(ap, prop_sub_t *));
    scanner_unref(s);
    break;

  case PROP_SELECT_CHILD:
    p = va_arg(ap, prop_t *);
    rstr_t *r = prop_get_name(p);
    const char *val = rstr_get(r);
    if(val != NULL) {
      if(!strcmp(val, "title"))
	prop_nf_sort(s->s_pnf, "node.metadata.title", 0, 2, NULL, 1);
      if(!strcmp(val, "date"))
	prop_nf_sort(s->s_pnf, "node.metadata.timestamp", 0, 2, NULL, 0);
    }
    kv_url_opt_set(s->s_url, KVSTORE_PAGE_DOMAIN_SYS, "sortorder", 
		   KVSTORE_SET_STRING, val);
    rstr_release(r);
    break;

  default:
    break;
  }
}



/**
 *
 */
static void
add_sort_option_type(scanner_t *s, prop_t *model)
{
  prop_t *parent = prop_create(model, "options");
  prop_t *n = prop_create_root(NULL);
  prop_t *m = prop_create(n, "metadata");
  prop_t *options = prop_create(n, "options");

  prop_set_string(prop_create(n, "type"), "multiopt");
  prop_set_int(prop_create(n, "enabled"), 1);
  prop_link(_p("Sort on"), prop_create(m, "title"));
  

  prop_t *on_title = prop_create_root("title");
  prop_link(_p("Filename"), prop_create(on_title, "title"));
  if(prop_set_parent(on_title, options))
    abort();

  prop_t *on_date = prop_create_root("date");
  prop_link(_p("Date"), prop_create(on_date, "title"));
  if(prop_set_parent(on_date, options))
    abort();

  rstr_t *cur = kv_url_opt_get_rstr(s->s_url, KVSTORE_PAGE_DOMAIN_SYS, 
				    "sortorder");

  if(cur != NULL && !strcmp(rstr_get(cur), "date")) {
    prop_select(on_date);
    prop_nf_sort(s->s_pnf, "node.metadata.timestamp", 0, 2, NULL, 0);
  } else {
    prop_select(on_title);
    prop_nf_sort(s->s_pnf, "node.metadata.title", 0, 2, NULL, 0);
  }

  s->s_refcount++;
  prop_subscribe(PROP_SUB_NO_INITIAL_UPDATE | PROP_SUB_TRACK_DESTROY,
		 PROP_TAG_CALLBACK, set_sort_order, s,
		 PROP_TAG_ROOT, options,
		 NULL);

  if(prop_set_parent(n, parent))
    prop_destroy(n);
}


/**
 *
 */
static const prop_nf_sort_strmap_t typemap[] = {
  { "directory", 0},
  { NULL, 1}
};



static void
set_sort_dirs(void *opaque, prop_event_t event, ...)
{
  scanner_t *s = opaque;
  va_list ap;
  int val;

  va_start(ap, event);

  switch(event) {
  case PROP_DESTROYED:
    prop_unsubscribe(va_arg(ap, prop_sub_t *));
    scanner_unref(s);
    break;
    
  case PROP_SET_INT:
    val = va_arg(ap, int);
    prop_nf_sort(s->s_pnf, val ? "node.type" : NULL, 0, 0, typemap, 1);
    kv_url_opt_set(s->s_url, KVSTORE_PAGE_DOMAIN_SYS, "dirsfirst",
		   KVSTORE_SET_INT, val);
     break;

  default:
    break;
  }
}


/**
 *
 */
static void
add_sort_option_dirfirst(scanner_t *s, prop_t *model)
{
  prop_t *parent = prop_create(model, "options");
  prop_t *n = prop_create_root(NULL);
  prop_t *m = prop_create(n, "metadata");
  prop_t *value = prop_create(n, "value");
  int v = kv_url_opt_get_int(s->s_url, KVSTORE_PAGE_DOMAIN_SYS, "dirsfirst", 1);

  prop_set_string(prop_create(n, "type"), "bool");
  prop_set_int(prop_create(n, "enabled"), 1);
  prop_set_int(value, v);

  prop_link(_p("Sort folders first"), prop_create(m, "title"));

  prop_nf_sort(s->s_pnf, v ? "node.type" : NULL, 0, 0, typemap, 1);

  s->s_refcount++;
  prop_subscribe(PROP_SUB_NO_INITIAL_UPDATE | PROP_SUB_TRACK_DESTROY,
		 PROP_TAG_CALLBACK, set_sort_dirs, s,
		 PROP_TAG_ROOT, value,
		 NULL);

  if(prop_set_parent(n, parent))
    prop_destroy(n);
}


/**
 *
 */
void
fa_scanner(const char *url, time_t url_mtime, 
	   prop_t *model, const char *playme,
	   prop_t *direct_close)
{
  scanner_t *s = calloc(1, sizeof(scanner_t));
  s->s_url = strdup(url);

  prop_t *source = prop_create(model, "source");

  struct prop_nf *pnf;

  pnf = prop_nf_create(prop_create(model, "nodes"),
		       source,
		       prop_create(model, "filter"),
		       PROP_NF_AUTODESTROY);
  


  prop_nf_pred_str_add(pnf, "node.type",
		       PROP_NF_CMP_EQ, "unknown", NULL, 
		       PROP_NF_MODE_EXCLUDE);

  prop_nf_pred_int_add(pnf, "node.hidden",
		       PROP_NF_CMP_EQ, 1, NULL, 
		       PROP_NF_MODE_EXCLUDE);

  prop_set_int(prop_create(model, "canFilter"), 1);

  s->s_pnf = prop_nf_retain(pnf);

  add_sort_option_type(s, model);
  add_sort_option_dirfirst(s, model);
  decorated_browse_create(model, pnf);

  s->s_mtime = url_mtime;
  s->s_playme = playme != NULL ? strdup(playme) : NULL;

  prop_set_int(prop_create(model, "loading"), 1);

  s->s_root = prop_ref_inc(model);
  s->s_nodes = prop_ref_inc(source);
  s->s_loading = prop_ref_inc(prop_create(model, "loading"));
  s->s_direct_close = prop_ref_inc(direct_close);

  s->s_refcount += 2; // One held by scanner thread, one by the subscription

  s->s_ref = fa_reference(s->s_url);

  hts_thread_create_detached("fa scanner", scanner, s, THREAD_PRIO_LOW);

  prop_subscribe(PROP_SUB_TRACK_DESTROY,
		 PROP_TAG_CALLBACK, scanner_stop, s,
		 PROP_TAG_ROOT, s->s_root,
		 NULL);
}
