/*
 *  Copyright (C) 2007-2015 Lonelycoder AB
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
 *
 *  This program is also available under a commercial proprietary license.
 *  For more information, contact andreas@lonelycoder.com
 */
#pragma once
void video_settings_init(void);

struct video_settings {
  int vdpau;

  struct setting *vzoom_setting;
  struct setting *pan_horizontal_setting;
  struct setting *pan_vertical_setting;
  struct setting *scale_horizontal_setting;
  struct setting *scale_vertical_setting;
  struct setting *stretch_horizontal_setting;
  struct setting *stretch_fullscreen_setting;
  struct setting *vinterpolate_setting;

  enum {
    VIDEO_RESUME_NO = 0,
    VIDEO_RESUME_YES = 1,
    VIDEO_RESUME_ASK = 2,
  } resume_mode;

  enum {
    VIDEO_DPAD_MASTER_VOLUME = 0,
    VIDEO_DPAD_PER_FILE_VOLUME = 1,
  } dpad_up_down_mode;

  int played_threshold;
  int vdpau_deinterlace;
  int vdpau_deinterlace_resolution_limit;
  int continuous_playback;
  int video_accel;

  int seek_back_step;
  int seek_fwd_step;

  int video_buffer_size;
};

extern struct video_settings video_settings;

