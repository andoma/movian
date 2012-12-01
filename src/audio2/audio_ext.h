#pragma once

struct audio_decoder;
struct media_pipe;

void audio_init(void);

void audio_fini(void);

struct audio_decoder *audio_decoder_create(struct media_pipe *mp);

void audio_decoder_destroy(struct audio_decoder *ad);
