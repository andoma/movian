#pragma once

void video_settings_init(void);

struct subtitle_settings {
  int scaling;
  int alignment;   // LAYOUT_ALIGN_ from layout.h
  int always_select;
  int include_all_subs;
  int align_on_video;
  int style_override;
  int color;
  int shadow_color;
  int shadow_displacement;
  int outline_color;
  int outline_size;
};

extern struct subtitle_settings subtitle_settings;


struct video_settings {
  int vdpau;
  int stretch_horizontal;
  int stretch_fullscreen;
  int vzoom;

  enum {
    VIDEO_RESUME_NO = 0,
    VIDEO_RESUME_YES = 1,
  } resume_mode;

  int played_threshold;
  int vdpau_deinterlace;
  int vdpau_deinterlace_resolution_limit;
};

extern struct video_settings video_settings;

extern struct prop *subtitle_settings_dir;
