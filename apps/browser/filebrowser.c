/*
 *  File browser
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

#include <pthread.h>
#include <assert.h>

#include <sys/stat.h>
#include <sys/time.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>

#include "showtime.h"
#include "browser.h"
#include "filebrowser.h"

TAILQ_HEAD(filebrowser_dir_queue, filebrowser_dir);
TAILQ_HEAD(filebrowser_entry_queue, filebrowser_entry);


typedef struct filebrowser_entry {
  TAILQ_ENTRY(filebrowser_entry) fbe_link;
  const char *fbe_url;
  const char *fbe_title;
  int fbe_id;
  int fbe_type;
} filebrowser_entry_t;



typedef struct filebrowser_dir {
  const char *fbd_path;

  TAILQ_ENTRY(filebrowser_dir) fbd_link;

  int fbd_id;

  enum {
    FBD_NONE,
    FBD_SCAN_DIR,
    FBD_PROBE,
    FBD_DONE,
  } fbd_state;

  DIR *fbd_dir;
  struct filebrowser *fbd_fb;

  int fbd_refcount;

  struct filebrowser_entry_queue fbd_entries;
  int fbd_nentries;
  filebrowser_entry_t *fbd_curprobe;

  struct filebrowser_dir_queue *fbd_qptr;

  int fbd_doprobe;

  const char *fbd_icon;
  int fbd_icon_score;

} filebrowser_dir_t;




typedef struct filebrowser {
  browser_interface_t fb_bi;

  int fb_id_tally;

  pthread_mutex_t fb_mutex;
  pthread_cond_t fb_cond;
  
  struct filebrowser_dir_queue fb_scanq;
  struct filebrowser_dir_queue fb_probeq;

  pthread_t fb_thread;

} filebrowser_t;

/**
 *
 */
static void
fbe_free(filebrowser_dir_t *fbd, filebrowser_entry_t *fbe)
{
  TAILQ_REMOVE(&fbd->fbd_entries, fbe, fbe_link);
  free((void *)fbe->fbe_title);
  free((void *)fbe->fbe_url);
  free(fbe);

}

/**
 *
 */
static void
fbe_destroy(filebrowser_t *fb, filebrowser_dir_t *fbd,
	    filebrowser_entry_t *fbe)
{
  browser_message_enqueue(&fb->fb_bi, BM_DELETE, fbe->fbe_id,
			  fbd->fbd_id, fbe->fbe_url, NULL);

  fbe_free(fbd, fbe);
}

/**
 *
 */
static int
fbd_deref(filebrowser_dir_t *fbd)
{
  filebrowser_t *fb = fbd->fbd_fb;
  filebrowser_entry_t *fbe;

  if(fbd->fbd_refcount > 1) {
    fbd->fbd_refcount--;
    return 0;
  }

  if(fbd->fbd_qptr != NULL)
    TAILQ_REMOVE(fbd->fbd_qptr, fbd, fbd_link);

  if(fbd->fbd_dir != NULL)
    closedir(fbd->fbd_dir);
  
  free((void *)fbd->fbd_path);
  free((void *)fbd->fbd_icon);

  while((fbe = TAILQ_FIRST(&fbd->fbd_entries)) != NULL)
    fbe_destroy(fb, fbd, fbe);

  free(fbd);
  return 1;
}


/**
 *
 */
static int
fb_compar(const void *A, const void *B)
{
  const filebrowser_entry_t *a = *(const filebrowser_entry_t **)A;
  const filebrowser_entry_t *b = *(const filebrowser_entry_t **)B;

  return strcasecmp(a->fbe_title, b->fbe_title);
}


/**
 *
 */
static int
fb_scan_completed(filebrowser_t *fb, filebrowser_dir_t *fbd)
{
  filebrowser_entry_t *fbe;
  filebrowser_entry_t **vec;
  mediainfo_t mi;
  int i;

  vec = malloc(fbd->fbd_nentries * sizeof(filebrowser_entry_t *));

  i = 0;
  TAILQ_FOREACH(fbe, &fbd->fbd_entries, fbe_link)
    vec[i++] = fbe;
  
  qsort(vec, fbd->fbd_nentries, sizeof(filebrowser_entry_t *), fb_compar);

  memset(&mi, 0, sizeof(mediainfo_t));

  TAILQ_INIT(&fbd->fbd_entries);
  for(i = 0; i < fbd->fbd_nentries; i++) {
    fbe = vec[i];
    TAILQ_INSERT_TAIL(&fbd->fbd_entries, fbe, fbe_link);

    memset(&mi, 0, sizeof(mi));

    mi.mi_type  = fbe->fbe_type;
    mi.mi_title = fbe->fbe_title;

    mediaprobe(fbe->fbe_url, &mi, 1, NULL);
    browser_message_enqueue(&fb->fb_bi, BM_ADD, fbe->fbe_id,
			    fbd->fbd_id, fbe->fbe_url, &mi);
  }

  free(vec);

  browser_message_enqueue(&fb->fb_bi, BM_SCAN_COMPLETE, fbd->fbd_id,
			  fbd->fbd_id, fbd->fbd_path, NULL);

  fbd->fbd_curprobe = TAILQ_FIRST(&fbd->fbd_entries);
  return fbd->fbd_doprobe ? FBD_PROBE : FBD_DONE;
}


/**
 * Return 1 if the directory is a DVD rip
 */
static int
fb_check_dvd(const char *path)
{
  char fbuf[1000];
  struct stat st;

  snprintf(fbuf, sizeof(fbuf), "%s/VIDEO_TS/VIDEO_TS.IFO", path);

  if(stat(fbuf, &st) != -1)
    return 1;

  snprintf(fbuf, sizeof(fbuf), "%s/video_ts/video_ts.ifo", path);

  if(stat(fbuf, &st) != -1)
    return 1;

  return 0;
}
 

/**
 *
 */
const char *fileextensions[] = {
  "jpg",
  "mp3",
  "mov",
  "aac",
  "m4a",
  "mp2",
  "wma",
  "ogg",
  NULL};

/**
 *
 */
static int
fb_scan_dir(filebrowser_t *fb, filebrowser_dir_t *fbd)
{
  struct stat st;
  struct dirent *d;
  filebrowser_entry_t *fbe;
  char buf[1000];
  char *x;
  int i, type;
  struct stat sbuf;
  
  if((d = readdir(fbd->fbd_dir)) == NULL) {
    closedir(fbd->fbd_dir);
    fbd->fbd_dir = NULL;

    /* Send message with a folder icon (if found) */

    if(fbd->fbd_icon != NULL) {
      mediainfo_t mi;
      memset(&mi, 0, sizeof(mi));
      mi.mi_icon = fbd->fbd_icon;

      browser_message_enqueue(&fb->fb_bi, BM_FOLDERICON, fbd->fbd_id,
			      fbd->fbd_id, fbd->fbd_path, &mi);
    }
    return fb_scan_completed(fb, fbd);
  }

  if(d->d_name[0] == '.')
    return FBD_SCAN_DIR;

  snprintf(buf, sizeof(buf), "%s/%s", fbd->fbd_path, d->d_name);

  /* Check for album art / Folder icon */

  x = strrchr(d->d_name, '.');
  if(x != NULL && !strcasecmp(x, ".jpg") &&
     (!strncasecmp(d->d_name, "albumart", 8) || 
      !strcasecmp(d->d_name, "folder.jpg"))) {
    /* Bigger files are probably better images, so score after size */

    if(stat(buf, &st) == 0 && st.st_size > fbd->fbd_icon_score) {
      fbd->fbd_icon_score = st.st_size;
      free((void *)fbd->fbd_icon);
      fbd->fbd_icon = strdup(buf);
    }
    return FBD_SCAN_DIR;
  }

  type = d->d_type;

  if(stat(buf, &sbuf))
    return FBD_SCAN_DIR;  /* stat file */

  if(d->d_type == DT_LNK) {
    /* Resolve what the link point to */

    switch(sbuf.st_mode & S_IFMT) {
    case S_IFDIR:
      type = DT_DIR;
      break;
    case S_IFREG:
      type = DT_REG;
      break;
    default:
      return FBD_SCAN_DIR;
    }
  }

  switch(type) {
  case DT_DIR:
    type = fb_check_dvd(buf) ? MI_DIR_DVD : MI_DIR;
    break;
    
  case DT_REG:
    if(sbuf.st_size < 2048)
      return FBD_SCAN_DIR; /* too small to be a media file */

    type = MI_FILE;
    break;
    
  default:
    return FBD_SCAN_DIR;
  }

  fbe = calloc(1, sizeof(filebrowser_entry_t));
  fbe->fbe_title = strdup(d->d_name);
  fbe->fbe_type = type;

  x = strrchr(fbe->fbe_title, '.');
  if(type == MI_FILE && x != NULL) {
    i = 0;
    while(fileextensions[i] != NULL) {
      if(!strcasecmp(x + 1, fileextensions[i])) {
	*x = 0;
	break;
      }
      i++;
    }
  }

  fbe->fbe_url = strdup(buf);
  fbe->fbe_id = ++fb->fb_id_tally;

  fbd->fbd_nentries++;
  TAILQ_INSERT_HEAD(&fbd->fbd_entries, fbe, fbe_link);
  return FBD_SCAN_DIR;
}

/**
 *
 */
static int
fb_probe(filebrowser_t *fb, filebrowser_dir_t *fbd)
{
  filebrowser_entry_t *fbe = fbd->fbd_curprobe;
  mediainfo_t mi;
  int r;

  if(fbe == NULL)
    return FBD_DONE;

  fbd->fbd_curprobe = TAILQ_NEXT(fbe, fbe_link);

  if(fbe->fbe_type == MI_FILE) {

    memset(&mi, 0, sizeof(mediainfo_t));

    fbd->fbd_refcount++;
    pthread_mutex_unlock(&fb->fb_mutex);
    r = mediaprobe(fbe->fbe_url, &mi, 0, fbd->fbd_icon);
    pthread_mutex_lock(&fb->fb_mutex);

    if(fbd_deref(fbd)) {
      mediaprobe_free(&mi);
      return FBD_NONE; /* don't continue (fbd is free'd now) */
    }
    if(r == 0) {
      browser_message_enqueue(&fb->fb_bi, BM_UPDATE, fbe->fbe_id,
			      fbd->fbd_id, fbe->fbe_url, &mi);
    } else {
      fbe_destroy(fb, fbd, fbe);
    }
    mediaprobe_free(&mi);
  }
  /* If we are finished or if we dont want to probe anymore, return
     done, otherwise make it continue */

  return fbd->fbd_curprobe && fbd->fbd_doprobe ? FBD_PROBE : FBD_DONE;
}

/**
 *
 */
static void
filebrowser_dir_enqueue(filebrowser_t *fb, filebrowser_dir_t *fbd,
			int nextstate)
{
  struct filebrowser_dir_queue *qptr;

  switch(nextstate) {
  case FBD_NONE:
    return; /* Do nothing, fbd is free'd */
  case FBD_SCAN_DIR:
    qptr = &fb->fb_scanq;
    break;
  case FBD_PROBE:
    qptr = &fb->fb_probeq;
    break;
  default:
    qptr = NULL;
    break;
  }
  
  fbd->fbd_state = nextstate;
  if(qptr != NULL) {
    fbd->fbd_qptr = qptr;
    TAILQ_INSERT_TAIL(qptr, fbd, fbd_link);
  }
}


/**
 *
 */
static void *
filebrowser_thread(void *aux)
{
  filebrowser_t *fb = aux;
  filebrowser_dir_t *fbd;
  int nextstate;

  pthread_mutex_lock(&fb->fb_mutex);

  while(1) {

    while(1) {
      pthread_testcancel();
      if((fbd = TAILQ_FIRST(&fb->fb_scanq)) != NULL)
	break;
      if((fbd = TAILQ_FIRST(&fb->fb_probeq)) != NULL)
	break;
      pthread_cond_wait(&fb->fb_cond, &fb->fb_mutex);
    }

    TAILQ_REMOVE(fbd->fbd_qptr, fbd, fbd_link);
    fbd->fbd_qptr = NULL;

    switch(fbd->fbd_state) {
    case FBD_SCAN_DIR:
      nextstate = fb_scan_dir(fb, fbd);
      break;

    case FBD_PROBE:
      nextstate = fb_probe(fb, fbd);
      break;

    default:
      assert(0);
    }
    filebrowser_dir_enqueue(fb, fbd, nextstate);
  }
  return NULL;
}


/**
 *
 */
static void *
filebrowser_open(struct browser_interface *bi, const char *path, int id,
		 int doprobe)
{
  filebrowser_t *fb = (filebrowser_t *)bi; 
  filebrowser_dir_t *fbd;
  DIR *dir;

  if((dir = opendir(path)) == NULL)
    return NULL;

  fbd = calloc(1, sizeof(filebrowser_dir_t)); 
  fbd->fbd_fb = fb;
  fbd->fbd_dir = dir;
  fbd->fbd_path = strdup(path);
  fbd->fbd_refcount = 1;
  fbd->fbd_id = id;
  fbd->fbd_doprobe = doprobe;

  TAILQ_INIT(&fbd->fbd_entries);

  pthread_mutex_lock(&fb->fb_mutex);
  filebrowser_dir_enqueue(fb, fbd, FBD_SCAN_DIR);
  pthread_cond_signal(&fb->fb_cond);
  pthread_mutex_unlock(&fb->fb_mutex);

  return fbd;
}


/**
 *
 */
static void
filebrowser_close(void *opaque)
{
  filebrowser_dir_t *fbd = opaque;
  filebrowser_t *fb = fbd->fbd_fb;

  pthread_mutex_lock(&fb->fb_mutex);
  fbd_deref(fbd);
  pthread_mutex_unlock(&fb->fb_mutex);
}



/**
 *
 */
static void
filebrowser_probe(void *opaque, int run)
{
  filebrowser_dir_t *fbd = opaque;
  filebrowser_t *fb = fbd->fbd_fb;

  pthread_mutex_lock(&fb->fb_mutex);
  fbd->fbd_doprobe = run;
  if(fbd->fbd_state == FBD_DONE) {
    filebrowser_dir_enqueue(fb, fbd, FBD_PROBE);
    pthread_cond_signal(&fb->fb_cond);
  }
  pthread_mutex_unlock(&fb->fb_mutex);
}


/**
 *
 */
static void
filebrowser_destroy(browser_interface_t *bi)
{
  filebrowser_t *fb = (filebrowser_t *)bi; 

  pthread_cancel(fb->fb_thread);
  pthread_join(fb->fb_thread, NULL);
  free(fb);
}



/**
 *
 */
browser_interface_t *
filebrowser_create(void)
{
  filebrowser_t *fb;

  fb = calloc(1, sizeof(filebrowser_t));

  pthread_mutex_init(&fb->fb_mutex, NULL);
  pthread_cond_init(&fb->fb_cond, NULL);
  TAILQ_INIT(&fb->fb_scanq);
  TAILQ_INIT(&fb->fb_probeq);

  fb->fb_bi.bi_open    = filebrowser_open;
  fb->fb_bi.bi_close   = filebrowser_close;
  fb->fb_bi.bi_probe   = filebrowser_probe;
  fb->fb_bi.bi_destroy = filebrowser_destroy;

  pthread_create(&fb->fb_thread, NULL, filebrowser_thread, fb);
  return &fb->fb_bi;
}
