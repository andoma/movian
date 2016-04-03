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

#include <unistd.h>

#include <libavformat/avio.h>
#include <libavformat/avformat.h>

#include "main.h"
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

  int lazy = !(whence & AVSEEK_FORCE);
  return fa_seek4(fh, offset, whence & ~AVSEEK_FORCE, lazy);
}


/**
 *
 */
AVIOContext *
fa_libav_reopen(fa_handle_t *fh, int no_seek)
{
  AVIOContext *avio;

  int seekable = !no_seek && fa_fsize(fh) != -1;

  if(seekable)
    if(fa_seek(fh, 0, SEEK_SET) != 0)
      return NULL;

  int buf_size = 32768;
  void *buf = av_malloc(buf_size);

  avio = avio_alloc_context(buf, buf_size, 0, fh, fa_libav_read, NULL, 
			    fa_libav_seek);
  if(avio != NULL && !seekable)
    avio->seekable = 0;

  return avio;
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
  { "video/MP2T", "mpegts" },
  { "video/mpeg", "mpegts" },
  { "video/vnd.dlna.mpeg-tts", "mpegts" },
  { "video/avi", "avi" },
  { "video/nsv", "nsv" },
  { "video/webm", "webm" },
  { "audio/x-mpeg", "mp3" },
  { "audio/mpeg", "mp3" },
  { "application/ogg", "ogg" },
  { "audio/aac", "aac" },
  { "audio/aacp", "aac" },
};


/**
 *
 */
AVFormatContext *
fa_libav_open_format(AVIOContext *avio, const char *url,
		     char *errbuf, size_t errlen, const char *mimetype,
                     int probe_size, int max_analyze_duration,
		     int fps_probe_frames)
{
  AVInputFormat *fmt = NULL;
  AVFormatContext *fctx;
  int err;

  avio_seek(avio, 0, SEEK_SET);
  if(mimetype != NULL) {
    int i;

    for(i = 0; i < sizeof(mimetype2fmt) / sizeof(mimetype2fmt[0]); i++) {
      if(!strcasecmp(mimetype, mimetype2fmt[i].mimetype)) {
	fmt = av_find_input_format(mimetype2fmt[i].fmt);
	break;
      }
    }
    if(fmt == NULL)
      TRACE(TRACE_DEBUG, "probe", "%s: Don't know mimetype %s, probing instead",
	    url, mimetype);
  }

  if(fmt == NULL) {
    if((err = av_probe_input_buffer(avio, &fmt, url, NULL, 0, probe_size)) != 0)
      return fa_libav_open_error(errbuf, errlen,
				 "Unable to probe file", err);

    if(fmt == NULL) {
      snprintf(errbuf, errlen, "Unknown file format");
      return NULL;
    }
    TRACE(TRACE_DEBUG, "probe", "%s: Probed as %s", url, fmt->name);
  }

  fctx = avformat_alloc_context();
  fctx->pb = avio;

  if((err = avformat_open_input(&fctx, url, fmt, NULL)) != 0) {
    if(mimetype != NULL) {
      TRACE(TRACE_DEBUG, "libav",
            "Unable to open using mimetype %s, retrying with probe",
            mimetype);
      return fa_libav_open_format(avio, url, errbuf, errlen, NULL, probe_size,
                                  max_analyze_duration, fps_probe_frames);
    }
    return fa_libav_open_error(errbuf, errlen,
			       "Unable to open file as input format", err);
  }

  if(fps_probe_frames != -1)
    fctx->fps_probe_size = fps_probe_frames;

  if(max_analyze_duration != -1)
    fctx->max_analyze_duration = max_analyze_duration;

  if(avformat_find_stream_info(fctx, NULL) < 0) {
    avformat_close_input(&fctx);
    if(mimetype != NULL) {
      TRACE(TRACE_DEBUG, "libav",
            "Unable to find stream info using mimetype %s, retrying with probe",
            mimetype);
      return fa_libav_open_format(avio, url, errbuf, errlen, NULL, probe_size,
                                  max_analyze_duration, fps_probe_frames);
    }
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
  av_free(avio->buffer);
  av_free(avio);
}


/**
 *
 */
void
fa_libav_close_format(AVFormatContext *fctx)
{
  AVIOContext *avio = fctx->pb;
  avformat_close_input(&fctx);
  fa_libav_close(avio);
}


/**
 *
 */
void
fa_libav_error_to_txt(int err, char *errbuf, size_t errlen)
{
  
if(av_strerror(err, errbuf, errlen))
    snprintf(errbuf, errlen, "libav error %d", err);
}

