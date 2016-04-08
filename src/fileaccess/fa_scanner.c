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
// This should be renamed to fa_browser to better reflect what it is

#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include "main.h"
#include "navigator.h"
#include "fileaccess.h"
#include "fa_probe.h"
#include "playqueue.h"
#include "misc/strtab.h"
#include "prop/prop_nodefilter.h"
#include "prop/prop_linkselected.h"
#include "plugins.h"
#include "text/text.h"
#include "db/kvstore.h"
#include "fa_indexer.h"
#include "notifications.h"
#include "metadata/playinfo.h"
#include "metadata/metadata_str.h"

#define SCAN_TRACE(x, ...) do {                                \
    if(gconf.enable_fa_scanner_debug)                          \
      TRACE(TRACE_DEBUG, "FA", x, ##__VA_ARGS__);              \
  } while(0)


extern int media_buffer_hungry;

typedef enum {
  BROWSER_STOP,
  BROWSER_DIR,
  BROWSER_INDEX_ALBUMS,
  BROWSER_INDEX_ARTISTS,
} browser_mode_t;



typedef struct scanner {
  atomic_t s_refcount;

  char *s_url;
  time_t s_mtime; // modifiaction time of s_url
  rstr_t *s_playme;

  prop_t *s_nodes;
  prop_t *s_loading;
  prop_t *s_model;
  prop_t *s_direct_close;
  
  browser_mode_t s_mode;

  fa_dir_t *s_fd;

  void *s_ref;

  void *s_metadb;

  struct prop_nf *s_pnf;

  rstr_t *s_title;

  browser_mode_t s_checkstop_cmp;
  deco_browse_t *s_db;

  prop_courier_t *s_pc;

} scanner_t;


static int rescan(scanner_t *s);
static void browse_as_dir(scanner_t *s);


/**
 *
 */
static void
set_mode(scanner_t *s, browser_mode_t mode)
{
  s->s_mode = mode;
  fa_indexer_enable(s->s_url, mode != BROWSER_DIR);
}




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
    if(fde->fde_type == CONTENT_DIR ||
       fde->fde_type == CONTENT_SHARE) {
      title = rstr_dup(fde->fde_filename);
    } else {
      title = metadata_remove_postfix_rstr(fde->fde_filename);
    }
    
    metadata = prop_create(p, "metadata");
    prop_set_rstring(prop_create(metadata, "title"), title);
    rstr_release(title);
  }


  if(fde->fde_statdone)
    prop_set(metadata, "timestamp", PROP_SET_INT, fde->fde_stat.fs_mtime);

  assert(fde->fde_prop == NULL);
  fde->fde_prop = prop_ref_inc(p);

  prop_set(p, "canDelete", PROP_SET_INT, gconf.fa_allow_delete);

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
  { "wav",             CONTENT_AUDIO },

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

  { "ttf",             CONTENT_FONT },
  { "otf",             CONTENT_FONT },

  { "pdf",             CONTENT_DOCUMENT },

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
  { "db",              CONTENT_UNKNOWN },
  { "pkg",             CONTENT_UNKNOWN },
  { "elf",             CONTENT_UNKNOWN },
  { "self",            CONTENT_UNKNOWN },
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
  if(fde->fde_type == CONTENT_SHARE)
    return;

  fde->fde_probestatus = FDE_PROBED_CONTENTS;

  SCAN_TRACE("Deep probing %s -- content_type:%s",
             rstr_get(fde->fde_url), content2type(fde->fde_type));

  if(fde->fde_type != CONTENT_UNKNOWN) {

    prop_t *meta = prop_create_r(fde->fde_prop, "metadata");

    if(!fde->fde_ignore_cache && !fa_dir_entry_stat(fde) &&
       (fde->fde_md == NULL || !fde->fde_md->md_cache_status)) {

      if(fde->fde_md != NULL)
	metadata_destroy(fde->fde_md);

      fde->fde_md = metadb_metadata_get(getdb(s), rstr_get(fde->fde_url),
					fde->fde_stat.fs_mtime);
      SCAN_TRACE("%s: Metadata %sfound", rstr_get(fde->fde_url),
                 fde->fde_md ? "" : "not ");
    }

    if(fde->fde_statdone && meta != NULL)
      prop_set(meta, "timestamp", PROP_SET_INT, fde->fde_stat.fs_mtime);

    metadata_index_status_t is = INDEX_STATUS_NIL;

    if(fde->fde_md == NULL) {

      if(fde->fde_type == CONTENT_DIR) {
        fde->fde_md = fa_probe_dir(rstr_get(fde->fde_url));
      } else {
	fde->fde_md = fa_probe_metadata(rstr_get(fde->fde_url), NULL, 0,
					rstr_get(fde->fde_filename), NULL);
        is = INDEX_STATUS_FILE_ANALYZED;
      }
    }
    
    if(fde->fde_md != NULL) {
      fde->fde_type = fde->fde_md->md_contenttype;
      fde->fde_ignore_cache = 0;

      if(meta != NULL) {
        switch(fde->fde_type) {
#if ENABLE_PLUGINS
        case CONTENT_PLUGIN:
          plugin_props_from_file(fde->fde_prop, rstr_get(fde->fde_url));
          break;
#endif
        case CONTENT_FONT:
          fontstash_props_from_title(fde->fde_prop, rstr_get(fde->fde_url),
                                     rstr_get(fde->fde_filename));
          break;
          
        default:
          metadata_to_proptree(fde->fde_md, meta, 1);
          break;
        }
      }
      SCAN_TRACE("%s: Cache status: %d",
                 rstr_get(fde->fde_url), fde->fde_md->md_cache_status);

      switch(fde->fde_md->md_cache_status) {
      case METADATA_CACHE_STATUS_NO:
        SCAN_TRACE("Storing item %s in DB parent:%s mtime:%d",
                   rstr_get(fde->fde_url), s->s_url,
                   (int)fde->fde_stat.fs_mtime);
	metadb_metadata_write(getdb(s), rstr_get(fde->fde_url),
			      fde->fde_stat.fs_mtime,
			      fde->fde_md, s->s_url, s->s_mtime,
                              is);
	break;
      case METADATA_CACHE_STATUS_FULL:
	// All set
	break;
      case METADATA_CACHE_STATUS_UNPARENTED:
	// Reparent item
	metadb_parent_item(getdb(s), rstr_get(fde->fde_url), s->s_url);
	break;
      }
    }
    prop_ref_dec(meta);

    if(fde->fde_prop != NULL && !fde->fde_bound_to_metadb) {
      fde->fde_bound_to_metadb = 1;
      playinfo_bind_url_to_prop(rstr_get(fde->fde_url), fde->fde_prop);
    }
  }

  if(fde->fde_prop != NULL)
    set_type(fde->fde_prop, fde->fde_type);
}


/**
 *
 */
static void
tryplay(scanner_t *s)
{
  if(s->s_playme == NULL)
    return;

  fa_dir_entry_t *fde = fa_dir_find(s->s_fd, s->s_playme);
  if(fde != NULL) {
    playqueue_load_with_source(fde->fde_prop, s->s_model, 0);
    rstr_release(s->s_playme);
    s->s_playme = NULL;
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
  RB_FOREACH(fde, &s->s_fd->fd_entries, fde_link) {

    while(media_buffer_hungry && s->s_mode == BROWSER_DIR) 
      sleep(1);

    if(s->s_mode != BROWSER_DIR)
      break;

    if(fde->fde_probestatus == FDE_PROBED_NONE) {
      if(fde->fde_type == CONTENT_FILE)
	fde->fde_type = type_from_filename(rstr_get(fde->fde_filename));

      fde->fde_probestatus = FDE_PROBED_FILENAME;
    }

    if(fde->fde_probestatus == FDE_PROBED_FILENAME && probe)
      deep_probe(fde, s);
  }
}


/**
 *
 */
static scanner_t *
scanner_create(const char *url, time_t mtime)
{
  scanner_t *s = calloc(1, sizeof(scanner_t));
  s->s_pc = prop_courier_create_waitable();

  s->s_url = strdup(url);
  s->s_mode = BROWSER_DIR;
  s->s_mtime = mtime;
  return s;
}


/**
 *
 */
static void
scanner_destroy(scanner_t *s)
{
  closedb(s);
  free(s->s_url);
  prop_courier_destroy(s->s_pc);
  free(s);
}

/**
 *
 */
static void
scanner_release(scanner_t *s)
{
  if(atomic_dec(&s->s_refcount))
    return;
  fa_unreference(s->s_ref);
  if(s->s_pnf != NULL)
    prop_nf_release(s->s_pnf);
  scanner_destroy(s);
}


/**
 *
 */
static int
scanner_entry_setup(scanner_t *s, fa_dir_entry_t *fde, const char *src)
{
  SCAN_TRACE("%s: File %s added by %s",
             s->s_url, rstr_get(fde->fde_url), src);

  if(fde->fde_type == CONTENT_FILE)
    fde->fde_type = type_from_filename(rstr_get(fde->fde_filename));

  if(s->s_nodes == NULL)
    return 0;

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
  SCAN_TRACE("%s: File %s removed by %s",
             s->s_url, rstr_get(fde->fde_url), src);
  metadb_unparent_item(getdb(s), rstr_get(fde->fde_url));
  if(fde->fde_prop != NULL)
    prop_destroy(fde->fde_prop);
  fa_dir_entry_free(s->s_fd, fde);
}

#if 0
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
    RB_FOREACH(fde, &s->s_fd->fd_entries, fde_link)
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
#endif

/**
 *
 */
static int
rescan(scanner_t *s)
{
  fa_dir_t *fd;
  fa_dir_entry_t *a, *b, *n;
  int changed = 0;
  char errbuf[512];
  if((fd = fa_scandir(s->s_url, errbuf, sizeof(errbuf))) == NULL) {
    SCAN_TRACE("%s: Rescanning failed: %s", s->s_url, errbuf);
    return -1; 
  }

  if(s->s_fd->fd_count != fd->fd_count) {
    SCAN_TRACE("%s: Rescanning found %d items, previously %d",
               s->s_url, fd->fd_count, s->s_fd->fd_count);
  }

  for(a = RB_FIRST(&s->s_fd->fd_entries); a != NULL; a = n) {
    n = RB_NEXT(a, fde_link);

    b = fa_dir_find(fd, a->fde_url);
    if(b != NULL) {

      // Exists in old and new set

      if(b->fde_type == CONTENT_SHARE) {
        a->fde_type = b->fde_type;
        fa_dir_entry_free(fd, b);
        if(a->fde_prop != NULL)
          set_type(a->fde_prop, a->fde_type);
        continue;
      }


      if(!fa_dir_entry_stat(b) && 
	 a->fde_stat.fs_mtime != b->fde_stat.fs_mtime) {
	// Modification time has changed,  trig deep probe
	a->fde_type = b->fde_type;
	a->fde_probestatus = FDE_PROBED_NONE;
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

  while((b = fd->fd_entries.root) != NULL) {
    fa_dir_remove(fd, b);
    fa_dir_insert(s->s_fd, b);

    changed |= scanner_entry_setup(s, b, "rescan");
  }

  if(changed)
    analyzer(s, 1);

  fa_dir_free(fd);
  return 0;
}


/**
 *
 */
static int
doscan(scanner_t *s, int with_notify)
{
  fa_dir_entry_t *fde;
  char errbuf[256];
  int pending_rescan = 0;
  int err = 1;

  assert(s->s_fd == NULL);
  s->s_fd = metadb_metadata_scandir(getdb(s), s->s_url, NULL);

  if(s->s_fd == NULL) {
    s->s_fd = fa_scandir(s->s_url, errbuf, sizeof(errbuf));
    if(s->s_fd != NULL) {
      SCAN_TRACE("%s: Found %d by directory scanning",
              s->s_url, s->s_fd->fd_count);

      if(gconf.enable_fa_scanner_debug) {
        RB_FOREACH(fde, &s->s_fd->fd_entries, fde_link) {
          SCAN_TRACE("%s: File %s",
                     s->s_url, rstr_get(fde->fde_url));
        }
      }

      err = 0;
    }
  } else {
    SCAN_TRACE("%s: Found %d items in cache",
               s->s_url, s->s_fd->fd_count);
    pending_rescan = 1;
  }
  prop_set_int(s->s_loading, 0);

  if(s->s_fd != NULL) {

    analyzer(s, 0);

    if(s->s_nodes != NULL) {

      prop_vec_t *pv = prop_vec_create(s->s_fd->fd_count);
    
      RB_FOREACH(fde, &s->s_fd->fd_entries, fde_link) {
        make_prop(fde);
        pv = prop_vec_append(pv, fde->fde_prop);
      }
    
      prop_set_parent_vector(pv, s->s_nodes, NULL, NULL);
      prop_vec_release(pv);
    }
    analyzer(s, 1);

  } else {
    TRACE(TRACE_INFO, "scanner",
	  "Unable to scan %s -- %s -- Retrying in background",
	  s->s_url, errbuf);
    s->s_fd = fa_dir_alloc();
  }

  if(pending_rescan) {
    SCAN_TRACE("%s: Starting rescan", s->s_url);
    err = rescan(s);
    SCAN_TRACE("%s: Rescan completed: %d", s->s_url, err);
  }

  closedb(s);
#if 0
  fa_handle_t *n = fa_notify_start(s->s_url, s, scanner_notification);
#endif
  while(s->s_mode == BROWSER_DIR)
    prop_courier_wait_and_dispatch(s->s_pc);
#if 0
  if(n != NULL)
    fa_notify_stop(n);
#endif
  fa_dir_free(s->s_fd);
  return err;
}



/**
 *
 */
static void
cleanup_model(scanner_t *s)
{
  if(s->s_db != NULL) {
    decorated_browse_destroy(s->s_db);
    s->s_db = NULL;
  }
  prop_destroy_childs(s->s_nodes);
}


/**
 *
 */
static int
checkstop_index(void *opaque)
{
  scanner_t *s = opaque;
  prop_courier_poll(s->s_pc);
  return s->s_mode != s->s_checkstop_cmp;
}


/**
 *
 */
static void *
scanner_thread(void *aux)
{
  scanner_t *s = aux;

  browser_mode_t mode = -1;

  while(s->s_mode != BROWSER_STOP) {

    if(s->s_mode == mode) {
      prop_courier_wait_and_dispatch(s->s_pc);
      continue;
    }

    mode = s->s_mode;
    cleanup_model(s);

    s->s_checkstop_cmp = mode;

    prop_set_int(s->s_loading, 1);

    switch(mode) {
    case BROWSER_DIR:
      prop_set(s->s_model, "contents", PROP_SET_VOID);
      browse_as_dir(s);
      doscan(s, 1);
      break;

    case BROWSER_STOP:
      break;

    case BROWSER_INDEX_ALBUMS:
      prop_set(s->s_model, "contents", PROP_SET_STRING, "albums");
      metadata_browse(getdb(s), s->s_url, s->s_nodes, s->s_model,
                      LIBRARY_QUERY_ALBUMS, checkstop_index, s);
      break;

    case BROWSER_INDEX_ARTISTS:
      prop_set(s->s_model, "contents", PROP_SET_STRING, "artists");
      metadata_browse(getdb(s), s->s_url, s->s_nodes, s->s_model,
                      LIBRARY_QUERY_ARTISTS, checkstop_index, s);
      break;
    }
  }
  cleanup_model(s);

  closedb(s);

  free(s->s_playme);

  prop_ref_dec(s->s_model);
  prop_ref_dec(s->s_nodes);
  prop_ref_dec(s->s_loading);
  prop_ref_dec(s->s_direct_close);
  rstr_release(s->s_title);
  scanner_release(s);
  return NULL;
}


/**
 *
 */
static void
delete_items(scanner_t *s, prop_vec_t *pv)
{
  char errbuf[256];
  TRACE(TRACE_DEBUG, "FA", "About to delete %d items", prop_vec_len(pv));
  for(int i = 0, c = prop_vec_len(pv); i < c; i++) {
    fa_dir_entry_t *fde;
    RB_FOREACH(fde, &s->s_fd->fd_entries, fde_link) {
      if(fde->fde_prop == prop_vec_get(pv, i))
        break;
    }
    if(fde == NULL)
      continue;

    TRACE(TRACE_DEBUG, "FA", "About to delete %s",
          rstr_get(fde->fde_url));

    if(fa_unlink_recursive(rstr_get(fde->fde_url),
                           errbuf, sizeof(errbuf), 1) < 0) {
      notify_add(NULL, NOTIFY_ERROR, NULL, 5,
                 _("Unable to delete %s\n%s"),
                 rstr_get(fde->fde_filename), errbuf);
    } else {
      notify_add(NULL, NOTIFY_INFO, NULL, 5,
                 _("Deleted %s"), rstr_get(fde->fde_filename));
      scanner_entry_destroy(s, fde, "user");
    }
  }
}

/**
 *
 */
static void
scanner_nodes_callback(void *opaque, prop_event_t event, ...)
{
  scanner_t *s = opaque;
  va_list ap;

  va_start(ap, event);

  switch(event) {
  case PROP_DESTROYED:
    prop_unsubscribe(va_arg(ap, prop_sub_t *));
    s->s_mode = BROWSER_STOP;
    scanner_release(s);
    break;

  case PROP_REQ_DELETE_VECTOR:
    delete_items(s, va_arg(ap, prop_vec_t *));
    break;

  default:
    break;
  }
  va_end(ap);
}


/**
 *
 */
static void
set_indexed_mode(void *opaque, prop_event_t event, ...)
{
  scanner_t *s = opaque;
  va_list ap;
  prop_t *p;

  va_start(ap, event);

  switch(event) {
  case PROP_DESTROYED:
    prop_unsubscribe(va_arg(ap, prop_sub_t *));
    scanner_release(s);
    break;

  case PROP_SELECT_CHILD:
    p = va_arg(ap, prop_t *);
    rstr_t *r = prop_get_name(p);
    const char *val = rstr_get(r);
    if(val != NULL) {
      if(!strcmp(val, "off"))
        set_mode(s, BROWSER_DIR);
      if(!strcmp(val, "albums"))
        set_mode(s, BROWSER_INDEX_ALBUMS);
      if(!strcmp(val, "artists"))
        set_mode(s, BROWSER_INDEX_ARTISTS);
    }
    kv_url_opt_set(s->s_url, KVSTORE_DOMAIN_SYS, "indexedmode",
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
add_indexed_option(scanner_t *s, prop_t *model)
{
  prop_t *parent = prop_create(model, "options");
  prop_t *n = prop_create_root(NULL);
  prop_t *m = prop_create(n, "metadata");
  prop_t *options = prop_create(n, "options");

  prop_set_string(prop_create(n, "type"), "multiopt");
  prop_set_int(prop_create(n, "enabled"), 1);
  prop_link(_p("Indexed mode"), prop_create(m, "title"));
  
  prop_t *off = prop_create_root("off");
  prop_link(_p("Off"), prop_create(off, "title"));
  if(prop_set_parent(off, options))
    abort();

  prop_t *albums = prop_create_root("albums");
  prop_link(_p("Albums"), prop_create(albums, "title"));
  if(prop_set_parent(albums, options))
    abort();

  prop_t *artists = prop_create_root("artists");
  prop_link(_p("Artists"), prop_create(artists, "title"));
  if(prop_set_parent(artists, options))
    abort();

  rstr_t *cur = kv_url_opt_get_rstr(s->s_url, KVSTORE_DOMAIN_SYS, 
				    "indexedmode");

  if(cur != NULL && !strcmp(rstr_get(cur), "albums")) {
    prop_select(albums);
    set_mode(s, BROWSER_INDEX_ALBUMS);
  } else if(cur != NULL && !strcmp(rstr_get(cur), "artists")) {
    prop_select(artists);
    set_mode(s, BROWSER_INDEX_ARTISTS);
  } else {
    prop_select(off);
    set_mode(s, BROWSER_DIR);
  }
  rstr_release(cur);
  atomic_inc(&s->s_refcount);

  prop_linkselected_create(options, n, "current", "value");

  prop_subscribe(PROP_SUB_NO_INITIAL_UPDATE | PROP_SUB_TRACK_DESTROY,
                 PROP_TAG_CALLBACK, set_indexed_mode, s,
                 PROP_TAG_ROOT, options,
                 PROP_TAG_COURIER, s->s_pc,
                 NULL);

  if(prop_set_parent(n, parent))
    prop_destroy(n);
}


/**
 *
 */
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
    scanner_release(s);
    break;

  case PROP_SELECT_CHILD:
    p = va_arg(ap, prop_t *);
    rstr_t *r = prop_get_name(p);
    const char *val = rstr_get(r);
    if(val != NULL) {
      if(!strcmp(val, "title"))
	prop_nf_sort(s->s_pnf, "node.metadata.title", 0, 3, NULL, 1);
      else if(!strcmp(val, "date"))
	prop_nf_sort(s->s_pnf, "node.metadata.timestamp", 1, 3, NULL, 0);
      else if(!strcmp(val, "dateold"))
	prop_nf_sort(s->s_pnf, "node.metadata.timestamp", 0, 3, NULL, 0);
    }
    kv_url_opt_set(s->s_url, KVSTORE_DOMAIN_SYS, "sortorder", 
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
  prop_link(_p("Newest first"), prop_create(on_date, "title"));
  if(prop_set_parent(on_date, options))
    abort();

  prop_t *on_dateold = prop_create_root("dateold");
  prop_link(_p("Oldest first"), prop_create(on_dateold, "title"));
  if(prop_set_parent(on_dateold, options))
    abort();

  rstr_t *cur = kv_url_opt_get_rstr(s->s_url, KVSTORE_DOMAIN_SYS, 
				    "sortorder");

  if(cur != NULL && !strcmp(rstr_get(cur), "date")) {
    prop_select(on_date);
    prop_nf_sort(s->s_pnf, "node.metadata.timestamp", 1, 3, NULL, 0);
  } else if(cur != NULL && !strcmp(rstr_get(cur), "dateold")) {
    prop_select(on_date);
    prop_nf_sort(s->s_pnf, "node.metadata.timestamp", 0, 3, NULL, 0);
  } else {
    prop_select(on_title);
    prop_nf_sort(s->s_pnf, "node.metadata.title", 0, 3, NULL, 1);
  }

  prop_linkselected_create(options, n, "current", "value");

  rstr_release(cur);
  atomic_inc(&s->s_refcount);
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


/**
 *
 */
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
    scanner_release(s);
    break;
    
  case PROP_SET_INT:
    val = va_arg(ap, int);
    prop_nf_sort(s->s_pnf, val ? "node.type" : NULL, 0, 0, typemap, 1);
    kv_url_opt_set(s->s_url, KVSTORE_DOMAIN_SYS, "dirsfirst",
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
  int v = kv_url_opt_get_int(s->s_url, KVSTORE_DOMAIN_SYS, "dirsfirst", 1);

  prop_set_string(prop_create(n, "type"), "bool");
  prop_set_int(prop_create(n, "enabled"), 1);
  prop_set_int(value, v);

  prop_link(_p("Sort folders first"), prop_create(m, "title"));

  prop_nf_sort(s->s_pnf, v ? "node.type" : NULL, 0, 0, typemap, 1);

  atomic_inc(&s->s_refcount);
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
static void
set_only_supported_files(void *opaque, prop_event_t event, ...)
{
  scanner_t *s = opaque;
  va_list ap;
  int val;

  va_start(ap, event);

  switch(event) {
  case PROP_DESTROYED:
    prop_unsubscribe(va_arg(ap, prop_sub_t *));
    scanner_release(s);
    break;
    
  case PROP_SET_INT:
    val = va_arg(ap, int);
    kv_url_opt_set(s->s_url, KVSTORE_DOMAIN_SYS, "supportedfiles",
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
add_only_supported_files(scanner_t *s, prop_t *model,
			 prop_t **valp)
{
  prop_t *parent = prop_create(model, "options");
  prop_t *n = prop_create_root(NULL);
  prop_t *m = prop_create(n, "metadata");
  prop_t *value = *valp = prop_create(n, "value");
  int v = kv_url_opt_get_int(s->s_url, KVSTORE_DOMAIN_SYS,
			     "supportedfiles", 1);

  prop_set_string(prop_create(n, "type"), "bool");
  prop_set_int(prop_create(n, "enabled"), 1);
  prop_set_int(value, v);

  prop_link(_p("Show only supported files"), prop_create(m, "title"));

  atomic_inc(&s->s_refcount);
  prop_subscribe(PROP_SUB_NO_INITIAL_UPDATE | PROP_SUB_TRACK_DESTROY,
		 PROP_TAG_CALLBACK, set_only_supported_files, s,
		 PROP_TAG_ROOT, value,
		 NULL);

  if(prop_set_parent(n, parent)) {
    prop_destroy(n);
    *valp = NULL;
  }
}


/**
 *
 */
static void
browse_as_dir(scanner_t *s)
{
  s->s_db =
    decorated_browse_create(s->s_model, s->s_pnf, s->s_nodes, s->s_title,
                            DECO_FLAGS_RAW_FILENAMES |
                            DECO_FLAGS_NO_AUTO_DESTROY, 
                            s->s_url, "Directory");
}

/**
 *
 */
void
fa_scanner_page(const char *url, time_t url_mtime, 
                prop_t *model, const char *playme,
                prop_t *direct_close, rstr_t *title)
{
  scanner_t *s = scanner_create(url, url_mtime);

  /* One reference to the scanner thread
     and one to the scanner_stop subscription
  */
  atomic_set(&s->s_refcount, 2);

  s->s_playme = rstr_alloc(playme);

  s->s_nodes = prop_create_r(model, "source");
  s->s_loading = prop_create_r(model, "loading");

  s->s_model = prop_ref_inc(model);
  s->s_direct_close = prop_ref_inc(direct_close);
  s->s_title = rstr_dup(title);

  if(gconf.enable_experimental)
    add_indexed_option(s, model);

  prop_t *onlysupported;

  s->s_pnf = prop_nf_create(prop_create(s->s_model, "nodes"),
                            s->s_nodes,
                            prop_create(s->s_model, "filter"),
                            PROP_NF_AUTODESTROY);

  prop_set(s->s_model, "canFilter", PROP_SET_INT, 1);

  prop_nf_pred_int_add(s->s_pnf, "node.hidden",
		       PROP_NF_CMP_EQ, 1, NULL, 
		       PROP_NF_MODE_EXCLUDE);

  add_sort_option_type(s, s->s_model);
  add_sort_option_dirfirst(s, s->s_model);

  add_only_supported_files(s, s->s_model, &onlysupported);
  prop_nf_pred_str_add(s->s_pnf, "node.type",
		       PROP_NF_CMP_EQ, "unknown", onlysupported, 
		       PROP_NF_MODE_EXCLUDE);

  prop_nf_pred_str_add(s->s_pnf, "node.type",
		       PROP_NF_CMP_EQ, "file", onlysupported, 
		       PROP_NF_MODE_EXCLUDE);


  s->s_ref = fa_reference(s->s_url);

  prop_subscribe(PROP_SUB_TRACK_DESTROY,
                 PROP_TAG_CALLBACK, scanner_nodes_callback, s,
                 PROP_TAG_ROOT, s->s_nodes,
                 PROP_TAG_COURIER, s->s_pc,
                 NULL);

  scanner_thread(s);
}


/**
 *
 */
int
fa_scanner_scan(const char *url, time_t mtime)
{
  scanner_t *s = scanner_create(url, mtime);
  int r = doscan(s, 0);
  scanner_destroy(s);
  return r;
}
