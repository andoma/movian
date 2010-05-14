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

static int do_album_view = 0;

typedef struct scanner {
  int s_refcount;

  char *s_url;
  char *s_playme;

  prop_t *s_nodes;
  prop_t *s_viewprop;
  prop_t *s_root;

  int s_stop;

  fa_dir_t *s_fd;

  void *s_ref;

} scanner_t;






/**
 *
 */
const char *type2str[] = {
  [CONTENT_DIR]      = "directory",
  [CONTENT_FILE]     = "file",
  [CONTENT_AUDIO]    = "audio",
  [CONTENT_ARCHIVE]  = "archive",
  [CONTENT_VIDEO]    = "video",
  [CONTENT_PLAYLIST] = "playlist",
  [CONTENT_DVD]      = "dvd",
  [CONTENT_IMAGE]    = "image",
};


/**
 *
 */
static void
set_type(prop_t *proproot, unsigned int type)
{
  if(type < sizeof(type2str) / sizeof(type2str[0]) && type2str[type] != NULL)
    prop_set_string(prop_create(proproot, "type"), type2str[type]);
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
add_prop(fa_dir_entry_t *fde, prop_t *root, fa_dir_entry_t *before)
{
  prop_t *p = prop_create(NULL, "node");
  rstr_t *fname;

  if(fde->fde_type == CONTENT_DIR) {
    fname = rstr_alloc(fde->fde_filename);
  } else {
    fname = make_filename(fde->fde_filename);
  }

  prop_set_string(prop_create(p, "url"), fde->fde_url);
  set_type(p, fde->fde_type);

  prop_set_rstring(prop_create(p, "filename"), fname);

  prop_set_rstring(prop_create(prop_create(p, "metadata"), "title"), fname);

  rstr_release(fname);

  if(prop_set_parent_ex(p, root, before ? before->fde_prop : NULL, NULL)) {
    prop_destroy(p);
  } else {
    fde->fde_prop = p;
    prop_ref_inc(p);
  }
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

  { "pdf",             CONTENT_UNKNOWN },
};


/**
 *
 */
static void
quick_analyzer(fa_dir_t *fd, prop_t *viewprop)
{
  fa_dir_entry_t *fde;
  int type;
  const char *str;
  int images = 0;

  TAILQ_FOREACH(fde, &fd->fd_entries, fde_link) {

    if(fde->fde_probestatus != FDE_PROBE_NONE)
      continue;

    if(fde->fde_type == CONTENT_DIR)
      continue;
    
    if((str = strrchr(fde->fde_filename, '.')) == NULL)
      continue;
    str++;
    
    if((type = str2val(str, postfixtab)) == -1)
      continue;

    fde->fde_type = type;
    fde->fde_probestatus = FDE_PROBE_FILENAME;

    if(type == CONTENT_IMAGE)
      images++;
  }

  if(images * 4 > fd->fd_count * 3)
    prop_set_string(viewprop, "images");
  else
    prop_set_string(viewprop, "list");
}


/**
 *
 */
static void
deep_analyzer(fa_dir_t *fd, prop_t *viewprop, prop_t *root, int *stop)
{
  int type;
  prop_t *metadata, *p;

  int album_score = 0;
  int different_artists = 0;
  int images = 0;
  int unknown = 0;
  char album_name[128] = {0};
  char artist_name[128] = {0};
  char album_art[1024] = {0};
  int64_t album_art_score = 0;  // Bigger is better
  char buf[URL_MAX];
  int trackidx;
  fa_dir_entry_t *fde;

  /* Empty */
  if(fd->fd_count == 0) {
    prop_set_string(viewprop, "empty");
    return;
  }

  /* Scan all entries */
  TAILQ_FOREACH(fde, &fd->fd_entries, fde_link) {

    if(fde->fde_probestatus == FDE_PROBE_DEEP)
      continue;

    fde->fde_probestatus = FDE_PROBE_DEEP;

    metadata = fde->fde_prop ? prop_create(fde->fde_prop, "metadata") : NULL;

    if(metadata != NULL) {

      if(stop && *stop)
	return;

      type = fde->fde_type;
    
      if(fde->fde_type == CONTENT_DIR) {
	type = fa_probe_dir(metadata, fde->fde_url);
      } else {
	type = fa_probe(metadata, fde->fde_url, NULL, 0, buf, sizeof(buf),
			fde->fde_statdone ? &fde->fde_stat : NULL);

	if(type == CONTENT_UNKNOWN)
	  TRACE(TRACE_DEBUG, "BROWSE",
		"File \"%s\" not recognized: %s", fde->fde_url, buf);
      }
      set_type(fde->fde_prop, type);
      fde->fde_type = type;
    }

    switch(fde->fde_type) {

    case CONTENT_IMAGE:
      images++;

      if(!strncasecmp(fde->fde_filename, "albumart", 8) ||
	 !strncasecmp(fde->fde_filename, "folder.", 7)) {

	if(fde->fde_statdone || 
	   (metadata != NULL && !fa_dir_entry_stat(fde))) {
	  album_art_score = fde->fde_stat.st_size;
	  snprintf(album_art, sizeof(album_art), "%s", fde->fde_url);
	}
      }
      break;

    case CONTENT_UNKNOWN:
      unknown++;
      prop_destroy(fde->fde_prop);
      prop_ref_dec(fde->fde_prop);
      fde->fde_prop = NULL;
      break;
      
    case CONTENT_AUDIO:
      if(metadata == NULL)
	break;

      if((p = prop_get_by_names(metadata, "album", NULL)) == NULL ||
	 prop_get_string(p, buf, sizeof(buf))) {
	album_score--;
	break;
      }

      if(album_name[0] == 0) {
	snprintf(album_name, sizeof(album_name), "%s", buf);
	album_score++;
      } else if(!strcasecmp(album_name, buf)) {
	album_score++;
      } else {
	album_score--;
	break;
      }
      
      if((p = prop_get_by_names(metadata, "artist", NULL)) == NULL ||
	 prop_get_string(p, buf, sizeof(buf)))
	break;

      if(strstr(artist_name, buf))
	break;

      different_artists++;
      snprintf(artist_name + strlen(artist_name),
	       sizeof(artist_name) - strlen(artist_name),
	       "%s%s", artist_name[0] ? ", " : "", buf);
      break;

    default:
      album_score = INT32_MIN;
      break;

    }
  }

  if(do_album_view && album_score > 0 && 
     (different_artists < 2 || different_artists < album_score / 2)) {
      
    /* It is an album */
    prop_set_string(viewprop, "album");

    if(root != NULL) {
      prop_set_string(prop_create(root, "album_name"), album_name);

      if(artist_name[0])
	prop_set_string(prop_create(root, "artist_name"), artist_name);
  
      if(album_art[0])
	prop_set_string(prop_create(root, "album_art"), album_art);
    }

    trackidx = 1;

    /* Remove everything that is not audio */
    TAILQ_FOREACH(fde, &fd->fd_entries, fde_link) {
      if(fde->fde_type != CONTENT_AUDIO) {
	if(fde->fde_prop != NULL) {
	  prop_destroy(fde->fde_prop);
	  prop_ref_dec(fde->fde_prop);
	  fde->fde_prop = NULL;
	}

      } else {
	metadata = prop_create(fde->fde_prop, "metadata");
	prop_set_int(prop_create(metadata, "trackindex"), trackidx);
	trackidx++;

	if(album_art[0])
	  prop_set_string(prop_create(metadata, "album_art"), 
			  album_art);
      }
    }
  } else if(fd->fd_count == unknown) {
    prop_set_string(viewprop, "empty");
  } else if(images * 4 > fd->fd_count * 3) {
    prop_set_string(viewprop, "images");
  } else {
    prop_set_string(viewprop, "list");
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
  prop_t *metadata;
  int r;

  add_prop(fde, s->s_nodes, TAILQ_NEXT(fde, fde_link));
    
  metadata = prop_create(fde->fde_prop, "metadata");

  if(fde->fde_type == CONTENT_DIR) {
    r = fa_probe_dir(metadata, fde->fde_url);
  } else {
    r = fa_probe(metadata, fde->fde_url, NULL, 0, NULL, 0,
		 fde->fde_statdone ? &fde->fde_stat : NULL);
  }

  set_type(fde->fde_prop, r);
  fde->fde_type = r;
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
    scanner_entry_setup(s, fa_dir_insert(s->s_fd, url, filename, type));
    break;
  }
  deep_analyzer(s->s_fd, s->s_viewprop, s->s_root, &s->s_stop);
}


/**
 * Very simple and naive diff
 */
static void
rescan(scanner_t *s)
{
  fa_dir_t *fd2;
  fa_dir_entry_t *a, *b, *x, *y;
  int change = 0;

  if((fd2 = fa_scandir(s->s_url, NULL, 0)) == NULL)
    return; 

  fa_dir_sort(fd2);

  a = TAILQ_FIRST(&s->s_fd->fd_entries);
  x = TAILQ_FIRST(&fd2->fd_entries);
  
  while(a != NULL && x != NULL) {
    
    if(!strcmp(a->fde_url, x->fde_url)) {
      a = TAILQ_NEXT(a, fde_link);
      x = TAILQ_NEXT(x, fde_link);
      continue;
    }
    
    b =     TAILQ_NEXT(a, fde_link);
    y =     TAILQ_NEXT(x, fde_link);

    if(y != NULL && !strcmp(a->fde_url, y->fde_url)) {
      TAILQ_REMOVE(&fd2->fd_entries, x, fde_link);
      TAILQ_INSERT_BEFORE(a, x, fde_link);
      s->s_fd->fd_count++;
      scanner_entry_setup(s, x);
      change = 1;
      a = TAILQ_NEXT(a, fde_link);
      x = TAILQ_NEXT(y, fde_link);
      continue;
    }

    if(b != NULL && !strcmp(b->fde_url, x->fde_url)) {
      scanner_entry_destroy(s, a);
      change = 1;
      a = b;
      continue;
    }

    a = b;
    x = y;
  }

  for(; x != NULL; x = y) {
    y = TAILQ_NEXT(x, fde_link);
    TAILQ_REMOVE(&fd2->fd_entries, x, fde_link);
    TAILQ_INSERT_TAIL(&s->s_fd->fd_entries, x, fde_link);
    s->s_fd->fd_count++;
    scanner_entry_setup(s, x);
    change = 1;
  }

  for(; a != NULL; a = b) {
    b = TAILQ_NEXT(a, fde_link);
    scanner_entry_destroy(s, a);
    change = 1;
  }

  fa_dir_free(fd2);
  fa_dir_sort(s->s_fd);

  if(change)
    deep_analyzer(s->s_fd, s->s_viewprop, s->s_root, &s->s_stop);
}


/**
 *
 */
static void
doscan(scanner_t *s)
{
  fa_dir_entry_t *fde;
  fa_dir_t *fd = s->s_fd;

  fa_dir_sort(s->s_fd);

  quick_analyzer(s->s_fd, s->s_viewprop);

  TAILQ_FOREACH(fde, &fd->fd_entries, fde_link) {
    if(s->s_stop)
      return;
    add_prop(fde, s->s_nodes, NULL);
    
    if(s->s_playme != NULL &&
       !strcmp(s->s_playme, fde->fde_url)) {
      playqueue_load_with_source(fde->fde_prop, s->s_root, 0);
      free(s->s_playme);
      s->s_playme = NULL;
    }
  }

  deep_analyzer(s->s_fd, s->s_viewprop, s->s_root, &s->s_stop);

  if(!fa_notify(s->s_url, s, scanner_notification, scanner_checkstop))
    return;
  
  /* Can not do notifcations */

#ifdef WII
  // We don't want to keep threads running on wii 
  return;
#endif

  while(!s->s_stop) {
    sleep(3);
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

  s->s_ref = fa_reference(s->s_url);
  
  if((s->s_fd = fa_scandir(s->s_url, NULL, 0)) != NULL) {
    doscan(s);
    fa_dir_free(s->s_fd);
  }

  free(s->s_url);
  free(s->s_playme);

  prop_ref_dec(s->s_root);
  prop_ref_dec(s->s_nodes);

  if(s->s_viewprop != NULL)
    prop_ref_dec(s->s_viewprop);

  scanner_unref(s);
  return NULL;
}


/**
 *
 */
static void
scanner_stop(void *opaque, prop_event_t event, ...)
{
  prop_t *p;
  scanner_t *s = opaque;

  va_list ap;
  va_start(ap, event);

  if(event != PROP_DESTROYED) 
    return;

  p = va_arg(ap, prop_t *);
  prop_unsubscribe(va_arg(ap, prop_sub_t *));

  s->s_stop = 1;
  scanner_unref(s);
}


/**
 *
 */
void
fa_scanner(const char *url, prop_t *source, prop_t *view, const char *playme)
{
  scanner_t *s = calloc(1, sizeof(scanner_t));

  s->s_url = strdup(url);
  s->s_playme = playme != NULL ? strdup(playme) : NULL;

  s->s_root = source;
  prop_ref_inc(s->s_root);

  s->s_nodes = prop_create(source, "nodes");
  prop_ref_inc(s->s_nodes);

  if(view != NULL) {
    s->s_viewprop = view;
    prop_ref_inc(s->s_viewprop);
  }

  s->s_refcount = 2; // One held by scanner thread, one by the subscription

  hts_thread_create_detached("fa scanner", scanner, s);

  prop_subscribe(PROP_SUB_TRACK_DESTROY,
		 PROP_TAG_CALLBACK, scanner_stop, s,
		 PROP_TAG_ROOT, s->s_root,
		 NULL);
}


typedef struct album_art_scanner {
  char *aas_url;
  prop_t *aas_prop;

} album_art_scanner_t;


/**
 *
 */
static void *
album_art_scanner(void *aux)
{
  album_art_scanner_t *aas = aux;
  int64_t album_art_score = 0;  // Bigger is better
  char album_art[URL_MAX] = {0};
  fa_dir_entry_t *fde;
  fa_dir_t *fd;

  if((fd = fa_scandir(aas->aas_url, NULL, 0)) != NULL) {

    TAILQ_FOREACH(fde, &fd->fd_entries, fde_link) {
      if(!strncasecmp(fde->fde_filename, "albumart", 8) ||
	 !strncasecmp(fde->fde_filename, "folder.", 7)) {

	if(!fa_dir_entry_stat(fde))
	  album_art_score = fde->fde_stat.st_size;
	snprintf(album_art, sizeof(album_art), "%s", fde->fde_url);
      }
    }

    if(album_art[0])
      prop_set_string(aas->aas_prop, album_art);
    
    fa_dir_free(fd);
  }


  prop_ref_dec(aas->aas_prop);
  free(aas->aas_url);
  free(aas);
  return NULL;
}


/**
 *
 */
void
fa_scanner_find_albumart(const char *url, prop_t *album_art)
{
  album_art_scanner_t *aas = malloc(sizeof(album_art_scanner_t));

  aas->aas_url = strdup(url);
  aas->aas_prop = album_art;
  
  hts_thread_create_detached("album art scanner", album_art_scanner, aas);

}
