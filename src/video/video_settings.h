#pragma once

void video_settings_init(void);

extern int subtitle_always_select;
extern int subtitle_scaling;
extern int subtitle_alignment;

#define SUBTITLE_ALIGNMENT_CENTER 0
#define SUBTITLE_ALIGNMENT_LEFT   1
#define SUBTITLE_ALIGNMENT_RIGHT  2
