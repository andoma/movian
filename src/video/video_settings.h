#pragma once

void video_settings_init(void);

struct subtitle_settings {
  int scaling;
  int alignment;   // LAYOUT_ALIGN_ from layout.h
  int always_select;
  int align_on_video;
};

extern struct subtitle_settings subtitle_settings;


struct video_settings {
  int vdpau;
  int stretch_horizontal;
};

extern struct video_settings video_settings;

extern struct prop *subtitle_settings_dir;

