#pragma once

void video_settings_init(void);

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
  int continuous_playback;
  int vda;
};

extern struct video_settings video_settings;

