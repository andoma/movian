/*
 *  Audio decoderuling
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

#ifndef AUDIO_DECODER_H
#define AUDIO_DECODER_H

#include "media.h"
#include "audio_mixer.h"

typedef struct audio_decoder {
  pthread_t ad_ptid;  /* Thread id */

  media_pipe_t *ad_mp;

  int16_t *ad_outbuf;

  audio_source_t *ad_output;

  int ad_rate;
  int ad_channels;
  int ad_codec;

} audio_decoder_t;


void audio_decoder_create(media_pipe_t *mp);
void audio_decoder_join(media_pipe_t *mp, audio_decoder_t *ad);


#endif /* AUDIO_DECODER_H */
