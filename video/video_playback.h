/*
 *  Video playback
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

#ifndef VIDEO_PLAYBACK_H
#define VIDEO_PLAYBACK_H

#include "media.h"

typedef struct video_playback {

  hts_thread_t vp_thread;

  media_pipe_t *vp_mp;

  //  char *vp_url;

  //  AVFormatContext *vp_fctx;

  //  codecwrap_t **vp_cwvec;
#if 0
  int64_t pvc_rcache_last;

  char *pvc_rcache_title;

  int pvc_force_status_display;

  prop_t *pvc_prop_root;
  prop_t *pvc_prop_playstatus;

  prop_t *pvc_prop_videoinfo;
  prop_t *pvc_prop_audioinfo;
#endif
} video_playback_t;


video_playback_t *video_playback_create(media_pipe_t *mp);

void video_playback_destroy(video_playback_t *vp);

#endif /* PLAY_VIDEO_H */
