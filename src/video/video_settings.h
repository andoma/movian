#pragma once

void video_settings_init(void);

#define SUBTITLE_ALIGNMENT_CENTER 0
#define SUBTITLE_ALIGNMENT_LEFT   1
#define SUBTITLE_ALIGNMENT_RIGHT  2
#define SUBTITLE_ALIGNMENT_AUTO   3

struct subtitle_settings {
  int scaling;
  int alignment;
  int always_select;
};

extern struct subtitle_settings subtitle_settings;


struct video_settings {
  int vdpau;
  int stretch_horizontal;
};

extern struct video_settings video_settings;


