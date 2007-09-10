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

#include <sys/stat.h>
#include <sys/time.h>
#include <pthread.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>

#include <ffmpeg/avcodec.h>
#include <ffmpeg/avformat.h>
#include <libglw/glw.h>

#include "showtime.h"
#include "input.h"
#include "layout/layout.h"
#include "menu.h"
#include "apps/dvdplayer/dvd.h"
#include "apps/playlist/playlist.h"
#include "play_file.h"
#include "miw.h"
#include "mediaprobe.h"

TAILQ_HEAD(peep_work_head, peep_work);
TAILQ_HEAD(be_queue, b_entry);

static void *peep_thread(void *aux);

typedef struct b_entry {

  TAILQ_ENTRY(b_entry) be_link;
  TAILQ_ENTRY(b_entry) be_image_link;

  enum {
    BE_DIR = 1,
    BE_FILE,
    BE_DVD,
  } be_type;

  const char *be_filename;


  glw_t *be_widget;
  glw_t *be_sub;
  glw_t *be_image;

  mediainfo_t be_mi;
  float be_xpos;
} b_entry_t;




typedef struct b_dir {
  TAILQ_ENTRY(b_dir) bd_link;
  struct be_queue bd_entries;
  struct be_queue bd_images;

  char *bd_name;
  
  glw_t *bd_list;

  b_entry_t *bd_parent;

  float bd_load_progress;

  struct browser *bd_b;

  int bd_ref; // just reference for the memory allocation

  // slideshow members

  b_entry_t *bd_s_cur;
  glw_vertex_anim_t bd_s_pos;
  float bd_s_xpos_max;
  glw_t *bd_s_saved;

} b_dir_t;

TAILQ_HEAD(b_dir_queue, b_dir);

typedef struct browser {
  char *b_rootpath;

  struct b_dir_queue b_dir_stack;
  pthread_mutex_t b_lock;

  appi_t *b_ai;

  pthread_mutex_t b_peep_lock;
  pthread_cond_t b_peep_cond;

  struct peep_work_head b_peep_queue;

  pthread_t b_peep_thread;

} browser_t;



typedef struct peep_work {
  TAILQ_ENTRY(peep_work) pw_link;
  const char *pw_path;
  glw_t *pw_container;
  browser_t *pw_b;
  int pw_refcount;

} peep_work_t;


static void peep_add(browser_t *b, const char *path, glw_t *parent,
		     float weight);

static void browser_slideshow(browser_t *b, b_dir_t *bd, b_entry_t *be);

static int 
browser_entry_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  return 0;
}


static void
browser_free_entry(b_dir_t *bd, b_entry_t *be)
{
  if(be->be_image != NULL)
    glw_destroy(be->be_image);
  
  if(be->be_mi.mi_type == MI_IMAGE)
    TAILQ_REMOVE(&bd->bd_images, be, be_image_link);

  mediaprobe_free(&be->be_mi);
  free((void *)be->be_filename);
  TAILQ_REMOVE(&bd->bd_entries, be, be_link);
  free(be);
}


static void
browser_free_dir(b_dir_t *bd)
{
  b_entry_t *be;

  bd->bd_list->glw_flags |= GLW_AUTO_DESTROY;

  while((be = TAILQ_FIRST(&bd->bd_entries)) != NULL)
    browser_free_entry(bd, be);

  free(bd->bd_name);

  free(bd);

}





static int
browser_check_dvd(b_dir_t *bd, b_entry_t *be)
{
  char fbuf[500];
  struct stat st;

  snprintf(fbuf, sizeof(fbuf), "%s/%s/VIDEO_TS/VIDEO_TS.IFO", 
	   bd->bd_name, be->be_filename);

  if(stat(fbuf, &st) != -1)
    return 1;

  snprintf(fbuf, sizeof(fbuf), "%s/%s/video_ts/video_ts.ifo", 
	   bd->bd_name, be->be_filename);

  if(stat(fbuf, &st) != -1)
    return 1;

  return 0;
}
 


/*
 * Media widget for wide (10:1) plates
 */

#define BROWSER_INCLUDE_TRACK 0x1
#define BROWSER_INCLUDE_AUTHOR 0x2

static void
browser_make_widget_10_1(glw_t *p, b_dir_t *bd, b_entry_t *be, int flags)
{
  glw_t *x, *y, *z;
  mediainfo_t *mi = &be->be_mi;
  double weight;
  char tmp[500];

  be->be_widget = p = 
    glw_create(GLW_ZOOM_SELECTOR,
	       GLW_ATTRIB_PARENT, p,
	       GLW_ATTRIB_SIGNAL_HANDLER, browser_entry_callback, be, 0,
	       NULL);
 
  z = glw_create(GLW_BITMAP,
		 GLW_ATTRIB_PARENT, p,
		 GLW_ATTRIB_FILENAME, "icon://plate-wide.png",
		 GLW_ATTRIB_FLAGS, GLW_NOASPECT,
		 NULL);

  x = glw_create(GLW_CONTAINER_X,
		 GLW_ATTRIB_PARENT, z,
		 NULL);

  switch(be->be_type) {
    
    /* directory */

  case BE_DIR:
    glw_create(GLW_BITMAP,
	       GLW_ATTRIB_PARENT, x,
	       GLW_ATTRIB_FILENAME, "icon://dir.png",
	       NULL);

    glw_create(GLW_TEXT_BITMAP,
	       GLW_ATTRIB_PARENT, x,
	       GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_LEFT,
	       GLW_ATTRIB_WEIGHT, 10.0,
	       GLW_ATTRIB_CAPTION, be->be_filename,
	       NULL);

    snprintf(tmp, sizeof(tmp), "%s/%s", 
	     bd->bd_name, be->be_filename);

    peep_add(bd->bd_b, tmp, x, 10.0f);
    break;

    /* DVD directory */

  case BE_DVD:
    glw_create(GLW_BITMAP,
	       GLW_ATTRIB_PARENT, x,
	       GLW_ATTRIB_FILENAME, "icon://cd.png",
	       NULL);

    glw_create(GLW_TEXT_BITMAP,
	       GLW_ATTRIB_PARENT, x,
	       GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_LEFT,
	       GLW_ATTRIB_WEIGHT, 20.0,
	       GLW_ATTRIB_CAPTION, be->be_filename,
	       NULL);
    break;

    /* Ordinary File */

  case BE_FILE:
    switch(mi->mi_type) {

      /* Audio and/or video stream */

    case MI_AUDIO:
    case MI_VIDEO:

      glw_create(GLW_BITMAP, 
		 GLW_ATTRIB_PARENT, x,
		 GLW_ATTRIB_WEIGHT, 0.5,
		 GLW_ATTRIB_FILENAME, mi->mi_type == MI_AUDIO ? 
		 "icon://audio.png" : "icon://video.png",
		 NULL);

      glw_create(GLW_DUMMY,
		 GLW_ATTRIB_PARENT, x,
		 GLW_ATTRIB_WEIGHT, 0.5,
		 NULL);

      
      if(flags & BROWSER_INCLUDE_AUTHOR) {

	glw_create(GLW_TEXT_BITMAP,
		   GLW_ATTRIB_ASPECT, 14.0f,
		   GLW_ATTRIB_WEIGHT, 7.0f,
		   GLW_ATTRIB_PARENT, x,
		   GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_LEFT,
		   GLW_ATTRIB_CAPTION, mi->mi_author,
		   GLW_ATTRIB_TEXT_FLAGS, GLW_TEXT_UTF8,
		   NULL);
	weight = 10.0f;
      } else {
	weight = 17.0f;
      }

      glw_create(GLW_TEXT_BITMAP,
		 GLW_ATTRIB_ASPECT, weight * 2.0f,
		 GLW_ATTRIB_WEIGHT, weight,
		 GLW_ATTRIB_PARENT, x,
		 GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_LEFT,
		 GLW_ATTRIB_CAPTION, mi->mi_title,
		 GLW_ATTRIB_TEXT_FLAGS, GLW_TEXT_UTF8,
		 NULL);


      y = glw_create(GLW_CONTAINER_Y,
		     GLW_ATTRIB_PARENT, x,
		     GLW_ATTRIB_WEIGHT, 1.0f,
		     NULL);

      if(flags & BROWSER_INCLUDE_TRACK) {

	snprintf(tmp, sizeof(tmp), "#%d", mi->mi_track);

	glw_create(GLW_TEXT_BITMAP,
		   GLW_ATTRIB_PARENT, y,
		   GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_RIGHT,
		   GLW_ATTRIB_CAPTION, tmp,
		   NULL);
      } else {
	glw_create(GLW_DUMMY,
		   GLW_ATTRIB_PARENT, y,
		   NULL);
      }


      snprintf(tmp, sizeof(tmp), "%d:%02d",
	       mi->mi_duration / 60, mi->mi_duration % 60);

      glw_create(GLW_TEXT_BITMAP,
		 GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_RIGHT,
		 GLW_ATTRIB_CAPTION, tmp,
		 GLW_ATTRIB_PARENT, y,
		 NULL);
      break;

      /* Picture */

    case MI_IMAGE:
      snprintf(tmp, sizeof(tmp), "thumb://%s/%s", 
	       bd->bd_name, be->be_filename);

      glw_create(GLW_BITMAP, 
		 GLW_ATTRIB_PARENT, x,
		 GLW_ATTRIB_FILENAME, tmp,
		 GLW_ATTRIB_FLAGS, GLW_BORDER_BLEND,
		 GLW_ATTRIB_BORDER_WIDTH, 0.05,
		 NULL);
      break;

      /* Playlist */

    case MI_PLAYLIST_PLS:
      glw_create(GLW_BITMAP, 
		 GLW_ATTRIB_PARENT, x,
		 GLW_ATTRIB_FILENAME, "icon://playlist.png",
		 NULL);

      glw_create(GLW_TEXT_BITMAP,
		 GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_LEFT,
		 GLW_ATTRIB_WEIGHT, 15.0,
		 GLW_ATTRIB_CAPTION, mi->mi_title,
		 NULL);
 
 
      snprintf(tmp, sizeof(tmp), "%d tracks", mi->mi_track);

      glw_create(GLW_TEXT_BITMAP,
		 GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_RIGHT,
		 GLW_ATTRIB_WEIGHT, 5.0,
		 GLW_ATTRIB_PARENT, x,
		 GLW_ATTRIB_CAPTION, tmp,
		 NULL);
      break;

    case MI_ISO:
      glw_create(GLW_BITMAP,
		 GLW_ATTRIB_PARENT, x,
		 GLW_ATTRIB_FILENAME, "icon://cd.png",
		 NULL);

      glw_create(GLW_TEXT_BITMAP,
		 GLW_ATTRIB_PARENT, x,
		 GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_LEFT,
		 GLW_ATTRIB_WEIGHT, 20.0,
		 GLW_ATTRIB_CAPTION, mi->mi_title,
		 NULL);
    }
    break;
  }
}



#if 1

/*
 * Media widget for 16:9 plates
 */

static void
browser_make_widget_16_9(glw_t *p, b_dir_t *bd, b_entry_t *be)
{
  glw_t *x, *y, *z;
  mediainfo_t *mi = &be->be_mi;
  char tmp[500];

  be->be_widget = p =
    glw_create(GLW_ZOOM_SELECTOR,
	       GLW_ATTRIB_PARENT, p,
	       GLW_ATTRIB_SIGNAL_HANDLER, browser_entry_callback, be, 0,
	       NULL);
 
  z = glw_create(GLW_BITMAP,
		 GLW_ATTRIB_PARENT, p,
		 GLW_ATTRIB_FILENAME, "icon://plate.png",
		 GLW_ATTRIB_FLAGS, GLW_NOASPECT,
		 NULL);


  switch(be->be_type) {
    
    /* directory */

  case BE_DIR:
#if 0
    z = glw_create(GLW_BITMAP,
		   GLW_ATTRIB_PARENT, z,
		   GLW_ATTRIB_ALPHA, 0.25,
		   GLW_ATTRIB_FILENAME, "icon://dir.png",
		   //		   GLW_ATTRIB_FLAGS, GLW_NOASPECT,
		   NULL);
#endif

    y = glw_create(GLW_CONTAINER_Y,
		   GLW_ATTRIB_PARENT, z,
		   NULL);

    glw_create(GLW_TEXT_BITMAP,
	       GLW_ATTRIB_PARENT, y,
	       GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
	       GLW_ATTRIB_CAPTION, be->be_filename,
	       NULL);

    snprintf(tmp, sizeof(tmp), "%s/%s", 
	     bd->bd_name, be->be_filename);

    peep_add(bd->bd_b, tmp, y, 2.0f);
    break;

    /* DVD directory */

  case BE_DVD:
    y = glw_create(GLW_CONTAINER_Y,
		   GLW_ATTRIB_PARENT, z,
		   NULL);
    
    glw_create(GLW_TEXT_BITMAP,
	       GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
	       GLW_ATTRIB_PARENT, y,
	       GLW_ATTRIB_CAPTION, be->be_filename,
	       NULL);

    x = glw_create(GLW_CONTAINER_X,
		   GLW_ATTRIB_WEIGHT, 2.0,
		   GLW_ATTRIB_PARENT, y,
		   NULL);

    glw_create(GLW_BITMAP, 
	       GLW_ATTRIB_PARENT, x,
	       GLW_ATTRIB_FILENAME, "icon://cd.png",
	       NULL);
    break;

    /* Ordinary File */

  case BE_FILE:

    y = glw_create(GLW_CONTAINER_Y,
		   GLW_ATTRIB_PARENT, z,
		   NULL);

    switch(mi->mi_type) {

      /* Audio and/or video stream */

    case MI_AUDIO:
    case MI_VIDEO:

      glw_create(GLW_TEXT_BITMAP,
		 GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
		 GLW_ATTRIB_PARENT, y,
		 GLW_ATTRIB_CAPTION, mi->mi_title,
		 GLW_ATTRIB_TEXT_FLAGS, GLW_TEXT_UTF8,
		 NULL);

      x = glw_create(GLW_CONTAINER_X,
		     GLW_ATTRIB_WEIGHT, 2.0,
		     GLW_ATTRIB_PARENT, y,
		     NULL);

      glw_create(GLW_BITMAP, 
		 GLW_ATTRIB_PARENT, x,
		 GLW_ATTRIB_FILENAME, mi->mi_type == MI_AUDIO ? 
		 "icon://audio.png" : "icon://video.png",
		 NULL);

 
      break;

      /* Picture */

    case MI_IMAGE:
      snprintf(tmp, sizeof(tmp), "thumb://%s/%s", 
	       bd->bd_name, be->be_filename);

      glw_create(GLW_BITMAP, 
		 GLW_ATTRIB_PARENT, y,
		 GLW_ATTRIB_FILENAME, tmp,
		 GLW_ATTRIB_FLAGS, GLW_BORDER_BLEND,
		 GLW_ATTRIB_BORDER_WIDTH, 0.05,
		 NULL);
      break;

      /* Playlist */

    case MI_PLAYLIST_PLS:
      glw_create(GLW_TEXT_BITMAP,
		 GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
		 GLW_ATTRIB_PARENT, y,
		 GLW_ATTRIB_CAPTION, mi->mi_title,
		 NULL);

 
      glw_create(GLW_BITMAP, 
		 GLW_ATTRIB_PARENT, y,
		 GLW_ATTRIB_FILENAME, "icon://playlist.png",
		 NULL);

 
      snprintf(tmp, sizeof(tmp), "%d tracks", mi->mi_track);

      glw_create(GLW_TEXT_BITMAP,
		 GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
		 GLW_ATTRIB_PARENT, y,
		 GLW_ATTRIB_CAPTION, tmp,
		 NULL);
      break;

    case MI_ISO:
      glw_create(GLW_TEXT_BITMAP,
		 GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
		 GLW_ATTRIB_PARENT, y,
		 GLW_ATTRIB_CAPTION, mi->mi_title,
		 NULL);

      x = glw_create(GLW_CONTAINER_X,
		     GLW_ATTRIB_WEIGHT, 2.0,
		     GLW_ATTRIB_PARENT, y,
		     NULL);

      glw_create(GLW_BITMAP, 
		 GLW_ATTRIB_PARENT, x,
		 GLW_ATTRIB_FILENAME, "icon://cd.png",
		 NULL);
    }
    break;
  }
}
#endif






static int 
loaderbar_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  b_dir_t *bd = opaque;

  switch(signal) {
  case GLW_SIGNAL_PREPARE:
    w->glw_extra = bd->bd_load_progress;
    return 0;
  default:
    return 0;
  }
}







static int 
browser_array_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  int r = 0;
  inputevent_t *ie;
  b_dir_t *bd = opaque;
  browser_t *b = bd->bd_b;

  va_list ap;
  va_start(ap, signal);

  switch(signal) {
  case GLW_SIGNAL_INPUT_EVENT:
    ie = va_arg(ap, void *);
    input_postevent(&b->b_ai->ai_ic, ie);
    r = 1;
  default:
    break;
  }
  va_end(ap);
  return r;
}





static int
sort_b_entry_trackwise(const void *A, const void *B)
{
  b_entry_t *a = *(b_entry_t **)A;
  b_entry_t *b = *(b_entry_t **)B;

  if(a->be_type == b->be_type) {

    if(a->be_type != BE_FILE)
      return strcasecmp(a->be_filename, b->be_filename);
      
    if(a->be_mi.mi_track == b->be_mi.mi_track)
      return strcasecmp(a->be_mi.mi_title, b->be_mi.mi_title);

    return a->be_mi.mi_track - b->be_mi.mi_track;
  }
  return a->be_type - b->be_type;
}

static int
sort_b_entry(const void *A, const void *B)
{
  b_entry_t *a = *(b_entry_t **)A;
  b_entry_t *b = *(b_entry_t **)B;

  if(a->be_type == b->be_type) {

    if(a->be_type != BE_FILE)
      return strcasecmp(a->be_filename, b->be_filename);
      
    if(a->be_mi.mi_time != 0 && b->be_mi.mi_time != 0)
      return a->be_mi.mi_time - b->be_mi.mi_time;

    return strcasecmp(a->be_mi.mi_title, b->be_mi.mi_title);
  }
  return a->be_type - b->be_type;
}


static glw_t *
browser_dir_title(browser_t *b, b_dir_t *bd)
{
  glw_t *sk, *f;
  char buf[300];
  b_dir_t *bd0;

  sk = glw_create(GLW_CONTAINER_Z, NULL);


  f = glw_create(GLW_XFADER,
		 GLW_ATTRIB_PARENT, sk,
		 NULL);

  glw_create(GLW_BAR,
	     GLW_ATTRIB_COLOR, GLW_COLOR_LIGHT_BLUE,
	     GLW_ATTRIB_PARENT, f,
	     GLW_ATTRIB_SIGNAL_HANDLER, loaderbar_callback, bd, 0,
	     NULL);

  buf[0] = 0;

  strcat(buf, "File browser / ");

  TAILQ_FOREACH_REVERSE(bd0, &b->b_dir_stack, b_dir_queue, bd_link) {
    if(bd0->bd_parent == NULL)
      continue;
    strcat(buf, bd0->bd_parent->be_filename);
    strcat(buf, " / ");
  }

  if(bd->bd_parent != NULL)
    strcat(buf, bd->bd_parent->be_filename);

  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_PARENT, sk,
	     GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
	     GLW_ATTRIB_CAPTION, buf,
	     NULL);
  return sk;
}


static void
browser_load_dir(browser_t *b, b_entry_t *src, char *path, int enq, glw_t *p)
{
  DIR *dir;
  struct dirent *d;
  char fbuf[500];
  b_entry_t *be, *n, **vec;
  b_dir_t *bd;
  int cnt = 0, tot, i, cur;
  int images = 0;
  int imagemode;
  int hastrack = 0;
  int music = 0;
  glw_t *sk2 = NULL;
  const char *author;
  int flags;

  bd = malloc(sizeof(b_dir_t));
  bd->bd_load_progress = 0;
  bd->bd_b = b;

  TAILQ_INIT(&bd->bd_entries);
  TAILQ_INIT(&bd->bd_images);
  bd->bd_name = strdup(path);

  bd->bd_parent = src;

  if(!enq) {
    sk2 = browser_dir_title(b, bd);

    glw_destroy_auto(p); // destroy any lingering directories

    bd->bd_list =
      glw_create(GLW_ARRAY,
		 GLW_ATTRIB_PARENT, p,
		 GLW_ATTRIB_SIGNAL_HANDLER, browser_array_callback, bd, 0,
		 GLW_ATTRIB_SIDEKICK, sk2,
		 GLW_ATTRIB_X_SLICES, 3,
		 GLW_ATTRIB_Y_SLICES, 4,
		 NULL);
  }

  if((dir = opendir(path)) != NULL) {

    while((d = readdir(dir)) != NULL) {
      if(d->d_name[0] == '.')
	continue;

      if(!strncasecmp(d->d_name, "albumart", 8))
	continue;

      if(!strcasecmp(d->d_name, "folder.jpg"))
	continue;


      be = calloc(1, sizeof(b_entry_t));
      be->be_filename = strdup(d->d_name);
      
      if(d->d_type == DT_DIR || d->d_type == DT_LNK) {
	be->be_type = BE_DIR;
      } else {
	be->be_type = BE_FILE;
      }

      TAILQ_INSERT_HEAD(&bd->bd_entries, be, be_link);
      cnt++;
    }
    closedir(dir);
  }
  tot = cnt;
  cur = 0;

  for(be = TAILQ_FIRST(&bd->bd_entries) ; be != NULL; be = n) {

    cur++;
    bd->bd_load_progress = (float)cur / (float)tot;

    n = TAILQ_NEXT(be, be_link);
    
    if(be->be_type == BE_DIR) {
      if(browser_check_dvd(bd, be))
	be->be_type = BE_DVD;
    }
    
    if(be->be_type == BE_FILE) {
      snprintf(fbuf, sizeof(fbuf), "%s/%s", bd->bd_name, be->be_filename);
      
      if(mediaprobe(fbuf, &be->be_mi, 0)) {
	browser_free_entry(bd, be);
	cnt--;
      } else if(be->be_mi.mi_type == MI_IMAGE) {
	images++;
      } else if(be->be_mi.mi_type == MI_AUDIO) {
	music++;
	if(be->be_mi.mi_track > 0)
	  hastrack++;
      }
    }
  }

  vec = malloc(sizeof(void *) * cnt);

  cnt = 0;
  TAILQ_FOREACH(be, &bd->bd_entries, be_link) {
    vec[cnt++] = be;
  }

  if(hastrack == cnt) {
    qsort(vec, cnt, sizeof(void *), sort_b_entry_trackwise);
  } else {
    qsort(vec, cnt, sizeof(void *), sort_b_entry);
  }

  flags = hastrack == cnt ? BROWSER_INCLUDE_TRACK : 0;

  author = cnt > 0 ? vec[0]->be_mi.mi_author : NULL;

  for(i = 0; i < cnt; i++) {
    be = vec[i];
    if(be->be_mi.mi_type == MI_IMAGE)
      TAILQ_INSERT_TAIL(&bd->bd_images, be, be_image_link);

    if(strcmp(author ?: "", be->be_mi.mi_author ?: ""))
      flags |= BROWSER_INCLUDE_AUTHOR;
  }

  if(enq) {
    /* Dont dive into, just enqueue on playlist */

    for(i = 0; i < cnt; i++) {
      be = vec[i];
      snprintf(fbuf, sizeof(fbuf), "%s/%s", path, be->be_filename);

      switch(be->be_type) {
      default:
	break;

      case BE_FILE:
	playlist_enqueue(fbuf, &be->be_mi, 0);
	break;
      case BE_DIR:
	browser_load_dir(b, be, fbuf, 1, NULL);
	break;
      }
      browser_free_entry(bd, be);
    }

    free(vec);
    free(bd->bd_name);
    free(bd);
    return;
  }


  glw_create(GLW_BITMAP,
	     GLW_ATTRIB_PARENT, glw_find_by_class(sk2, GLW_XFADER),
	     GLW_ATTRIB_FILENAME, "icon://plate-wide.png",
	     GLW_ATTRIB_FLAGS, GLW_NOASPECT,
	     NULL);

  imagemode = images > cnt / 2;

  if(music == cnt) {

    glw_set(bd->bd_list,
	    GLW_ATTRIB_X_SLICES, 1,
	    GLW_ATTRIB_Y_SLICES, 13,
	    NULL);

    for(i = 0; i < cnt; i++)
      browser_make_widget_10_1(bd->bd_list, bd, vec[i], flags);

     
  } else {

    if(imagemode) {
      glw_set(bd->bd_list,
	      GLW_ATTRIB_X_SLICES, 4,
	      NULL);
    }
    for(i = 0; i < cnt; i++)
      browser_make_widget_16_9(bd->bd_list, bd, vec[i]);
  }


  pthread_mutex_lock(&b->b_lock);
  TAILQ_INSERT_HEAD(&b->b_dir_stack, bd, bd_link);
  pthread_mutex_unlock(&b->b_lock);
  free(vec);

  if(imagemode)
    browser_slideshow(b, bd, NULL);
}







static void
browser_click(appi_t *ai, browser_t *b, int sel)
{
  b_dir_t *bd;
  b_entry_t *be;
  static char fbuf[500];
  glw_t *w;

  bd = TAILQ_FIRST(&b->b_dir_stack);
  w = bd->bd_list->glw_selected;
  if(w == NULL)
    return;
  be = glw_get_opaque(w, browser_entry_callback);
  
  snprintf(fbuf, sizeof(fbuf), "%s/%s", bd->bd_name, be->be_filename);

  switch(be->be_type) {
  case BE_DIR:
    if(sel) {
      glw_nav_signal(bd->bd_list, GLW_SIGNAL_CLICK);
      playlist_flush();
    } else {
      glw_nav_signal(bd->bd_list, GLW_SIGNAL_ENTER);
    }
    browser_load_dir(b, be, fbuf, sel, be->be_widget);
    break;

  case BE_DVD:
  dvd:
    glw_nav_signal(bd->bd_list, GLW_SIGNAL_ENTER);
    dvd_main(ai, fbuf, 0, be->be_widget);
    bd->bd_list->glw_flags &= ~GLW_ZOOMED; /* XXX: fix locking */
    break;

  case BE_FILE:

    glw_nav_signal(bd->bd_list, GLW_SIGNAL_CLICK);

    switch(be->be_mi.mi_type) {

    case MI_ISO:
      goto dvd;

    case MI_PLAYLIST_PLS:
      playlist_enqueue(fbuf, &be->be_mi, 0);
      break;

    case MI_AUDIO:
      playlist_enqueue(fbuf, &be->be_mi, !sel);
      break;

    case MI_VIDEO:
      glw_nav_signal(bd->bd_list, GLW_SIGNAL_ENTER);
      play_file(fbuf, ai, &ai->ai_ic, &be->be_mi, NULL, be->be_widget);
      bd->bd_list->glw_flags &= ~GLW_ZOOMED; /* XXX: fix locking */
      break;

    case MI_IMAGE:
      browser_slideshow(b, bd, be);
      break;
    }
    break;
  }
}

static void
browser_enter_by_name(appi_t *ai, browser_t *b, char *name)
{
  b_dir_t *bd = TAILQ_FIRST(&b->b_dir_stack);
  glw_t *c, *w = bd->bd_list;
  b_entry_t *be;

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    be = glw_get_opaque(c, browser_entry_callback);
    if(!strcasecmp(be->be_filename, name))
      break;
  }

  if(c == NULL)
    return;

  w->glw_selected = c;
  browser_click(ai, b, 0);
}



static void
browser_rotate(browser_t *b)
{
  b_entry_t *be;
  b_dir_t *bd;
  glw_t *w;

  bd = TAILQ_FIRST(&b->b_dir_stack);
  w = bd->bd_list->glw_selected;
  if(w == NULL)
    return;
  be = glw_get_opaque(w, browser_entry_callback);
  if(be->be_image == NULL)
    return;

  glw_set(be->be_image, GLW_ATTRIB_ANGLE_DELTA, 90.0, NULL);

}



static int
browser_back(browser_t *b)
{
  b_dir_t *bd;

  pthread_mutex_lock(&b->b_lock);

  bd = TAILQ_FIRST(&b->b_dir_stack);

  if(TAILQ_NEXT(bd, bd_link) == NULL) {
    pthread_mutex_unlock(&b->b_lock);
    return 0;
  }

  TAILQ_REMOVE(&b->b_dir_stack, bd, bd_link);

  pthread_mutex_unlock(&b->b_lock);

  browser_free_dir(bd);

  bd = TAILQ_FIRST(&b->b_dir_stack);
  bd->bd_list->glw_flags &= ~GLW_ZOOMED; /* XXX: fix locking */

  return 1;
}





void
browser(appi_t *ai, char *rootpath)
{
  browser_t *b;
  b_dir_t *bd;
  inputevent_t ie;
  pthread_t ptid;

  b = calloc(1, sizeof(browser_t));
  b->b_ai = ai;
  ai->ai_browser = b;
  b->b_rootpath = rootpath;
  pthread_mutex_init(&b->b_lock, NULL);

  TAILQ_INIT(&b->b_dir_stack);

  printf("browser starting up %s\n", rootpath);

  ai->ai_widget = glw_create(GLW_CONTAINER, NULL);

  TAILQ_INIT(&b->b_peep_queue);
  pthread_mutex_init(&b->b_peep_lock, NULL);
  pthread_cond_init(&b->b_peep_cond, NULL);

  pthread_create(&ptid, NULL, peep_thread, b);

  browser_load_dir(b, NULL, b->b_rootpath, 0, ai->ai_widget);

  ai->ai_visible = 1;

  while(1) {
 
    input_getevent(&ai->ai_ic, 1, &ie, NULL);

    bd = TAILQ_FIRST(&b->b_dir_stack);
    
    switch(ie.type) {
    default:
      break;

    case INPUT_KEY:

      switch(ie.u.key) {

      case INPUT_KEY_NEXT:
      case INPUT_KEY_PREV:
      case INPUT_KEY_RESTART_TRACK:
      case INPUT_KEY_SEEK_FORWARD:
      case INPUT_KEY_SEEK_BACKWARD:
      case INPUT_KEY_STOP:
      case INPUT_KEY_PAUSE:
      case INPUT_KEY_PLAY:
      case INPUT_KEY_PLAYPAUSE:
      case INPUT_KEY_EJECT:
	playlist_eventstrike(&ie);
	break;

      case INPUT_KEY_UP:
	glw_nav_signal(bd->bd_list, GLW_SIGNAL_UP);
	break;

      case INPUT_KEY_DOWN:
	glw_nav_signal(bd->bd_list, GLW_SIGNAL_DOWN);
	break;

      case INPUT_KEY_LEFT:
	glw_nav_signal(bd->bd_list, GLW_SIGNAL_LEFT);
	break;

      case INPUT_KEY_RIGHT:
	glw_nav_signal(bd->bd_list, GLW_SIGNAL_RIGHT);
	break;

      case INPUT_KEY_ENTER:
	browser_click(ai, b, 0);
	break;

      case INPUT_KEY_SELECT:
	browser_click(ai, b, 1);
	break;

      case INPUT_KEY_ROTATE:
	browser_rotate(b);
	break;

      case INPUT_KEY_BACK:
	if(browser_back(b) == 0)
      case INPUT_KEY_CLOSE:
	  layout_hide(ai);
	break;

      case INPUT_KEY_INCR:
	if(bd->bd_list->glw_show_childs < 3)
	  break;

	glw_set(bd->bd_list, 
		GLW_ATTRIB_SHOW_CHILDS, bd->bd_list->glw_show_childs - 2,
		NULL);
	break;

      case INPUT_KEY_DECR:
	if(bd->bd_list->glw_show_childs >= 15)
	  break;

	glw_set(bd->bd_list, 
		GLW_ATTRIB_SHOW_CHILDS, bd->bd_list->glw_show_childs + 2,
		NULL);
	break;

      case INPUT_KEY_GOTO_MUSIC:
	while(browser_back(b)) {}
	browser_enter_by_name(ai, b, "music");
	break;

      case INPUT_KEY_GOTO_PHOTO:
	while(browser_back(b)) {}
	browser_enter_by_name(ai, b, "photos");
	break;

      case INPUT_KEY_GOTO_MOVIES:
	while(browser_back(b)) {}
	browser_enter_by_name(ai, b, "video");
	break;


      default:
	break;
      }
      break;
    }
  }
  
  free(b);
}


static void *
browser_start(void *aux)
{
  browser(aux, "/storage/media/");
  return NULL;
}


static void
browser_spawn(appi_t *ai)
{
  pthread_create(&ai->ai_tid, NULL, browser_start, ai);
}

/********************************
 *
 * Directory content preview
 *
 */

static int
peep_into_dir(glw_t *p, const char *path, int maxcnt, int r)
{
  DIR *dir;
  char tmp[500];
  char tmp2[500];
  struct dirent *d;
  int x = 0;
  int v = 5;
  mediainfo_t mi;
  if(r == 2)
    return 0;

  if((dir = opendir(path)) != NULL) {

    while(v > 0 && x < maxcnt && (d = readdir(dir)) != NULL) {
      if(d->d_name[0] == '.')
	continue;

      if(!strncasecmp(d->d_name, "albumart", 8))
	continue;

      snprintf(tmp, sizeof(tmp), "%s/%s", path, d->d_name);

      if(!strcasecmp(d->d_name, "folder.jpg")) {
	glw_create(GLW_BITMAP, 
		   GLW_ATTRIB_PARENT, p,
		   GLW_ATTRIB_FILENAME, tmp,
		   GLW_ATTRIB_FLAGS, GLW_BORDER_BLEND,
		   GLW_ATTRIB_BORDER_WIDTH, 0.05,
		   NULL);
	x++;
	continue;
      }

      if(d->d_type == DT_DIR || d->d_type == DT_LNK) {
	x += peep_into_dir(p, tmp, maxcnt - x, r + 1);
	v--;
      } else {
	/* If there are any other images in the first level directory, 
	   include them */
	if(r == 0 && !mediaprobe(tmp, &mi, 1) && mi.mi_type == MI_IMAGE) {
	  snprintf(tmp2, sizeof(tmp2), "thumb://%s/%s", path, d->d_name);

	  glw_create(GLW_BITMAP, 
		     GLW_ATTRIB_PARENT, p,
		     GLW_ATTRIB_FILENAME, tmp2,
		     GLW_ATTRIB_FLAGS, GLW_BORDER_BLEND,
		     GLW_ATTRIB_BORDER_WIDTH, 0.05,
		     NULL);
	  x++;
	  continue;
	}
      }
    }
    closedir(dir);
  }
  return x;
}



static void
peep_work_deref(peep_work_t *pw)
{
  if(pw->pw_refcount > 1) {
    pw->pw_refcount--;
    return;
  }
  free((void *)pw->pw_path);
  free(pw);
}



static void *
peep_thread(void *aux)
{
  browser_t *b = aux;
  peep_work_t *pw;
  glw_t *c, *x;
  int v;

  pthread_mutex_lock(&b->b_peep_lock);

  while(1) {
    
    while((pw = TAILQ_FIRST(&b->b_peep_queue)) == NULL)
      pthread_cond_wait(&b->b_peep_cond, &b->b_peep_lock);
    
    pthread_mutex_unlock(&b->b_peep_lock);
    
    if(pw->pw_container != NULL) {
#if 0
      c = glw_create(GLW_BITMAP,
		     GLW_ATTRIB_FILENAME, "icon://plate.png",
		     GLW_ATTRIB_FLAGS, GLW_NOASPECT,
		     GLW_ATTRIB_COLOR, GLW_COLOR_BLACK,
		     GLW_ATTRIB_ALPHA, 0.7,
		     NULL);
#endif


      x = glw_create(GLW_CONTAINER_X, 
		     //		     GLW_ATTRIB_PARENT, c,
		     NULL);
      c = x;
      v = peep_into_dir(x, pw->pw_path, 4, 0);

      if(v == 0)
	glw_create(GLW_BITMAP, 
		   GLW_ATTRIB_PARENT, pw->pw_container,
		   GLW_ATTRIB_FILENAME, "icon://dir.png",
		   NULL);

      if(v > 0 && pw->pw_container != NULL)
	glw_set(c, GLW_ATTRIB_PARENT, pw->pw_container, NULL);
      else
	glw_destroy(c);
    }

    pthread_mutex_lock(&b->b_peep_lock);
    TAILQ_REMOVE(&b->b_peep_queue, pw, pw_link);
    peep_work_deref(pw);
  }
}


static int 
peep_widget_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  peep_work_t *pw = opaque;
  browser_t *b = pw->pw_b;
  va_list ap;
  va_start(ap, signal);

  switch(signal) {
  default:
    break;
  case GLW_SIGNAL_DESTROY:
    pthread_mutex_lock(&b->b_peep_lock);
    pw->pw_container = NULL;
    peep_work_deref(pw);
    pthread_mutex_unlock(&b->b_peep_lock);
    break;
  }
  va_end(ap);
  return 0;
}


static void
peep_add(browser_t *b, const char *path, glw_t *parent, float weight)
{
  peep_work_t *pw = malloc(sizeof(peep_work_t));

  pw->pw_path = strdup(path);
  pw->pw_b = b;
  pw->pw_refcount = 2;

  pw->pw_container =
    glw_create(GLW_CONTAINER_X,
	       GLW_ATTRIB_WEIGHT, weight,
	       GLW_ATTRIB_PARENT, parent,
	       GLW_ATTRIB_SIGNAL_HANDLER, peep_widget_callback, pw, 0,
	       NULL);

  pthread_mutex_lock(&b->b_peep_lock);
  TAILQ_INSERT_TAIL(&b->b_peep_queue, pw, pw_link);
  pthread_cond_signal(&b->b_peep_cond);
  pthread_mutex_unlock(&b->b_peep_lock);
}



app_t app_fb = {
  .app_name = "Media library",
  .app_icon = "icon://library.png",
  .app_spawn = browser_spawn,
};




/********************************
 *
 * Image slideshow
 *
 */

#define SLIDE_DISTANCE 2.5


static void
slideshow_layout(b_dir_t *bd, glw_rctx_t *rc)
{
  b_entry_t *be;
  glw_vertex_t xyz;
  float v;

  glw_vertex_anim_fwd(&bd->bd_s_pos, 0.03);
  glw_vertex_anim_read(&bd->bd_s_pos, &xyz);

  TAILQ_FOREACH(be, &bd->bd_images, be_image_link) {
    v = be->be_xpos - xyz.x;
    if(fabs(v) < SLIDE_DISTANCE * 2) {
      if(fabs(v) < 0.1)
	bd->bd_s_cur = be;
      glw_layout(be->be_image, rc);
    }
  }

  glw_layout(bd->bd_s_saved, rc);
}



static void
slideshow_render(b_dir_t *bd, glw_rctx_t *rc)
{
  b_entry_t *be;
  glw_vertex_t xyz;
  float v;

  glw_vertex_anim_read(&bd->bd_s_pos, &xyz);

  TAILQ_FOREACH(be, &bd->bd_images, be_image_link) {
    v = be->be_xpos - xyz.x;
    if(fabs(v) < SLIDE_DISTANCE * 2) {
      glPushMatrix();
      glTranslatef(v, 0, 0);
      glw_render(be->be_image, rc);
      glPopMatrix();
    }
  }
}


static int
slideshow_ext_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  b_dir_t *bd = opaque;
  inputevent_t *ie;
  va_list ap;
  va_start(ap, signal);

  switch(signal) {
  case GLW_SIGNAL_RENDER:
    slideshow_render(bd, va_arg(ap, void *));
    return 0;

  case GLW_SIGNAL_LAYOUT:
    slideshow_layout(bd, va_arg(ap, void *));
    return 0;

  case GLW_SIGNAL_INPUT_EVENT:
    ie = va_arg(ap, void *);
    input_postevent(&bd->bd_b->b_ai->ai_ic, ie);
    return 1;

  default:
    return 0;
  }
  va_end(ap);
  return 0;
}


static void
browser_slideshow(browser_t *b, b_dir_t *bd, b_entry_t *be)
{
  appi_t *ai = b->b_ai;
  char tmp[500];
  glw_t *saved;
  float xpos = 0.0f;
  float curx = 0.0f;
  int run = 1;
  float xpos_max;
  inputevent_t ie;

  ai->ai_req_fullscreen = AI_FS_BLANK;

  be = be ?: TAILQ_FIRST(&bd->bd_images);
  saved = bd->bd_s_saved = ai->ai_widget;
  bd->bd_s_cur = be;

  /* Create full scale images, notice that the actual load wont take
     place until the widget is layouted */

  TAILQ_FOREACH(be, &bd->bd_images, be_image_link) {
    snprintf(tmp, sizeof(tmp), "%s/%s",  bd->bd_name, be->be_filename);
    be->be_xpos = xpos;
    be->be_image = glw_create(GLW_BITMAP, 
			      GLW_ATTRIB_FLAGS, GLW_BORDER_BLEND,
			      GLW_ATTRIB_BORDER_WIDTH, 0.01,
			      GLW_ATTRIB_FILENAME, tmp,
			      NULL);
    if(be == bd->bd_s_cur) {
      curx = xpos;
    }

    xpos += SLIDE_DISTANCE;
  }

  xpos_max = xpos - SLIDE_DISTANCE;
  glw_vertex_anim_init(&bd->bd_s_pos, curx, 0, 0, GLW_VERTEX_ANIM_SIN_LERP);

  ai->ai_widget =
    glw_create(GLW_EXT,
	       GLW_ATTRIB_SIGNAL_HANDLER, slideshow_ext_callback, bd, 0,
	       NULL);

  while(run) {
    input_getevent(&ai->ai_ic, 1, &ie, NULL);

    switch(ie.type) {
    default:
      break;

    case INPUT_KEY:

      switch(ie.u.key) {

      case INPUT_KEY_RIGHT:
	curx = GLW_MIN(curx + SLIDE_DISTANCE, xpos_max);
	glw_vertex_anim_set3f(&bd->bd_s_pos, curx, 0, 0);
	break;

      case INPUT_KEY_LEFT:
	curx = GLW_MAX(curx - SLIDE_DISTANCE, 0);
	glw_vertex_anim_set3f(&bd->bd_s_pos, curx, 0, 0);
	break;

      case INPUT_KEY_BACK:
	if(browser_back(b) == 0)
      case INPUT_KEY_CLOSE:
	  layout_hide(ai);
      case INPUT_KEY_SELECT:
	run = 0;
	break;

      default:
	break;
      }
      break;
    }
  }

  ai->ai_req_fullscreen = 0;

  glw_lock();
  glw_destroy(ai->ai_widget);
  ai->ai_widget = saved;
  glw_unlock();

  bd->bd_list->glw_selected = bd->bd_s_cur->be_widget;
}
