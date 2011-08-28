/*
 *  File access <-> AVIOContext
 *  Copyright (C) 2011 Andreas Öman
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
#include "showtime.h"
#include "fa_libav.h"

/**
 *
 */
static int
fa_libav_read(void *opaque, uint8_t *buf, int size)
{
  fa_handle_t *fh = opaque;
  return fa_read(fh, buf, size);
}


/**
 *
 */
static int64_t
fa_libav_seek(void *opaque, int64_t offset, int whence)
{
  fa_handle_t *fh = opaque;
  if(whence == AVSEEK_SIZE)
    return fa_fsize(fh);

  return fa_seek(fh, offset, whence & ~AVSEEK_FORCE);
}


/**
 *
 */
AVIOContext *
fa_libav_reopen(fa_handle_t *fh, int buf_size)
{
  AVIOContext *avio;

  if(buf_size == 0)
    buf_size = 32768;
  void *buf = malloc(buf_size);

  avio = avio_alloc_context(buf, buf_size, 0, fh, fa_libav_read, NULL, 
			    fa_libav_seek);
  if(fa_fsize(fh) == -1)
    avio->seekable = 0;
  return avio;
}


/**
 *
 */
AVIOContext *
fa_libav_open(const char *url, int buf_size, char *errbuf, size_t errlen,
	      int flags, struct prop *stats)
{
  fa_handle_t *fh;

  if((fh = fa_open_ex(url, errbuf, errlen, flags, stats)) == NULL)
    return NULL;
  return fa_libav_reopen(fh, buf_size);
}


/**
 *
 */
AVIOContext *
fa_libav_open_vpaths(const char *url, int buf_size, const char **vpaths)
{
  fa_handle_t *fh;

  if((fh = fa_open_vpaths(url, vpaths)) == NULL)
    return NULL;
  return fa_libav_reopen(fh, buf_size);
}


/**
 *
 */
static AVFormatContext *
fa_libav_open_error(char *errbuf, size_t errlen, const char *hdr, int errcode)
{
  char libaverr[256];

  if(av_strerror(errcode, libaverr, sizeof(libaverr)))
    snprintf(libaverr, sizeof(libaverr), "libav error %d", errcode);
  
  snprintf(errbuf, errlen, "%s: %s", hdr, libaverr);
  return NULL;
}

/**
 *
 */
static const struct {
  const char *mimetype;
  const char *fmt;
} mimetype2fmt[] = {
  { "video/x-matroska", "matroska" },
  { "video/quicktime", "mov" },
  { "video/mp4", "mp4" },
  { "video/x-msvideo", "avi" },
  { "video/vnd.dlna.mpeg-tts,", "mpegts" },
  { "video/avi", "avi" },
  { "audio/x-mpeg", "mp3" },
};


/**
 *
 */
AVFormatContext *
fa_libav_open_format(AVIOContext *avio, const char *url, 
		     char *errbuf, size_t errlen, const char *mimetype)
{
  AVInputFormat *fmt = NULL;
  AVFormatContext *fctx;
  int err;

  avio_seek(avio, 0, SEEK_SET);
  if(mimetype != NULL) {
    int i;

    for(i = 0; i < sizeof(mimetype2fmt) / sizeof(mimetype2fmt[0]); i++) {
      if(!strcmp(mimetype, mimetype2fmt[i].mimetype)) {
	fmt = av_find_input_format(mimetype2fmt[i].fmt);
	break;
      }
    }
    if(fmt == NULL)
      TRACE(TRACE_DEBUG, "probe", "Don't know mimetype %s, probing instead",
	    mimetype);
  }

  if(fmt == NULL) {

    if((err = av_probe_input_buffer(avio, &fmt, url, NULL, 0, 0)) != 0)
      return fa_libav_open_error(errbuf, errlen,
				 "Unable to probe file", err);

    if(fmt == NULL) {
      snprintf(errbuf, errlen, "Unknown file format");
      return NULL;
    }
  }

  fctx = avformat_alloc_context();
  fctx->pb = avio;

  if((err = avformat_open_input(&fctx, url, fmt, NULL)) != 0) {
    if(mimetype != NULL)
      return fa_libav_open_format(avio, url, errbuf, errlen, NULL);
    return fa_libav_open_error(errbuf, errlen,
			       "Unable to open file as input format", err);
  }

  if(av_find_stream_info(fctx) < 0) {
    av_close_input_stream(fctx);
    if(mimetype != NULL)
      return fa_libav_open_format(avio, url, errbuf, errlen, NULL);
    return fa_libav_open_error(errbuf, errlen,
			       "Unable to handle file contents", err);
  }

  return fctx;
}


/**
 *
 */
void
fa_libav_close(AVIOContext *avio)
{
  fa_close(avio->opaque);
  free(avio->buffer);
  av_free(avio);
}


/**
 *
 */
void
fa_libav_close_format(AVFormatContext *fctx)
{
  AVIOContext *avio = fctx->pb;
  av_close_input_stream(fctx);
  fa_libav_close(avio);
}


/**
 *
 */
uint8_t *
fa_libav_load_and_close(AVIOContext *avio, size_t *sizep)
{
  size_t r;
  size_t size = avio_size(avio);
  if(size == -1)
    return NULL;

  uint8_t *mem = malloc(size+1);

  avio_seek(avio, 0, SEEK_SET);
  r = avio_read(avio, mem, size);
  fa_libav_close(avio);

  if(r != size) {
    free(mem);
    return NULL;
  }

  if(sizep != NULL)
    *sizep = size;
  mem[size] = 0; 
  return mem;
}
