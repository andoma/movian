/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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

#include "media.h"

#if ENABLE_LIBAV


/**
 *
 */
void
media_buf_dtor_frame_info(media_buf_t *mb)
{
  free(mb->mb_frame_info);
}

/**
 *
 */
static void
media_buf_dtor_avpacket(media_buf_t *mb)
{
  av_packet_unref(&mb->mb_pkt);
}

#define BUF_PAD 32

media_buf_t *
media_buf_alloc_locked(media_pipe_t *mp, size_t size)
{
  hts_mutex_assert(&mp->mp_mutex);
  media_buf_t *mb = pool_get(mp->mp_mb_pool);
  av_new_packet(&mb->mb_pkt, size);
  mb->mb_dtor = media_buf_dtor_avpacket;
  return mb;
}


media_buf_t *
media_buf_alloc_unlocked(media_pipe_t *mp, size_t size)
{
  media_buf_t *mb;
  hts_mutex_lock(&mp->mp_mutex);
  mb = media_buf_alloc_locked(mp, size);
  hts_mutex_unlock(&mp->mp_mutex);
  return mb;
}


/**
 *
 */
media_buf_t *
media_buf_from_avpkt_unlocked(media_pipe_t *mp, AVPacket *pkt)
{
  media_buf_t *mb;

  hts_mutex_lock(&mp->mp_mutex);
  mb = pool_get(mp->mp_mb_pool);
  hts_mutex_unlock(&mp->mp_mutex);

  mb->mb_dtor = media_buf_dtor_avpacket;

  av_packet_ref(&mb->mb_pkt, pkt);
  return mb;
}


/**
 *
 */
void
copy_mbm_from_mb(media_buf_meta_t *mbm, const media_buf_t *mb)
{
  mbm->mbm_delta     = mb->mb_delta;
  mbm->mbm_pts       = mb->mb_pts;
  mbm->mbm_dts       = mb->mb_dts;
  mbm->mbm_epoch     = mb->mb_epoch;
  mbm->mbm_duration  = mb->mb_duration;
  mbm->mbm_flags.u32 = mb->mb_flags.u32;
}

#endif

/**
 *
 */
void
media_buf_free_locked(media_pipe_t *mp, media_buf_t *mb)
{
  if(mb->mb_dtor != NULL)
    mb->mb_dtor(mb);

  if(mb->mb_cw != NULL)
    media_codec_deref(mb->mb_cw);
  
  pool_put(mp->mp_mb_pool, mb);
}


/**
 *
 */
void
media_buf_free_unlocked(media_pipe_t *mp, media_buf_t *mb)
{
  hts_mutex_lock(&mp->mp_mutex);
  media_buf_free_locked(mp, mb);
  hts_mutex_unlock(&mp->mp_mutex);
}


