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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>
#include <unistd.h>
#include <math.h>
#include <sys/time.h>

#include "arch/atomic.h"

#include "prop.h"
#include "prop_i.h"

#ifdef PROP_DEBUG

void prop_test(void);

static int testval;

static void
set_testval(void *opaque, int value)
{
  printf("Value=%d\n", value);
  testval = value;
}

static void
checktestval(int line, int value)
{
  if(testval == value)
    return;
  printf("Expected %d got %d on line %d\n", value, testval, line);
  exit(1);
}

#define CHECKTESTVAL(value) checktestval(__LINE__, value)



/**
 * Test various linkings
 */
static void
prop_test1(void)
{
  prop_t *r = prop_create_root(NULL);

  prop_t *a1 = prop_create(r,  "node1");
  prop_t *a2 = prop_create(a1, "node2");
  prop_t *a3 = prop_create(a2, "node3");

  prop_t *b1 = prop_create_root("node1");
  prop_t *b2 = prop_create(b1,  "node2");
  prop_t *b3 = prop_create(b2,  "node3");

  prop_t *c2 = prop_create_root("node2");
  prop_t *c3 = prop_create(c2,  "node3");

  printf("a1=%p a2=%p a3=%p\n", a1, a2, a3);
  printf("b1=%p b2=%p b3=%p\n", b1, b2, b3);
  printf("      c2=%p c3=%p\n",     c2, c3);

  prop_set_int(a3, 1);
  prop_set_int(b3, 2);
  prop_set_int(c3, 3);

  prop_subscribe(PROP_SUB_INTERNAL,
                 PROP_TAG_CALLBACK_INT, set_testval, NULL,
                 PROP_TAG_ROOT, a3,
                 NULL);

  CHECKTESTVAL(1);

  // Check simple link and unlink

  prop_link(b1, a1);
  CHECKTESTVAL(2);

  prop_unlink(a1);
  CHECKTESTVAL(1);

  // Check again

  prop_link(b1, a1);
  CHECKTESTVAL(2);

  prop_unlink(a1);
  CHECKTESTVAL(1);

  // Check link one level down

  prop_link(c2, a2);
  CHECKTESTVAL(3);

  prop_unlink(a2);
  CHECKTESTVAL(1);

  // Check that more specific link is not changed when a link
  // higher up is made

  prop_link(c2, a2);
  CHECKTESTVAL(3);

  prop_link(b1, a1);
  CHECKTESTVAL(3);

  prop_unlink(a1);
  CHECKTESTVAL(3);

  prop_unlink(a2);
  CHECKTESTVAL(1);

  // Check that more specific link is not changed when a link
  // higher up is made and unlink is done in reverse order

  prop_link(c2, a2);
  CHECKTESTVAL(3);

  prop_link(b1, a1);
  CHECKTESTVAL(3);

  prop_unlink(a2);
  CHECKTESTVAL(1);

  prop_unlink(a1);
  CHECKTESTVAL(1);



  // ...

  prop_link(b1, a1);
  CHECKTESTVAL(2);

  prop_link(c2, b2);
  CHECKTESTVAL(3);

  prop_unlink(b2);
  CHECKTESTVAL(2);

  prop_link(c2, b2);
  CHECKTESTVAL(3);

  prop_unlink(a1);
  CHECKTESTVAL(1);

  prop_unlink(b2);
  CHECKTESTVAL(1);



  // ...

  prop_link(c2, b2);
  CHECKTESTVAL(1);

  prop_link(b1, a1);
  CHECKTESTVAL(3);

  prop_unlink(b2);
  CHECKTESTVAL(2);

  prop_link(c2, b2);
  CHECKTESTVAL(3);

  prop_unlink(a1);
  CHECKTESTVAL(1);

  prop_unlink(b2);
  CHECKTESTVAL(1);


  // ...

  prop_link(c2, b2);
  CHECKTESTVAL(1);

  prop_link(b1, a1);
  CHECKTESTVAL(3);

  prop_unlink(a1);
  CHECKTESTVAL(1);

  prop_link(b1, a1);
  CHECKTESTVAL(3);

  prop_unlink(a1);
  CHECKTESTVAL(1);

  prop_unlink(b2);
  CHECKTESTVAL(1);


  // ...

  prop_link(c2, b2);
  CHECKTESTVAL(1);

  prop_link(b1, a1);
  CHECKTESTVAL(3);

  prop_unlink(a1);
  CHECKTESTVAL(1);

  prop_link(b1, a1);
  CHECKTESTVAL(3);

  prop_unlink(b2);
  CHECKTESTVAL(2);

  prop_unlink(a1);
  CHECKTESTVAL(1);
}


/**
 * Tests a complex linking that happes around the navigator
 */
static void
prop_test2(void)
{
  printf("Running test 2\n");
  int i;

  prop_t *r = prop_create_root(NULL);

  prop_t *pages       = prop_create(r, "pages");
  prop_t *currentpage = prop_create(r, "currentpage");

#define NUM_PAGES 8

  prop_t *page[NUM_PAGES];
  prop_t *model[NUM_PAGES];
  prop_t *origin[NUM_PAGES];

  for(i = 0; i < NUM_PAGES; i++) {
    page[i] = prop_create(pages, NULL);
    model[i] = prop_create(page[i], "model");
    origin[i] = prop_create(page[i], "origin");
    prop_set(model[i], "loading", PROP_SET_INT, i + 1);
  }

  prop_subscribe(PROP_SUB_INTERNAL,
                 PROP_TAG_NAME("top", "currentpage", "model", "loading"),
                 PROP_TAG_CALLBACK_INT, set_testval, NULL,
                 PROP_TAG_NAMED_ROOT, r, "top",
                 NULL);

  CHECKTESTVAL(0);

  for(i = 0; i < NUM_PAGES; i++) {
    printf("Setting page %d\n", i);
    prop_link(page[i], currentpage);
    CHECKTESTVAL(i + 1);
  }

  // Rewind (go back)

  for(i = NUM_PAGES - 1; i >= 0; i--) {
    printf("Setting page %d\n", i);
    prop_link(page[i], currentpage);
    CHECKTESTVAL(i + 1);
  }

  prop_unlink(currentpage);
  CHECKTESTVAL(0);

  /**
   * Now, let each page contain a link to the previous page's model
   */

  for(i = 1; i < NUM_PAGES; i++) {
    prop_link(model[i - 1], origin[i]);
  }

  CHECKTESTVAL(0);

  for(i = 0; i < NUM_PAGES; i++) {
    printf("Setting page %d\n", i);
    prop_link(page[i], currentpage);
    CHECKTESTVAL(i + 1);
  }

  // Rewind (go back)

  for(i = NUM_PAGES - 1; i >= 0; i--) {
    printf("Setting page %d\n", i);
    prop_link(page[i], currentpage);
    prop_print_tree(currentpage, 7);
    CHECKTESTVAL(i + 1);
  }

  prop_unlink(currentpage);
  CHECKTESTVAL(0);


}



/**
 *
 */
void
prop_test(void)
{
  prop_test1();
  prop_test2();
}
#endif
