/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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

#include <assert.h>
#include "event.h"
#include "glw.h"

#define NLANES 8

#define CHILD_NEW     0
#define CHILD_PENDING 1
#define CHILD_RUNNING 2
#define CHILD_ZOMBIE  3

#define TBS 1000

typedef struct glw_multitile {
  glw_t w;
  int a;
  unsigned int rand;
  int child_width;
  int child_height;
  int hold_spawn;

  int pending_release;

  int lane_cnt;
  int all_lanes_loaded;
  float xfade;

  struct glw_tile_transformer *current;
  struct glw_tile_transformer *next;

  int transmode;
  int transcnt;

  int did_render;

} glw_multitile_t;


typedef struct glw_tile_transformer {
  void (*global)(glw_rctx_t *rc, glw_multitile_t *mt);
  void (*local)(glw_rctx_t *rc, glw_multitile_t *mt, glw_t *c);
  int reverse;
} glw_tile_transformer_t;


#define glw_parent_value   glw_parent_val[0].f
#define glw_parent_state   glw_parent_val[1].i32
#define glw_parent_timeout glw_parent_val[2].i32
#define glw_parent_lane    glw_parent_val[3].i32
#define glw_parent_r0      glw_parent_val[4].f
#define glw_parent_r1      glw_parent_val[5].f


/**
 *
 */
static void
rotate_normalized(glw_rctx_t *rc, float a, float x, float y, float z)
{
  float t_aspect = 1;

  if(t_aspect * rc->rc_height < rc->rc_width) {
    // Shrink X
    int border = rc->rc_width - t_aspect * rc->rc_height;
    int left  = (border + 1) / 2;
    int right = rc->rc_width - (border / 2);

    float s = (right - left) / (float)rc->rc_width;
    float t = -1.0f + (right + left) / (float)rc->rc_width;

    glw_Translatef(rc, t, 0, 0);
    glw_Scalef(rc, s, 1.0f, 1.0f);
    glw_Rotatef(rc, a, x, y, z);
    glw_Scalef(rc, 1/s, 1.0f, 1.0f);


  } else {
    // Shrink Y
    int border = rc->rc_height - rc->rc_width / t_aspect;
    int bottom  = (border + 1) / 2;
    int top     = rc->rc_height - (border / 2);

    float s = (top - bottom) / (float)rc->rc_height;
    float t = -1.0f + (top + bottom) / (float)rc->rc_height;

    glw_Translatef(rc, 0, t, 0);
    glw_Scalef(rc, 1.0f, s, 1.0f);
    glw_Rotatef(rc, a, x, y, z);
    glw_Scalef(rc, 1.0f, 1/s, 1.0f);
  }
}


/**
 * Starwars
 */
static void
starwars_global(glw_rctx_t *rc, glw_multitile_t *mt)
{
  glw_Rotatef(rc, 66, -1,0,0);
}


static void
starwars2_global(glw_rctx_t *rc, glw_multitile_t *mt)
{
  float a = mt->w.glw_root->gr_frames % 360;

  rotate_normalized(rc, a, 0, 0, 1);
  glw_Rotatef(rc, 66, -1,0,0);
}


static void
starwars_local(glw_rctx_t *rc, glw_multitile_t *mt, glw_t *c)
{
  glw_Translatef(rc,
		 -1 + (1.0f / NLANES) + c->glw_parent_lane * (2.0f / NLANES),
		 11 - 17 * c->glw_parent_value,
		 -1);
  
  rc->rc_alpha *= c->glw_parent_value;
}


static glw_tile_transformer_t starwars = {
  .global = starwars_global,
  .local  = starwars_local,
};

static glw_tile_transformer_t starwars2 = {
  .global = starwars2_global,
  .local  = starwars_local,
};



/**
 * Rain
 */
static void
rain_local(glw_rctx_t *rc, glw_multitile_t *mt, glw_t *c)
{
  float n = c->glw_parent_value;
  n = n * n;

  glw_Translatef(rc,
		 0.2 * (c->glw_parent_r0 - 0.5) +
		 -1 + (1.0f / NLANES) + c->glw_parent_lane * (2.0f / NLANES),
		 0.2 * (c->glw_parent_r1 - 0.5) +
		 1.5 - 10 * n,
		 0);
  
  glw_Scalef(rc, 0.75, 0.75, 1.0);

  rc->rc_alpha *= 1 - c->glw_parent_value;
}


static glw_tile_transformer_t rain = {
  .local  = rain_local,
};


/**
 * Tunnel
 */
static void
tunnel_global(glw_rctx_t *rc, glw_multitile_t *mt)
{
  glw_scale_to_aspect(rc, 1);
}


static void
tunnel_local(glw_rctx_t *rc, glw_multitile_t *mt, glw_t *c)
{
  float a = mt->w.glw_root->gr_frames % 360;

  float screw = sin(1 * a * M_PI * 2 / 360) * 180;

  glw_Rotatef(rc, 
	      (1 - c->glw_parent_value) * screw + 
	      c->glw_parent_lane * 360.0 / NLANES, 0, 0, 1);
  
  glw_Translatef(rc,
		 -0.9,
		 0,
		 -16 + c->glw_parent_value * 20);

  glw_Rotatef(rc, 90, 0, 1, 0);

  
  rc->rc_alpha *= c->glw_parent_value;
}


static glw_tile_transformer_t tunnel = {
  .global = tunnel_global,
  .local  = tunnel_local,
};



static void
hyperdrive_local(glw_rctx_t *rc, glw_multitile_t *mt, glw_t *c)
{
  float x, y;

  x = -0.75 + (1.5 * c->glw_parent_r0);
  y = -0.75 + (1.5 * c->glw_parent_r1);

  float d = (1 - c->glw_parent_value) * 10;
  d = d * d;

  float s = 1 + 0.05 * d;

  glw_Translatef(rc,
		 x * s,
		 y * s,
		 1 -d);

  float alpha;

  if(c->glw_parent_value > 0.9) {
    alpha = 1 - (c->glw_parent_value - 0.95) * 20;
  } else {
    alpha = c->glw_parent_value * (1 / 0.95);
  }
 
  rc->rc_alpha *= alpha;
}



static glw_tile_transformer_t hyperdrive = {
  .local  = hyperdrive_local,
};


/**
 *
 */
static void
rollercoaster_global(glw_rctx_t *rc, glw_multitile_t *mt)
{
  glw_Rotatef(rc, 66, -1,0,0);
}


static void
rollercoaster_local(glw_rctx_t *rc, glw_multitile_t *mt, glw_t *c)
{
  float a = mt->w.glw_root->gr_frames % 360;
  float b = (c->glw_parent_value);
  glw_Rotatef(rc, b * cos(a * 2 * M_PI / 360) * 180, 0, 1, 0);

  glw_Translatef(rc,
		 0.5 * b * cos(a * 2 * M_PI / 360) +
		 -1 + (1.0f / NLANES) + c->glw_parent_lane * (2.0f / NLANES),
		 -2 + 30 * c->glw_parent_value,
		 -1 + 3 * b * sin(a * 2 * M_PI / 360));
  
  glw_Rotatef(rc, -65, -1,0,0);

  rc->rc_alpha *= 1 - c->glw_parent_value;
}



static glw_tile_transformer_t rollercoaster  __attribute__((unused)) = {
  .global = rollercoaster_global,
  .local  = rollercoaster_local,
};


/**
 *
 */
static void
glw_multitile_render(glw_t *w, const glw_rctx_t *rc)
{
  glw_multitile_t *sf = (glw_multitile_t *)w;
  glw_t *c;

  glw_rctx_t global_rc_a = *rc;
  glw_rctx_t global_rc_b = *rc;

  glw_rctx_t local_rc_a;
  glw_rctx_t local_rc_b;

  glw_rctx_t rc_mix = *rc;
  
  glw_tile_transformer_t *a = sf->current;
  glw_tile_transformer_t *b = sf->next;
  float width, height;

  float xfade = GLW_S(sf->xfade);

  if(a->global != NULL)
    a->global(&global_rc_a, sf);
  if(b != NULL && b->global != NULL)
    b->global(&global_rc_b, sf);

  rc_mix.rc_width  = sf->child_width;
  rc_mix.rc_height = sf->child_height;

  TAILQ_FOREACH_REVERSE(c, &w->glw_childs, glw_queue, glw_parent_link) {

    if(c->glw_parent_state != CHILD_RUNNING)
      continue;

    local_rc_a = global_rc_a;
    a->local(&local_rc_a, sf, c);

    local_rc_b = global_rc_b;
    if(b != NULL) 
      b->local(&local_rc_b, sf, c);
    
    rc_mix.rc_alpha = GLW_LERP(xfade, local_rc_a.rc_alpha, local_rc_b.rc_alpha);

    glw_LerpMatrix(rc_mix.rc_mtx, xfade, local_rc_a.rc_mtx,
		   local_rc_b.rc_mtx);

    width  = GLW_LERP(xfade, global_rc_a.rc_width,  global_rc_b.rc_width);
    height = GLW_LERP(xfade, global_rc_a.rc_height, global_rc_b.rc_height);

    glw_Scalef(&rc_mix,
	       sf->child_width  / width,
	       sf->child_height / height,
	       1.0);

    glw_render0(c, &rc_mix);
  }
  sf->did_render = 1;
}


/**
 *
 */
static void
reap_child(glw_t *w)
{
  w->glw_parent_state = CHILD_ZOMBIE;
  if(w->glw_originating_prop != NULL) {
    event_t *e = event_create_action_str("departed");
    prop_send_ext_event(w->glw_originating_prop, e);
    event_release(e);
  } else {
    fprintf(stderr,
	    "multitile: Child has no originating prop, can't depart it\n");
  }
}



/**
 *
 */
static void
randomize(glw_multitile_t *sf, glw_t *c)
{
  sf->rand = sf->rand * 1664525 + 1013904223;

  c->glw_parent_r0 = ((sf->rand >> 16) / 65536.0);

  sf->rand = sf->rand * 1664525 + 1013904223;

  c->glw_parent_r1 = ((sf->rand >> 16) / 65536.0);
}


/**
 *
 */
static void
dotransmode(glw_multitile_t *sf)
{
  struct glw_tile_transformer *next;

  if(sf->transcnt > 0) {
    sf->transcnt--;
    return;
  }

  if(sf->xfade == 0) {
    sf->transmode++;

    switch(sf->transmode) {
    default: sf->transmode = 0;
    case 0: next = &starwars;  break;
    case 1: next = &hyperdrive;  break;
    case 2: next = &rain;      break;
    case 3: next = &starwars2; break;
    case 4: next = &tunnel;    break;
    }
    sf->next = next;
  }

  sf->xfade += 0.01;

  if(sf->xfade > 1) {
    sf->xfade = 0;
    sf->transcnt = TBS;
    sf->current = sf->next;
    sf->next = NULL;
  }
}


/**
 *
 */
static void
glw_multitile_layout(glw_t *w, const glw_rctx_t *rc)
{
  glw_multitile_t *sf = (glw_multitile_t *)w;
  glw_t *c;
  glw_rctx_t rc0;

  int may_release = 0;

  int full_pending = 0; 
  int lane = 0;

  dotransmode(sf);
  
  sf->child_width = rc->rc_width / NLANES;
  sf->child_height = sf->child_width;

  if(sf->pending_release > 0) {
    if(sf->did_render)
      sf->pending_release--;
  } else {
    if(sf->all_lanes_loaded) {
      sf->pending_release = 30;
      may_release = NLANES;
    }
  }

  int num = -1;

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    num++;
    switch(c->glw_parent_state) {
    case CHILD_NEW:
      c->glw_parent_state = CHILD_PENDING;
      c->glw_parent_timeout = 0;
      // FALLTHRU
    case CHILD_PENDING:
      if(c->glw_class->gc_ready != NULL && !c->glw_class->gc_ready(c)) {
	// Not yet ready
	c->glw_parent_timeout++;
	if(c->glw_parent_timeout == 1000) {
	  reap_child(c);
	  continue;
	}
	break;
      }

      full_pending++;
      if(full_pending == NLANES) {
	sf->all_lanes_loaded = 1;
      }

      if(may_release == 0)
	break;
      
      may_release--;

      // FALLTHRU
      c->glw_parent_state = CHILD_RUNNING;
      c->glw_parent_value = 0;
      c->glw_parent_lane  = lane;
      randomize(sf, c);
      lane++;

    case CHILD_RUNNING:
      if(sf->did_render)
	c->glw_parent_value += 0.0010;

      if(c->glw_parent_value < 1)
	break;
      reap_child(c);
      // FALLTHRU
    case CHILD_ZOMBIE:
      continue;
    default:
      abort();
    }

    rc0 = *rc;
    rc0.rc_width  = sf->child_width;
    rc0.rc_height = sf->child_height;
    glw_layout0(c, &rc0);
  }
  sf->did_render = 0;
}


/**
 *
 */
static void 
glw_multitile_ctor(glw_t *w)
{
  glw_multitile_t *sf = (glw_multitile_t *)w;
  sf->current = &starwars;
  sf->transcnt = TBS;
}


/**
 *
 */
static glw_class_t glw_multitile = {
  .gc_name = "multitile",
  .gc_instance_size = sizeof(glw_multitile_t),
  .gc_flags = GLW_CAN_HIDE_CHILDS,
  .gc_layout = glw_multitile_layout,
  .gc_render = glw_multitile_render,
  .gc_ctor = glw_multitile_ctor,
};

GLW_REGISTER_CLASS(glw_multitile);
