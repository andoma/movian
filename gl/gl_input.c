/*
 *  GLUT input
 *  Copyright (C) 2007 Andreas Öman
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

#include <sys/time.h>
#include <pthread.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <libglw/glw.h>

#include "showtime.h"
#include "hid/input.h"


static void
gl_special_key(int key, int x, int y)
{
  switch(key) {
  case GLUT_KEY_UP:
    input_key_down(INPUT_KEY_UP);
    break;
  case GLUT_KEY_DOWN:
    input_key_down(INPUT_KEY_DOWN);
    break;
  case GLUT_KEY_LEFT:
    input_key_down(INPUT_KEY_LEFT);
    break;
  case GLUT_KEY_RIGHT:
    input_key_down(INPUT_KEY_RIGHT);
    break;

  case GLUT_KEY_PAGE_UP:
    input_key_down(INPUT_KEY_PREV);
    break;

  case GLUT_KEY_PAGE_DOWN:
    input_key_down(INPUT_KEY_NEXT);
    break;

  case GLUT_KEY_END:
    input_key_down(INPUT_KEY_STOP);
    break;

  case GLUT_KEY_F1:
    input_key_down(INPUT_KEY_MENU);
    break;

  case GLUT_KEY_F2:
    input_key_down(INPUT_KEY_PLAYPAUSE);
    break;

  case GLUT_KEY_F3:
    input_key_down(INPUT_KEY_APP_LAUNCHER);
    break;

  case GLUT_KEY_F4:
    input_key_down(INPUT_KEY_ROTATE);
    break;

  case GLUT_KEY_F5:
    input_key_down(INPUT_KEY_VOLUME_DOWN);
    break;

  case GLUT_KEY_F6:
    input_key_down(INPUT_KEY_VOLUME_UP);
    break;

  case GLUT_KEY_F7:
    input_key_down(INPUT_KEY_VOLUME_MUTE);
    break;

  case GLUT_KEY_F8:
    input_key_down(INPUT_KEY_META_INFO);
    break;

  case GLUT_KEY_F9:
    input_key_down(INPUT_KEY_WIDEZOOM);
    break;

  case GLUT_KEY_F10:
    input_key_down(INPUT_KEY_POWER);
    break;

  case GLUT_KEY_F11:
    input_key_down(INPUT_KEY_SEEK_BACKWARD);
    break;
  case GLUT_KEY_F12:
    input_key_down(INPUT_KEY_SEEK_FORWARD);
    break;
  }
}

static void
gl_key(unsigned char key, int x, int y)
{
  switch(key) {
  case 9:
    input_key_down(INPUT_KEY_TASKSWITCH);
    break;

  case ' ':
    input_key_down(INPUT_KEY_SELECT);
    break;

  case 13:
    input_key_down(INPUT_KEY_ENTER);
    break;
  case 27:
    input_key_down(INPUT_KEY_CLOSE);
    break;
  case '+':
    input_key_down(INPUT_KEY_INCR);
    break;
  case '-':
    input_key_down(INPUT_KEY_DECR);
    break;
  case 8:
    input_key_down(INPUT_KEY_BACK);
    break;

  default:
    input_key_down(key);
    break;

  }

}


static void
mousefunc(int button, int state, int x, int y)
{

  if(state == GLUT_DOWN) {
    switch(button) {
    case 2:
      input_key_down(INPUT_KEY_ENTER);
      break;
    case 3:
      gl_special_key(GLUT_KEY_UP, x, y);
      break;
    case 4:
      gl_special_key(GLUT_KEY_DOWN, x, y);
      break;
    }
  }
}

static int lx, ly;

void 
mousemotion_passive(int x, int y)
{
  lx = x;
  ly = y;
}

void 
mousemotion(int x, int y)
{
  int dx, dy;
  inputevent_t ie;

  dx = x - lx;
  dy = y - ly;

  lx = x;
  ly = y;

  ie.u.xy.x = dx;
  ie.u.xy.y = dy;

  ie.type = INPUT_PAD;
  input_root_event(&ie);
}


void
gl_input_setup(void)
{
  glutSpecialFunc(gl_special_key);
  glutKeyboardFunc(gl_key);
  glutMouseFunc(mousefunc);
  glutMotionFunc(mousemotion);
  glutPassiveMotionFunc(mousemotion_passive);
}


