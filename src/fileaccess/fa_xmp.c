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
#include <xmp.h>

#include "main.h"
#include "backend/backend.h"
#include "fileaccess.h"
#include "fa_proto.h"
#include "fa_audio.h"
#include "fa_libav.h"
#include "misc/str.h"
#include "media/media.h"


/**
 *
 */
event_t *
fa_xmp_playfile(media_pipe_t *mp, FILE *f, char *errbuf, size_t errlen,
                int hold, const char *url, size_t size)
{
  event_t *e = NULL;
  xmp_context ctx = xmp_create_context();
  //  struct xmp_module_info mi;
  struct xmp_frame_info fi;
  char *u = mystrdupa(url);

  mp->mp_audio.mq_stream = 0;
  mp_configure(mp, MP_CAN_PAUSE,
	       MP_BUFFER_SHALLOW, 0, "tracks");
  mp_become_primary(mp);

  if(xmp_load_modulef(ctx, f, u, size) >= 0) {
    if(xmp_start_player(ctx, 44100, 0) == 0) {

      media_buf_t *mb = NULL;
      media_queue_t *mq = &mp->mp_audio;

      while(1) {

        if(mb == NULL) {

          if(xmp_play_frame(ctx)) {
            e = event_create_type(EVENT_EOF);
            break;
          }
          xmp_get_frame_info(ctx, &fi);
          if(fi.loop_count > 0) {
            e = event_create_type(EVENT_EOF);
            break;
          }

          mb = media_buf_alloc_unlocked(mp, fi.buffer_size);
          mb->mb_data_type = MB_AUDIO;
          mb->mb_channels = 2;
          mb->mb_rate = 44100;
          mb->mb_pts = fi.time * 1000;
          mb->mb_drive_clock = 1;
          memcpy(mb->mb_data, fi.buffer, fi.buffer_size);
	  mp_set_duration(mp, fi.total_time * 1000);
        }

        if((e = mb_enqueue_with_events(mp, mq, mb)) == NULL) {
          mb = NULL; /* Enqueue succeeded */
          continue;
        }

        if(event_is_type(e, EVENT_PLAYQUEUE_JUMP)) {
          mp_flush(mp);
          break;

        } else if(event_is_action(e, ACTION_SKIP_BACKWARD) ||
                  event_is_action(e, ACTION_SKIP_FORWARD) ||
                  event_is_action(e, ACTION_STOP)) {
          mp_flush(mp);
          break;
        }
        event_release(e);
      }
      xmp_end_player(ctx);
    } else {
      snprintf(errbuf, errlen, "XMP failed to start");
    }
  } else {
    snprintf(errbuf, errlen, "XMP Loading error");
  }
  //  prop_ref_dec(dur);
  xmp_free_context(ctx);
  return e;
}
