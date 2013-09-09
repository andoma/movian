#pragma once

/**
 *
 */
typedef struct rpi_video_codec {
  omx_component_t *rvc_decoder;
  hts_cond_t rvc_avail_cond;
  int rvc_last_epoch;
  const char *rvc_name;
  int rvc_name_set;
} rpi_video_codec_t;

