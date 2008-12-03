/*
 *  GL Widgets, Cursors
 *  Copyright (C) 2008 Andreas Öman
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
 */

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <GL/gl.h>

#include "glw.h"
#include "glw_bitmap.h"
#include "glw_cursor.h"
#include "glw_event.h"



static float cursor_alpha[5][5] = {
  {1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  {1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  {1.0f, 1.0f, 0.0f, 1.0f, 1.0f},
  {1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  {1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
};


static float cursor_red[5][5] = {
  {1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  {1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  {1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  {1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  {1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
};

static float cursor_green[5][5] = {
  {1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  {1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  {1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  {1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  {1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
};

static float cursor_blue[5][5] = {
  {1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  {1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  {1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  {1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  {1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
};



static float cursor_tex[5] = {
  0.0f, 1.0f / 3.0f, 0.5f, 2.0f / 3.0f, 1.0f
};


static void
set_cursor_col(int y, int x, float r)
{
  cursor_red[y][x] =   GLW_MIN(0.6 + r, 1.0f);
  cursor_green[y][x] = GLW_MIN(0.8 + r, 1.0f);
}

void
glw_cursor_layout_frame(glw_root_t *gr)
{
  static float v;
  float r;
  glw_texture_t *gt;

  if(gr->gr_cursor_gt == NULL)
    gr->gr_cursor_gt = glw_tex_create(gr, "theme://images/cursor.png");

  gt = gr->gr_cursor_gt;

  glw_tex_layout(gr, gt);


#define F(v) (0.5 + 0.5 * (v))

  r = F(sin(GLW_DEG2RAD(v + 0)));
  set_cursor_col(0, 0, r);
  set_cursor_col(0, 1, r);
  set_cursor_col(1, 0, r);
  set_cursor_col(1, 1, r);

  r = F(sin(GLW_DEG2RAD(v + 45)));
  set_cursor_col(0, 2, r);
  set_cursor_col(1, 2, r);
  
  r = F(sin(GLW_DEG2RAD(v + 90)));
  set_cursor_col(0, 3, r);
  set_cursor_col(0, 4, r);
  set_cursor_col(1, 3, r);
  set_cursor_col(1, 4, r);

  r = F(sin(GLW_DEG2RAD(v + 135)));
  set_cursor_col(2, 3, r);
  set_cursor_col(2, 4, r);

  r = F(sin(GLW_DEG2RAD(v + 180)));
  set_cursor_col(3, 3, r);
  set_cursor_col(3, 4, r);
  set_cursor_col(4, 3, r);
  set_cursor_col(4, 4, r);

  r = F(sin(GLW_DEG2RAD(v + 225)));
  set_cursor_col(3, 2, r);
  set_cursor_col(4, 2, r);

  r = F(sin(GLW_DEG2RAD(v + 270)));
  set_cursor_col(3, 0, r);
  set_cursor_col(3, 1, r);
  set_cursor_col(4, 0, r);
  set_cursor_col(4, 1, r);

  r = F(sin(GLW_DEG2RAD(v + 315)));
  set_cursor_col(2, 0, r);
  set_cursor_col(2, 1, r);

#undef F

  v+=1;


}



/**
 *
 */
static void
glw_cursor_draw(glw_root_t *gr, float alpha, float xscale, float yscale)
{
  glw_texture_t *gt = gr->gr_cursor_gt;
  float vex[5][2];
  int x, y;
  float v;

  if(gt == NULL || gt->gt_texture == 0)
    return;

  v = yscale;

  vex[0][1] =  1.0f + v * 0.5;
  vex[1][1] =  1.0f - v * 0.5;
  vex[2][1] =  0.0f;
  vex[3][1] = -1.0f + v * 0.5;
  vex[4][1] = -1.0f - v * 0.5;

  v = xscale;
  
  vex[0][0] = -1.0f - v * 0.5;
  vex[1][0] = -1.0f + v * 0.5;
  vex[2][0] =  0.0f;
  vex[3][0] =  1.0f - v * 0.5;
  vex[4][0] =  1.0f + v * 0.5;


  alpha = alpha * 0.75;
  
  glActiveTextureARB(GL_TEXTURE0_ARB);

  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, gt->gt_texture);

 glBegin(GL_QUADS);

  /* XXX: replace with drawarray */

  for(y = 0; y < 4; y++) {
    for(x = 0; x < 4; x++) {

      if(x > 0 && x < 3 && y > 0 && y < 3)
	continue;
      
      glColor4f(cursor_red  [y + 1][x + 0], 
		cursor_green[y + 1][x + 0], 
		cursor_blue [y + 1][x + 0], 
		cursor_alpha[y + 1][x + 0] * alpha);
      glTexCoord2f(cursor_tex[x + 0], cursor_tex[y + 1]);
      glVertex3f  (vex[x + 0][0], vex[y + 1][1], 0.0f);

      glColor4f(cursor_red  [y + 1][x + 1], 
		cursor_green[y + 1][x + 1], 
		cursor_blue [y + 1][x + 1], 
		cursor_alpha[y + 1][x + 1] * alpha);
      glTexCoord2f(cursor_tex[x + 1], cursor_tex[y + 1]);
      glVertex3f  (vex[x + 1][0], vex[y + 1][1], 0.0f);

      glColor4f(cursor_red  [y + 0][x + 1], 
		cursor_green[y + 0][x + 1], 
		cursor_blue [y + 0][x + 1], 
		cursor_alpha[y + 0][x + 1] * alpha);
      glTexCoord2f(cursor_tex[x + 1], cursor_tex[y + 0]);
      glVertex3f  (vex[x + 1][0], vex[y + 0][1], 0.0f);

      glColor4f(cursor_red  [y + 0][x + 0], 
		cursor_green[y + 0][x + 0], 
		cursor_blue [y + 0][x + 0], 
		cursor_alpha[y + 0][x + 0] * alpha);
      glTexCoord2f(cursor_tex[x + 0], cursor_tex[y + 0]);
      glVertex3f  (vex[x + 0][0], vex[y + 0][1], 0.0f);
    }
  }

  glEnd();
  glDisable(GL_TEXTURE_2D);
}


/**
 *
 */
static void
gcp_render(glw_root_t *gr, glw_cursor_painter_t *gcp, float aspect)
{
  int i;
  float xs, ys;
  //  float x, y;

  for(i = 0; i < 16; i++)
    gcp->gcp_m_prim[i] = GLW_LP(5, gcp->gcp_m_prim[i], gcp->gcp_m[i]);

  gcp->gcp_alpha_prim  = GLW_LP(5, gcp->gcp_alpha_prim, gcp->gcp_alpha);
  gcp->gcp_aspect_prim = GLW_LP(5, gcp->gcp_aspect_prim, gcp->gcp_aspect);

  glPushMatrix();
  glLoadMatrixf(gcp->gcp_m_prim);

  aspect = 1.0;;

  if(aspect > 0) {
    xs = 1 / aspect;
    ys = 1;
  } else {
    xs = 1;
    ys = aspect;
  }


  glw_cursor_draw(gr, gcp->gcp_alpha_prim, 
		  xs / (100 * fabs(gcp->gcp_m_prim[0])),
		  ys / (100 * fabs(gcp->gcp_m_prim[5]))
		  );
  glPopMatrix();
}

#if 0
/**
 *
 */
void
glw_form_focusable(glw_t *w, glw_rctx_t *rc)
{
  glw_form_t *gf = rc->rc_form;
  glw_form_ctrl_t *gfc;

  if(gf == NULL)
    return;

  gfc = gf->gf_current_form_ctrl;
  assert(gfc != NULL);

  if(w->glw_form_ctrl != gfc) {

    if(w->glw_form_ctrl != NULL)
      LIST_REMOVE(w, glw_form_ctrl_link);
    
    w->glw_form_ctrl = gfc;
    LIST_INSERT_HEAD(&gfc->gfc_focusables, w, glw_form_ctrl_link);
  }

  if(w->glw_matrix == NULL)
    w->glw_matrix = malloc(sizeof(float) * 16);
  
  glGetFloatv(GL_MODELVIEW_MATRIX, w->glw_matrix);

  if(w->glw_flags & GLW_FOCUS_DRAW_CURSOR && gfc->gfc_current_focus == w) {
    gfc->gfc_alpha  = rc->rc_alpha;
    gfc->gfc_aspect = rc->rc_aspect;
    memcpy(gfc->gfc_m, w->glw_matrix, 16 * sizeof(float));
  }
}


/**
 *
 */
int
glw_form_is_focused(glw_t *w)
{
  glw_form_ctrl_t *gfc;

  if((gfc = w->glw_form_ctrl) == NULL)
    return 0;

  return w->glw_form_ctrl->gfc_current_focus == w;
}



/**
 *
 */
glw_t *
glw_form_get_focused_child(glw_t *w, glw_rctx_t *rc)
{
  glw_form_t *gf = rc->rc_form;
  glw_form_ctrl_t *gfc;
  glw_t *x;

  if(gf == NULL)
    return NULL;

  gfc = gf->gf_current_form_ctrl;
  assert(gfc != NULL);

  if((x = gfc->gfc_current_focus) == NULL)
    return NULL;

  while(x->glw_parent != NULL) {
    if(x->glw_parent == w)
      return x;
    x = x->glw_parent;
  }
  return 0;
}
#endif


#if 0
/**
 *
 */
static float
compute_angle(float dx, float dy)
{
  float angle;

  if(dx < 0) {
    angle = atanf((float)dy / (float)dx);
  } else if(dx == 0) {
    angle = (dy > 0 ? M_PI : 0) + M_PI * 0.5;
  } else {
      angle = M_PI + atanf((float)dy / (float)dx);
  }
  angle *= 360.0f / (M_PI * 2);
  if(angle < 0)
    angle = angle + 360;
  return angle;
}


/**
 *
 */
static float
compute_distance(float dx, float dy)
{
  return sqrt(dx * dx + dy * dy);
}


/**
 *
 */
static int
valid_angle(float angle, event_type_t type)
{
  switch(type) {
  default:
    return 0;

  case EVENT_RIGHT:
    if(!(angle < 90 || angle > 190))
      return 0;
    break;
  case EVENT_UP:
    if(!(angle > 10 && angle <170))
      return 0;
    break;
  case EVENT_LEFT:
    if(!(angle > 100 && angle < 270))
      return 0;
    break;
  case EVENT_DOWN:
    if(!(angle > 190 && angle < 350))
      return 0;
    break;
  }
  return 1;
}

static float
compute_score(float angle, float ma, float distance)
{
  float score;

  score = fabs(angle - ma);
  score = log(score + 2);
  score = score * distance;
  return score;
}

/**
 *
 */
static int
glw_form_navigate(glw_form_ctrl_t *gfc, event_t *e)
{
  float x, y, dx, dy, angle, score, bestscore;
  float ma;
  glw_t *c = gfc->gfc_current_focus;
  glw_t *t;
  glw_t *best;

  switch(e->e_type) {
  case EVENT_RIGHT:
    ma = 0;
    break;
  case EVENT_UP:
    ma = 90;
    break;
  case EVENT_LEFT:
    ma = 180;
    break;
  case EVENT_DOWN:
    ma = 270;
    break;
  default:
    return 0;
  }

  bestscore = 10000000;

  if(c->glw_matrix != NULL) {
    x = c->glw_matrix[12];
    y = c->glw_matrix[13];
  } else {
    x = 0;
    y = 0;
  }

  best = NULL;

  LIST_FOREACH(t, &gfc->gfc_focusables, glw_form_ctrl_link) {
    if(t == c || t->glw_matrix == NULL)
      continue;

#if 0
    printf("Entry\n");

    score = 100000;

    /* Lower left */
    dx = x - t->glw_matrix[12] - t->glw_matrix[0];
    dy = y - t->glw_matrix[13] - t->glw_matrix[5];

    printf("ll: %f %f\n", dx, dy);
    angle = compute_angle(dx, dy);
    if(valid_angle(angle, e->e_type))
      score = GLW_MIN(score, compute_score(angle, ma,
					   compute_distance(dx, dy)));

    /* Lower right */
    dx = x - t->glw_matrix[12] + t->glw_matrix[0];
    dy = y - t->glw_matrix[13] - t->glw_matrix[5];
    printf("lr: %f %f\n", dx, dy);
    angle = compute_angle(dx, dy);
    if(valid_angle(angle, e->e_type))
      score = GLW_MIN(score, compute_score(angle, ma,
					   compute_distance(dx, dy)));

    /* Upper right */
    dx = x - t->glw_matrix[12] + t->glw_matrix[0];
    dy = y - t->glw_matrix[13] + t->glw_matrix[5];
    printf("ur: %f %f\n", dx, dy);
    angle = compute_angle(dx, dy);
    if(valid_angle(angle, e->e_type))
      score = GLW_MIN(score, compute_score(angle, ma,
					   compute_distance(dx, dy)));

    /* Upper left */
    dx = x - t->glw_matrix[12] - t->glw_matrix[0];
    dy = y - t->glw_matrix[13] + t->glw_matrix[5];
    printf("ul: %f %f\n", dx, dy);
    angle = compute_angle(dx, dy);
    if(valid_angle(angle, e->e_type))
      score = GLW_MIN(score, compute_score(angle, ma,
					   compute_distance(dx, dy)));

    if(score == 100000)
      continue;
#endif

    dx = x - t->glw_matrix[12];
    dy = y - t->glw_matrix[13];
    angle = compute_angle(dx, dy);
    if(!valid_angle(angle, e->e_type))
      continue;
    score = compute_score(angle, ma, compute_distance(dx, dy));

    if(score < bestscore) {
      best = t;
      bestscore = score;
    }
  }

  if(best == NULL)
    return 0;


  int i;

  printf("Target matrix\n");
  for(i = 0; i < 16; i++) {
    printf("%f%c", best->glw_matrix[i], "\t\t\t\n"[i & 3]);
  }

  gfc->gfc_current_focus = best;
  return 1;
}
#endif

static float
compute_position(glw_t *w, int horizontal)
{
  glw_t *p, *c;
  float tw, a, x;

  x = 0;

  for(; (p = w->glw_parent) != NULL; w = p) {
    switch(p->glw_class) {
    case GLW_CONTAINER_X:
      if(!horizontal)
	continue;
      break;

    case GLW_CONTAINER_Y:
      if(horizontal)
	continue;
      break;

    default:
      continue;
    }
    
    tw = 0;
    a = w->glw_weight / 2;

    TAILQ_FOREACH(c, &p->glw_childs, glw_parent_link)
      tw += c->glw_weight;

    TAILQ_FOREACH(c, &p->glw_childs, glw_parent_link) {
      if(c == w)
	break;
      a += c->glw_weight;
    }

    x = (2 * a / tw) - 1 + (x * w->glw_weight / tw);
  }
  return x;
}

#if 0

/**
 *
 */
static void
find_target2(struct glw_queue *q, glw_t *w, glw_t *forbidden, 
	     float xmin, float xmax, float ymin, float ymax)
{
  glw_t *c;
  float x, y;

  if(w != forbidden) {

    if(w->glw_flags & GLW_FOCUSABLE) {
      x = compute_position(w, 1);
      y = compute_position(w, 0);
      if(x > xmin && x < xmax && y > ymin && y < ymax) {
	TAILQ_INSERT_TAIL(q, w, glw_tmp_link);
	return;
      }
    }
  }

  switch(w->glw_class) {
  case GLW_LIST:
    /* We never dive into lists, but they are a candidate if
       they have something in them */
    if(TAILQ_FIRST(&w->glw_childs) != NULL)
      TAILQ_INSERT_TAIL(q, w, glw_tmp_link);
    break;


  case GLW_DECK:
    c = w->glw_selected;
    if(c != NULL && c != forbidden)
      find_target2(q, c, NULL, xmin, xmax, ymin, ymax);
    break;

  case GLW_ANIMATOR:
    c = TAILQ_FIRST(&w->glw_childs);
    if(c != NULL && c != forbidden)
      find_target2(q, c, NULL, xmin, xmax, ymin, ymax);
    break;

    
  case GLW_MODEL:
  case GLW_CONTAINER_X:
  case GLW_CONTAINER_Y:
  case GLW_CONTAINER_Z:
  case GLW_CONTAINER:
    TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
      if(c != forbidden)
	find_target2(q, c, NULL, xmin, xmax, ymin, ymax);
    }
    break;

  default:
    break;
  }
}

/**
 *
 */
static glw_t *
find_target(glw_t *w, float xmin, float xmax, float ymin, float ymax, int l)
{
  struct glw_queue q;

  TAILQ_INIT(&q);
  find_target2(&q, w, w, xmin, xmax, ymin, ymax);

  if(TAILQ_FIRST(&q) != NULL)
    return l ? TAILQ_LAST(&q, glw_queue) : TAILQ_FIRST(&q);


  while(w->glw_parent != NULL) {
    TAILQ_INIT(&q);

    find_target2(&q, w->glw_parent, w, xmin, xmax, ymin, ymax);

    if(TAILQ_FIRST(&q) != NULL)
      return l ? TAILQ_LAST(&q, glw_queue) : TAILQ_FIRST(&q);

    w = w->glw_parent;
  }
  return NULL;
}

#endif



typedef struct query {
  float x, xmin, xmax;
  float y, ymin, ymax;

  int direction;
  int orientation;

  glw_t *best;
  float score;

} query_t;



#if 0
static glw_t *
searchdown(glw_t *w, int orientation, int direction, float x, float y)
{
  glw_t *c, *t;

  if(w->glw_flags & GLW_FOCUSABLE)
    return w;

  switch(w->glw_class) {
  default:
    return NULL;

  case GLW_DECK:
    if((c = w->glw_selected) == NULL)
      return NULL;
    return searchdown(c, orientation, direction, x, y);

  case GLW_ANIMATOR:
  case GLW_BITMAP:
  case GLW_MODEL:
    if((c = TAILQ_FIRST(&w->glw_childs)) == NULL)
      return NULL;
    return searchdown(c, orientation, direction, x, y);

  case GLW_LIST:
    break;

  case GLW_CONTAINER_Z:
    TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link)
      if((t = searchdown(c, orientation, direction, x, y)) != NULL)
	return t;
    break;

  case GLW_CONTAINER_X:
  case GLW_CONTAINER_Y:
    if(w->glw_class != (orientation ? GLW_CONTAINER_X : GLW_CONTAINER_Y))
      break;
    
    if(direction) {
      TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link)
	if((t = searchdown(c, orientation, direction, x, y)) != NULL)
	  return t;
    } else {
      TAILQ_FOREACH_REVERSE(c, &w->glw_childs, glw_queue, glw_parent_link)
	if((t = searchdown(c, orientation, direction, x, y)) != NULL)
	  return t;
    }
  }
  return NULL;
}



#endif

static void
find_candidate(glw_t *w, query_t *query)
{
  glw_t *c;
  float x, y, distance, dx, dy;

  if(w->glw_focus_mode == GLW_FOCUS_TARGET) {
    
    x = compute_position(w, 1);
    y = compute_position(w, 0);

    dx = query->x - x;
    dy = query->y - y;
    distance = sqrt(dx * dx + dy * dy);

    if(distance < query->score) {
      query->score = distance;
      query->best = w;
    }
  }

  switch(w->glw_class) {
  default:
    return;

  case GLW_DECK:
    if((c = w->glw_selected) != NULL)
      find_candidate(c, query);
    break;

  case GLW_ANIMATOR:
  case GLW_BITMAP:
  case GLW_MODEL:
    if((c = TAILQ_FIRST(&w->glw_childs)) != NULL)
      find_candidate(c, query);
    break;

  case GLW_LIST:
    /* We end up here if we try to enter a GLW_LIST from outside */
    if(query->direction) {
      c = TAILQ_FIRST(&w->glw_childs);
    } else {
      c = TAILQ_LAST(&w->glw_childs, glw_queue);
    }

    if(c != NULL)
      find_candidate(c, query);

    break;

  case GLW_CONTAINER_Z:
    TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link)
      find_candidate(c, query);
    break;

  case GLW_CONTAINER_X:
  case GLW_CONTAINER_Y:
    TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link)
      find_candidate(c, query);
    break;
  }
}


/**
 *
 */
int
glw_navigate(glw_t *w, event_t *e)
{
  glw_t  *p, *c, *t = NULL;
  float x, y;
  int direction;
  int orientation;
  query_t query;

  x = compute_position(w, 1);
  y = compute_position(w, 0);

  memset(&query, 0, sizeof(query));

  query.x = x;
  query.y = y;
  query.score = 100000000;

  switch(e->e_type) {
  default:
    return 0;

  case EVENT_UP:
    orientation = 0;
    direction   = 0;

    query.xmin = -1;
    query.xmax = 1;
    query.ymin = -1;
    query.ymax = y - 0.0001;
    break;

  case EVENT_DOWN:
    orientation = 0;
    direction   = 1;

    query.xmin = -1;
    query.xmax = 1;
    query.ymin = y + 0.0001;
    query.ymax = 1;
    break;

  case EVENT_LEFT:
    orientation = 1;
    direction   = 0;

    query.xmin = -1;
    query.xmax = x - 0.0001;
    query.ymin = -1;
    query.ymax = 1;
    break;

  case EVENT_RIGHT:
    orientation = 1;
    direction   = 1;

    query.xmin = x + 0.0001;
    query.xmax = 1;
    query.ymin = -1;
    query.ymax = 1;
    break;
  }


  query.orientation = orientation;
  query.direction   = direction;

  c = NULL;
  for(; (p = w->glw_parent) != NULL; w = p) {

    switch(p->glw_class) {
      
    default:

    case GLW_ANIMATOR:
    case GLW_BITMAP:
    case GLW_DECK:
    case GLW_MODEL:
      break;

    case GLW_CONTAINER_Z:
      break;

    case GLW_CONTAINER_X:
    case GLW_CONTAINER_Y:

      if(p->glw_class != (orientation ? GLW_CONTAINER_X : GLW_CONTAINER_Y))
	break;

    case GLW_LIST:

      c = w;
      while(1) {
	if(direction == 1)
	  c = TAILQ_NEXT(c, glw_parent_link);
	else
	  c = TAILQ_PREV(c, glw_queue, glw_parent_link);
	if(c == NULL)
	  break;
	find_candidate(c, &query);
	t = query.best;
	if(t != NULL)
	  break;
      }
      break;

    }
    if(t != NULL)
      break;
  }

  if(t != NULL)
    glw_focus_set(t);

  return 0;
}


/**
 *
 */
static int
glw_cursor_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_t *c = TAILQ_FIRST(&w->glw_childs);
  glw_cursor_t *gf = (void *)w;
  glw_rctx_t *rc;
  glw_root_t *gr = w->glw_root;

  switch(signal) {
  default:
    break;

  case GLW_SIGNAL_LAYOUT:
    gf->render_cycle = 0;

    if(c != NULL)
      glw_layout0(c, extra);
    break;

  case GLW_SIGNAL_RENDER:
    rc = extra;

    if(gf->render_cycle == 0)
      rc->rc_cursor_painter = &gf->gcp;
    else
      rc->rc_cursor_painter = NULL;

    if(c != NULL)
      glw_render0(c, rc);

    if(gf->render_cycle == 0)
      gcp_render(gr, &gf->gcp, rc->rc_aspect);

    gf->render_cycle++;
    break;
  }
  return 0;
}



/**
 *
 */
void 
glw_cursor_ctor(glw_t *w, int init, va_list ap)
{
  if(init) {
    glw_signal_handler_int(w, glw_cursor_callback);
  }
}
