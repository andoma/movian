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

#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>

#include "navigator.h"
#include "gu.h"
#include "showtime.h"

#define PIXBUFCACHE_SIZE (10 * 1024 * 1024)

TAILQ_HEAD(pbcache_queue, pbcache);

static struct pbcache_queue pixbufcache;
static unsigned int pixbufcachesize;

typedef struct pbcache {
  TAILQ_ENTRY(pbcache) pbc_link;
  char *pbc_url;
  GdkPixbuf *pbc_pixbuf;
  int pbc_bytes;

} pbcache_t;


#define isALPHA(x)      (           \
           (x)==PIX_FMT_BGR32       \
        || (x)==PIX_FMT_BGR32_1     \
        || (x)==PIX_FMT_RGB32       \
        || (x)==PIX_FMT_RGB32_1     \
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
gu_pixbuf_get_internal(const char *url, int *sizep)
{
  AVCodecContext *ctx = NULL;
  AVCodec *codec;
  AVFrame *frame;
  AVPicture pic;
  struct SwsContext *sws;
  void *data;
  size_t datasize;
  int codecid;
  prop_pixmap_t *pp;
  int got_thumb, want_thumb;
  GdkPixbuf *gp;
  int r;
  int got_pic;
  int outfmt;
  int pixfmt;
  int width;
  int height;

  if(!strncmp(url, "thumb://", 8)) {
    url = url + 8;
    want_thumb = 1;
  } else {
    want_thumb = 0;
  }
  got_thumb = want_thumb;

  if(nav_imageloader(url, NULL, 0, &got_thumb,
		     &data, &datasize, &codecid, NULL, &pp))
    return NULL;

  frame = avcodec_alloc_frame();

  if(pp != NULL) {

    width = pp->pp_width;
    height = pp->pp_height;
    pixfmt = pp->pp_pixfmt;
    frame->data[0] = pp->pp_pixels;
    frame->linesize[0] = pp->pp_linesize;

  } else {
    
    ctx = avcodec_alloc_context();
    codec = avcodec_find_decoder(codecid);
  
    if(avcodec_open(ctx, codec) < 0) {
      av_free(frame);
      av_free(ctx);
      free(data);
      return NULL;
    }
  

    r = avcodec_decode_video(ctx, frame, &got_pic, data, datasize);
    if(r < 0) {
      av_free(frame);
      av_free(ctx);
      free(data);
      return NULL;
    }
    height = ctx->height;
    width  = ctx->width;
    pixfmt = ctx->pix_fmt;

  }

  outfmt = isALPHA(pixfmt) ? PIX_FMT_RGBA : PIX_FMT_RGB24;

  sws = sws_getContext(width, height, pixfmt, 
		       width, height, outfmt,
		       SWS_BICUBIC, NULL, NULL, NULL);


  avpicture_alloc(&pic, outfmt, width, height);

  sws_scale(sws, frame->data, frame->linesize, 0, height,
	    pic.data, pic.linesize);
  
  sws_freeContext(sws);

  if(sizep != NULL)
    *sizep = pic.linesize[0] * height;

  gp = gdk_pixbuf_new_from_data(pic.data[0],
				GDK_COLORSPACE_RGB,
				isALPHA(pixfmt),
				8,
				width,
				height,
				pic.linesize[0],
				pixbuf_avfree_pixels,
				pic.data[0]);

  av_free(frame);

  if(ctx != NULL) {
    avcodec_close(ctx);
    av_free(ctx);
  }

  if(pp != NULL) {
    prop_pixmap_ref_dec((prop_pixmap_t *)data);
  }

  return gp;
}


/**
 *
 */
GdkPixbuf *
gu_pixbuf_get_sync(const char *url)
{
  pbcache_t *pbc;
  GdkPixbuf *pb;
  int size;

  TAILQ_FOREACH(pbc, &pixbufcache, pbc_link) {
    if(!strcmp(pbc->pbc_url, url)) {
      /* Cache hit, move entry to front */
      TAILQ_REMOVE(&pixbufcache, pbc, pbc_link);
      TAILQ_INSERT_HEAD(&pixbufcache, pbc, pbc_link);

      /* We are assumed to give a reference */
      g_object_ref(G_OBJECT(pbc->pbc_pixbuf));
      return pbc->pbc_pixbuf;
    }
  }

  pb = gu_pixbuf_get_internal(url, &size);
  if(pb == NULL)
    return NULL;

  pixbufcachesize += size;

  while(pixbufcachesize > PIXBUFCACHE_SIZE && 
	(pbc = TAILQ_FIRST(&pixbufcache)) != NULL) {
    pixbufcachesize -= pbc->pbc_bytes;
    TAILQ_REMOVE(&pixbufcache, pbc, pbc_link);
    g_object_unref(G_OBJECT(pbc->pbc_pixbuf));
    free(pbc->pbc_url);
    free(pbc);
  }

  pbc = malloc(sizeof(pbcache_t));
  pbc->pbc_url = strdup(url);
  pbc->pbc_pixbuf = pb;
  pbc->pbc_bytes = size;

  /* Keep a reference for the cache */
  g_object_ref(G_OBJECT(pbc->pbc_pixbuf));

  TAILQ_INSERT_HEAD(&pixbufcache, pbc, pbc_link);
  return pbc->pbc_pixbuf;
}


/**
 *
 */
void
gu_pixbuf_init(void)
{
  TAILQ_INIT(&pixbufcache);
}
