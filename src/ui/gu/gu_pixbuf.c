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
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>

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
  av_free(pixels);
}

/**
 *
 */
static GdkPixbuf *
gu_pixbuf_get_internal(const char *url, int *sizep,
		       int req_width, int req_height, int relock)
{
  AVCodecContext *ctx = NULL;
  AVCodec *codec;
  AVFrame *frame = NULL;
  AVPicture *src, dst;
  struct SwsContext *sws;
  GdkPixbuf *gp;
  int r;
  int got_pic;
  enum PixelFormat outfmt;
  enum PixelFormat pixfmt;
  int width;
  int height;
  const uint8_t *ptr[4];
  image_meta_t im = {0};

  if(!strncmp(url, "thumb://", 8)) {
    url = url + 8;
    im.want_thumb = 1;
  }

  im.req_width  = req_width;
  im.req_height = req_height;
  
  pixmap_t *pm = backend_imageloader(url, &im, NULL, NULL, 0);
  if(pm == NULL)
    return NULL;

  if(!pixmap_is_coded(pm)) {
    pixmap_release(pm);
    return NULL;

  } else {
    
    switch(pm->pm_type) {
    case PIXMAP_PNG:
      codec = avcodec_find_decoder(CODEC_ID_PNG);
      break;
    case PIXMAP_JPEG:
      codec = avcodec_find_decoder(CODEC_ID_MJPEG);
      break;
    case PIXMAP_GIF:
      codec = avcodec_find_decoder(CODEC_ID_GIF);
      break;
    default:
      codec = NULL;
      break;
    }
    if(codec == NULL) {
      pixmap_release(pm);
      return NULL;
    }  
    ctx = avcodec_alloc_context();

    ctx->codec_id   = codec->id;
    ctx->codec_type = codec->type;

    if(avcodec_open(ctx, codec) < 0) {
      av_free(ctx);
      pixmap_release(pm);
      return NULL;
    }
    
    frame = avcodec_alloc_frame();

    AVPacket avpkt;
    av_init_packet(&avpkt);
    avpkt.data = pm->pm_data;
    avpkt.size = pm->pm_size;

    r = avcodec_decode_video2(ctx, frame, &got_pic, &avpkt);
    if(r < 0) {
      av_free(frame);
      av_free(ctx);
      pixmap_release(pm);
      return NULL;
    }
    height = ctx->height;
    width  = ctx->width;
    pixfmt = ctx->pix_fmt;

    src = (AVPicture *)frame;
  }

  if(req_width == -1 && req_height == -1) {
    req_width  = width; 
    req_height = height;
  } else if(req_width == -1) {
    req_width = width * req_height / height;
  } else if(req_height == -1) {
    req_height = height * req_width / width;
  } 

  outfmt = isALPHA(pixfmt) ? PIX_FMT_RGBA : PIX_FMT_RGB24;

  sws = sws_getContext(width, height, pixfmt, 
		       req_width, req_height, outfmt,
		       SWS_BICUBIC, NULL, NULL, NULL);
  if(sws == NULL) {
    av_free(frame);
    av_free(ctx);
    pixmap_release(pm);
    return NULL;
  }

  avpicture_alloc(&dst, outfmt, req_width, req_height);

  ptr[0] = src->data[0];
  ptr[1] = src->data[1];
  ptr[2] = src->data[2];
  ptr[3] = src->data[3];

  sws_scale(sws, ptr, src->linesize, 0, height,
	    dst.data, dst.linesize);
  
  sws_freeContext(sws);

  if(sizep != NULL)
    *sizep = dst.linesize[0] * req_height;


  if(relock)
    hts_mutex_lock(&gu_mutex);

  gp = gdk_pixbuf_new_from_data(dst.data[0],
				GDK_COLORSPACE_RGB,
				isALPHA(pixfmt),
				8,
				req_width,
				req_height,
				dst.linesize[0],
				pixbuf_avfree_pixels,
				dst.data[0]);

  if(frame != NULL)
    av_free(frame);

  if(ctx != NULL) {
    avcodec_close(ctx);
    av_free(ctx);
  }

  pixmap_release(pm);
  return gp;
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
  int size;

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
  int size;
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
