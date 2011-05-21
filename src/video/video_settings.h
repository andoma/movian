#pragma once

void video_settings_init(void);

extern int subtitle_setting_always_select;
extern int subtitle_setting_scaling;
extern int subtitle_setting_alignment;

#define SUBTITLE_ALIGNMENT_CENTER 0
#define SUBTITLE_ALIGNMENT_LEFT   1
#define SUBTITLE_ALIGNMENT_RIGHT  2
#define SUBTITLE_ALIGNMENT_AUTO   3

extern int video_setting_vdpau;
