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
#include <stdarg.h>

#include "prop.h"
#include "prop_linkselected.h"

typedef struct lspriv {
  prop_t *lp_current;
  prop_t *lp_target;
  prop_t *lp_name;
} lspriv_t;

/**
 *
 */
static void
set(lspriv_t *lp, prop_t *p)
{
  lp->lp_current = p;
  prop_link(lp->lp_current, lp->lp_target);

  rstr_t *r = prop_get_name(p);
  prop_set_rstring(lp->lp_name, r);
  rstr_release(r);
}


/**
 *
 */
static void
child_added(lspriv_t *lp, prop_t *p, int flags)
{
  if(flags & PROP_ADD_SELECTED)
    set(lp, p);
}


/**
 *
 */
static void
prop_linkselected_cb(void *opaque, prop_event_t event, ...)
{
  prop_t *c;
  lspriv_t *lp = opaque;
  va_list ap;
  va_start(ap, event);

  switch(event) {
  default:
    break;

  case PROP_DESTROYED:
    prop_ref_dec(lp->lp_target);
    prop_ref_dec(lp->lp_name);
    free(lp);
    break;

  case PROP_SELECT_CHILD:
    set(lp, va_arg(ap, prop_t *));
    break;

  case PROP_ADD_CHILD:
    c = va_arg(ap, prop_t *);
    child_added(lp, c, va_arg(ap, int));
    break;

  case PROP_ADD_CHILD_BEFORE:
    c = va_arg(ap, prop_t *);
    (void)va_arg(ap, prop_t *);
    child_added(lp, c, va_arg(ap, int));
    break;

  case PROP_DEL_CHILD:
    c = va_arg(ap, prop_t *);
    if(c == lp->lp_current) {
      prop_unlink(lp->lp_target);
      lp->lp_current = NULL;
      prop_set_void(lp->lp_name);
    }
    break;
  }
  va_end(ap);
}


/**
 *
 */
void
prop_linkselected_create(prop_t *dir, prop_t *p,
                         const char *target,
                         const char *name)
{
  lspriv_t *lp = calloc(1, sizeof(lspriv_t));

  lp->lp_target = prop_create_r(p, target);
  lp->lp_name   = prop_create_r(p, name);

  prop_subscribe(PROP_SUB_TRACK_DESTROY,
                 PROP_TAG_CALLBACK, prop_linkselected_cb, lp,
                 PROP_TAG_ROOT, dir,
                 NULL);
}
