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

typedef struct glw_scroll_control {

  int target_pos;      // This is where we want to go
  float filtered_pos;  // This is weher we are
  int rounded_pos;     // Position rounded to pixels

  int total_size;
  int page_size;

  int scroll_threshold_pre;
  int scroll_threshold_post;

  float initial_touch_x;
  float initial_touch_y;
  int initial_pos;
  float last_touch_x;
  float last_touch_y;
  int64_t last_touch_time;

  float touch_velocity;
  float kinetic_scroll;

  glw_slider_metrics_t metrics;

  int16_t clip_offset_pre;
  int16_t clip_offset_post;
  float clip_alpha;
  float clip_blur;

} glw_scroll_control_t;


int glw_scroll_handle_pointer_event(glw_scroll_control_t *gsc,
                                    glw_t *w,
                                    const glw_pointer_event_t *gpe);

void glw_scroll_layout(glw_scroll_control_t *gsc, glw_t *w,
                       int height);

void glw_scroll_update_metrics(glw_scroll_control_t *gsc, glw_t *w);

int glw_scroll_set_float_attributes(glw_scroll_control_t *gsc, const char *a,
                                    float value);

int glw_scroll_set_int_attributes(glw_scroll_control_t *gsc, const char *a,
                                  int value);
