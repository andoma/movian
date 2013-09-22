#pragma once

extern glw_class_t glw_grid;

typedef struct glw_grid {
  glw_t w;

  float child_scale;

  int current_ypos;
  float filtered_ypos;

  int current_xpos;
  float filtered_xpos;

  glw_t *scroll_to_me;

} glw_grid_t;


typedef struct glw_gridrow {
  glw_t w;

  float child_scale;

  glw_t *scroll_to_me;
} glw_gridrow_t;
