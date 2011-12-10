/*
 *  Showtime GTK frontend
 *  Copyright (C) 2009 Andreas Öman
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
#include <assert.h>

#include "backend/backend.h"
#include "gu.h"
#include "showtime.h"
#include "misc/pixmap.h"

extern hts_mutex_t gu_mutex;
static hts_cond_t async_loader_cond;

#define PIXBUFCACHE_SIZE (10 * 1024 * 1024)

TAILQ_HEAD(pbcache_queue, pbcache);

static struct pbcache_queue pixbufcache;
static unsigned int pixbufcachesize;


typedef struct pbcache {
  TAILQ_ENTRY(pbcache) pbc_link;
  char *pbc_url;
  GdkPixbuf *pbc_pixbuf;
  int pbc_bytes;

  int pbc_width, pbc_height; // -1 == original

} pbcache_t;


#define isALPHA(x)      (           \
           (x)==PIX_FMT_ARGB        \
        || (x)==PIX_FMT_RGBA        \
        || (x)==PIX_FMT_ABGR        \
        || (x)==PIX_FMT_BGRA        \
        || (x)==PIX_FMT_YUVA420P    \
    )


#if 0
/**
 *
 */
static void 
pixbuf_free_prop_pixmap(guchar *pixels, gpointer data)
{
  prop_pixmap_ref_dec((prop_pixmap_t *)data);
}
#endif


/**
 *
 */
static void 
pixbuf_avfree_pixels(guchar *pixels, gpointer data)
{
  pixmap_release(data);
}

/**
 *
 */
static GdkPixbuf *
gu_pixbuf_get_internal(const char *url, int *sizep,
		       int req_width, int req_height, int relock)
{
  image_meta_t im = {0};

  im.im_req_width  = req_width;
  im.im_req_height = req_height;
  
  pixmap_t *pm = backend_imageloader(url, &im, NULL, NULL, 0, NULL);
  if(pm == NULL)
    return NULL;

  if(relock)
    hts_mutex_lock(&gu_mutex);

  return gdk_pixbuf_new_from_data(pm->pm_data,
				  GDK_COLORSPACE_RGB,
				  pm->pm_type == PIXMAP_BGR32,
				  8,
				  pm->pm_width,
				  pm->pm_height,
				  pm->pm_linesize,
				  pixbuf_avfree_pixels,
				  pm);
}


/**
 *
 */
static GdkPixbuf *
gu_pixbuf_get_from_cache(const char *url, int width, int height)
{
  pbcache_t *pbc;

  TAILQ_FOREACH(pbc, &pixbufcache, pbc_link) {
    if(!strcmp(pbc->pbc_url, url) &&
       pbc->pbc_width == width &&
       pbc->pbc_height == height) {
      /* Cache hit, move entry to front */
      TAILQ_REMOVE(&pixbufcache, pbc, pbc_link);
      TAILQ_INSERT_HEAD(&pixbufcache, pbc, pbc_link);

      return pbc->pbc_pixbuf;
    }
  }
  return NULL;
}


/**
 *
 */
static void
gu_pixbuf_flush_cache(void)
{
  pbcache_t *pbc;

  while(pixbufcachesize > PIXBUFCACHE_SIZE && 
	(pbc = TAILQ_FIRST(&pixbufcache)) != NULL) {
    pixbufcachesize -= pbc->pbc_bytes;
    TAILQ_REMOVE(&pixbufcache, pbc, pbc_link);
    g_object_unref(G_OBJECT(pbc->pbc_pixbuf));
    free(pbc->pbc_url);
    free(pbc);
  }

}


/**
 *
 */
static void
gu_pixbuf_add_to_cache(const char *url, int size, int width, int height,
		       GdkPixbuf *pb)
{
  pbcache_t *pbc;

  pixbufcachesize += size;

  gu_pixbuf_flush_cache();

  pbc = malloc(sizeof(pbcache_t));
  pbc->pbc_url = strdup(url);
  pbc->pbc_pixbuf = pb;
  pbc->pbc_bytes = size;
  pbc->pbc_width = width;
  pbc->pbc_height = height;
  TAILQ_INSERT_HEAD(&pixbufcache, pbc, pbc_link);
}



/**
 *
 */
GdkPixbuf *
gu_pixbuf_get_sync(const char *url, int width, int height)
{
  GdkPixbuf *pb;
  int size = 0;

  pb = gu_pixbuf_get_from_cache(url, width, height);
  if(pb != NULL) {
    /* We are assumed to give a reference */
    g_object_ref(G_OBJECT(pb));
    return pb;
  }

  pb = gu_pixbuf_get_internal(url, &size, width, height, 0);
  if(pb == NULL)
    return NULL;

  gu_pixbuf_add_to_cache(url, size, width, height, pb);

  /* We are assumed to give a reference */
  g_object_ref(G_OBJECT(pb));

  return pb;
}

TAILQ_HEAD(pb_asyncload_queue, pb_asyncload);

static struct pb_asyncload_queue pbaqueue;

/**
 *
 */
typedef struct pb_asyncload {
  TAILQ_ENTRY(pb_asyncload) pba_link;

  char *pba_url;
  int pba_width;
  int pba_height;

  GtkObject *pba_target;
} pb_asyncload_t;


/**
 *
 */
void
gu_pixbuf_async_set(const char *url, int width, int height, GtkObject *target)
{
  pb_asyncload_t *pba;
  GdkPixbuf *pb;
  
  pb = gu_pixbuf_get_from_cache(url, width, height);
  if(pb != NULL) {
    g_object_set(G_OBJECT(target), "pixbuf", pb, NULL);
    return;
  }

  pba = calloc(1, sizeof(pb_asyncload_t));
  
  pba->pba_url = strdup(url);
  pba->pba_width = width;
  pba->pba_height = height;
  pba->pba_target = target;
  g_object_ref(target);

  TAILQ_INSERT_TAIL(&pbaqueue, pba, pba_link);
  hts_cond_signal(&async_loader_cond);
}


/**
 *
 */
static void *
pixbuf_loader_thread(void *aux)
{
  pb_asyncload_t *pba;
  int size = 0;
  GdkPixbuf *pb;

  hts_mutex_lock(&gu_mutex);

  while(1) {

    while((pba = TAILQ_FIRST(&pbaqueue)) == NULL)
      hts_cond_wait(&async_loader_cond, &gu_mutex);
    
    TAILQ_REMOVE(&pbaqueue, pba, pba_link);

    pb = gu_pixbuf_get_from_cache(pba->pba_url, pba->pba_width, 
				  pba->pba_height);
    if(pb != NULL) {
      g_object_set(G_OBJECT(pba->pba_target), "pixbuf", pb, NULL);
    } else {
      hts_mutex_unlock(&gu_mutex);

      pb = gu_pixbuf_get_internal(pba->pba_url, &size, pba->pba_width, 
				  pba->pba_height, 1);
      if(pb == NULL) {
	hts_mutex_lock(&gu_mutex);
      } else {

	gu_pixbuf_add_to_cache(pba->pba_url, size, pba->pba_width, 
			       pba->pba_height, pb);

	g_object_set(G_OBJECT(pba->pba_target), "pixbuf", pb, NULL);
      }
    }
    g_object_unref(pba->pba_target);
    free(pba->pba_url);
    free(pba);
  }
  
  return NULL;
}


/**
 *
 */
void
gu_pixbuf_init(void)
{
  TAILQ_INIT(&pbaqueue);
  TAILQ_INIT(&pixbufcache);

  hts_thread_create_detached("GDK pixbuf loader", 
			     pixbuf_loader_thread, NULL,
			     THREAD_PRIO_NORMAL);
}
