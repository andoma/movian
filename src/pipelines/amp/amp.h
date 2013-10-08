#pragma once

#define ANDROID
#define __LINUX__

#include "OSAL_api.h"
#include "amp_client.h"
#include "amp_client_support.h"
#include "amp_component.h"
#include "vpp_api.h"
#include "isl/amp_logger.h"
#include "isl/amp_buf_desc.h"


#include "video/h264_annexb.h"

extern AMP_FACTORY amp_factory;
extern AMP_COMPONENT amp_disp;

typedef struct amp_extra {

  //  AMP_COMPONENT amp_vout;
  AMP_COMPONENT amp_clk;

} amp_extra_t;



typedef struct amp_video {
  h264_annexb_ctx_t annexb;
  //  h264_parser_t parser;

  AMP_BDCHAIN *video_stream_queue;

  AMP_COMPONENT amp_vdec;

  int av_configured;
  
} amp_video_t;
