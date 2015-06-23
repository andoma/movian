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
#include <stdint.h>

#define RB_TREE_NODE_RED      1
#define RB_TREE_NODE_BLACK    0

#define RB_HEAD(name, type)			\
struct name {					\
  struct type *first, *last, *root;		\
}

// Head without first and last pointers
#define RB_HEAD_NFL(name, type)			\
struct name {					\
  struct type *root;                            \
}


#define RB_ENTRY(type)				\
struct {					\
  struct type *left, *right, *parent;           \
  int color;                                    \
}

#define RB_SP(node, field, p)    (node)->field.parent = (p)
#define RB_GP(node, field)      ((node)->field.parent)
#define RB_SC(node, field, col)  (node)->field.color = (col)
#define RB_GC(node, field)      ((node)->field.color)


#define RB_INIT(head)				\
do {						\
  (head)->first   = NULL;			\
  (head)->last    = NULL;			\
  (head)->root    = NULL;			\
} while(0)

#define RB_INIT_NFL(head)                       \
do {						\
  (head)->root    = NULL;			\
} while(0)


#define RB_ROTATE_LEFT(x, field, root)		\
do {						\
  typeof(x) xx = x;                             \
  typeof(x) yy = xx->field.right;		\
						\
  xx->field.right = yy->field.left;		\
  if(yy->field.left != NULL) {                  \
    RB_SP(yy->field.left, field, xx);           \
  }                                             \
  RB_SP(yy, field, RB_GP(xx, field));           \
						\
  if(xx == root) {				\
    root = yy;					\
  } else if(xx == RB_GP(xx,field)->field.left) {	\
    RB_GP(xx,field)->field.left = yy;           \
  } else { 					\
    RB_GP(xx,field)->field.right = yy;          \
  }                                             \
  yy->field.left = xx;				\
  RB_SP(xx, field, yy);                         \
} while(0)
  

#define RB_ROTATE_RIGHT(x, field, root)			\
do {							\
  typeof(x) xx = x;					\
  typeof(x) yy = xx->field.left;			\
							\
  xx->field.left = yy->field.right;			\
  if (yy->field.right != NULL) {   			\
    RB_SP(yy->field.right, field, xx);                  \
  }                                                     \
  RB_SP(yy, field, RB_GP(xx, field));                   \
                                                        \
  if (xx == root) {    					\
    root = yy;						\
  } else if (xx == RB_GP(xx, field)->field.right) {  	\
    RB_GP(xx, field)->field.right = yy;                 \
  } else {  						\
    RB_GP(xx, field)->field.left = yy;                  \
  }                                                     \
  yy->field.right = xx;                                 \
  RB_SP(xx, field, yy);                                 \
} while(0)

#define RB_INSERT_BALANCE(x, field, root) do {                          \
    typeof(x) y,p;                                                      \
                                                                        \
    RB_SC(x, field, RB_TREE_NODE_RED);                                  \
    while (x != root && RB_GC(RB_GP(x, field), field) == RB_TREE_NODE_RED) { \
      p = RB_GP(x, field);                                              \
      if (p == RB_GP(p, field)->field.left) {                           \
        y = RB_GP(p, field)->field.right;                               \
        if (y && RB_GC(y, field) == RB_TREE_NODE_RED) {                 \
          RB_SC(p, field, RB_TREE_NODE_BLACK);                          \
          RB_SC(y, field, RB_TREE_NODE_BLACK);                          \
          RB_SC(RB_GP(p, field), field, RB_TREE_NODE_RED);              \
          x = RB_GP(p, field);                                          \
        } else {                                                        \
          if (x == p->field.right) {                                    \
          x = p;                                                        \
          RB_ROTATE_LEFT(x, field, root);                               \
          p = RB_GP(x, field);                                          \
        }                                                               \
          RB_SC(p, field, RB_TREE_NODE_BLACK);                          \
          RB_SC(RB_GP(p, field), field, RB_TREE_NODE_RED);              \
          RB_ROTATE_RIGHT(RB_GP(p, field), field, root);                \
      }                                                                 \
    } else {                                                            \
        y = RB_GP(p, field)->field.left;                                \
        if (y && RB_GC(y, field) ==  RB_TREE_NODE_RED) {                \
          RB_SC(p, field, RB_TREE_NODE_BLACK);                          \
          RB_SC(y, field, RB_TREE_NODE_BLACK);                          \
          RB_SC(RB_GP(p, field), field, RB_TREE_NODE_RED);              \
          x = RB_GP(p, field);                                          \
        } else {                                                        \
          if (x == p->field.left) {                                     \
            x = p;                                                      \
            RB_ROTATE_RIGHT(x, field, root);                            \
            p = RB_GP(x, field);                                        \
          }                                                             \
          RB_SC(p, field, RB_TREE_NODE_BLACK);                          \
          RB_SC(RB_GP(p, field), field, RB_TREE_NODE_RED);              \
          RB_ROTATE_LEFT(RB_GP(p, field), field, root);                 \
        }                                                               \
      }                                                                 \
  }                                                                     \
    RB_SC(root, field, RB_TREE_NODE_BLACK);                             \
} while(0);

/**
 * Insert a new node, if a collision occures the colliding node is returned
 */
#define RB_INSERT_SORTED(head, skel, field, cmpfunc)			 \
({									 \
  int res, fromleft = 0;						 \
  typeof(skel) x = skel, c, parent = NULL;				 \
									 \
  c = (head)->root;							 \
  while(c != NULL) {							 \
    res = cmpfunc(x, c);						 \
    if(res < 0) {							 \
      parent = c;							 \
      c = c->field.left;						 \
      fromleft = 1;							 \
    } else if(res > 0) {						 \
      parent = c;							 \
      c = c->field.right;						 \
      fromleft = 0;							 \
    } else {								 \
      break;								 \
    }									 \
  }									 \
  if(c == NULL) {							 \
    RB_SP(x, field, parent);                                             \
    x->field.left = NULL;						 \
    x->field.right = NULL;						 \
    RB_SC(x, field, RB_TREE_NODE_RED);                                  \
									 \
    if(parent) {							 \
      if(fromleft)							 \
	parent->field.left = x;						 \
      else								 \
	parent->field.right = x;					 \
    } else {								 \
      (head)->root = x;							 \
    }									 \
                                                                        \
    if(RB_GP(x, field) == (head)->first &&                              \
       (RB_GP(x, field) == NULL || RB_GP(x, field)->field.left == x)) { \
      (head)->first = x;                                                \
    }                                                                   \
									 \
    if(RB_GP(x, field) == (head)->last &&                               \
       (RB_GP(x, field) == NULL || RB_GP(x, field)->field.right == x)) { \
      (head)->last = x;							 \
    }									 \
    RB_INSERT_BALANCE(x, field, (head)->root);                           \
  }									 \
  c;									 \
})

/**
 * Insert a new node, if a collision occures the colliding node is returned
 */
#define RB_INSERT_SORTED_NFL(head, skel, field, cmpfunc)                 \
({									 \
  int res, fromleft = 0;						 \
  typeof(skel) x = skel, c, parent = NULL;				 \
									 \
  c = (head)->root;							 \
  while(c != NULL) {							 \
    res = cmpfunc(x, c);						 \
    if(res < 0) {							 \
      parent = c;							 \
      c = c->field.left;						 \
      fromleft = 1;							 \
    } else if(res > 0) {						 \
      parent = c;							 \
      c = c->field.right;						 \
      fromleft = 0;							 \
    } else {								 \
      break;								 \
    }									 \
  }									 \
  if(c == NULL) {							 \
    RB_SP(x, field, parent);                                             \
    x->field.left = NULL;						 \
    x->field.right = NULL;						 \
    RB_SC(x, field, RB_TREE_NODE_RED);                                  \
									 \
    if(parent) {							 \
      if(fromleft)							 \
	parent->field.left = x;						 \
      else								 \
	parent->field.right = x;					 \
    } else {								 \
      (head)->root = x;							 \
    }									 \
    RB_INSERT_BALANCE(x, field, (head)->root);                           \
  }									 \
  c;									 \
})

/**
 * Returns next node
 */
#define RB_NEXT(e, field)			\
({						\
  typeof(e) xx = e, f;				\
  if (xx->field.right != NULL) {		\
    xx = xx->field.right;			\
    while (xx->field.left != NULL) {		\
      xx = xx->field.left;			\
    }						\
  }						\
  else {					\
    do {					\
      f = xx;					\
      xx = RB_GP(xx, field);                    \
    } while (xx && f == xx->field.right);	\
  }						\
  xx;						\
})


/**
 * Returns previous node
 */
#define RB_PREV(e, field)			\
({						\
  typeof(e) xx = e, f;				\
  if (xx->field.left != NULL) {			\
    xx = xx->field.left;			\
    while (xx->field.right != NULL) {		\
      xx = xx->field.right;			\
    }						\
  }						\
  else {					\
    do {					\
      f = xx;					\
      xx = RB_GP(xx, field);                    \
    } while (xx && f == xx->field.left);	\
  }						\
  xx;						\
})


/**
 * Returns first node
 */
#define RB_FIRST(head) ((head)->first)

/**
 * Returns last node
 */
#define RB_LAST(head)  ((head)->last)

/**
 * Iterate thru all nodes
 */
#define RB_FOREACH(e, head, field)		\
 for(e = (head)->first; e != NULL; 		\
       ({					\
	 if(e->field.right != NULL) {		\
	   e = e->field.right;			\
	   while(e->field.left != NULL) {	\
	     e = e->field.left;			\
	   }					\
	 } else {				\
	   typeof(e) f;				\
	   do {					\
	     f = e;				\
	     e = RB_GP(e, field);		\
	   } while(e && f == e->field.right);	\
	 }					\
       }))


/**
 * Iterate thru all nodes in reverse order
 */
#define RB_FOREACH_REVERSE(e, head, field)	\
 for(e = (head)->last; e != NULL; 		\
       ({					\
	 if(e->field.left != NULL) {		\
	   e = e->field.left;			\
	   while(e->field.right != NULL) {	\
	     e = e->field.right;		\
	   }					\
	 } else {				\
	   typeof(e) f;				\
	   do {					\
	     f = e;				\
	     e = RB_GP(e, field);		\
	   } while(e && f == e->field.left);	\
	 }					\
       }))

/**
 * Remove the given node
 */
#define RB_REMOVE(head, e, field)					 \
do {									 \
  int swapColor;							 \
  typeof(e) x, y, z = e, x_parent, w;					 \
									 \
  y = z;								 \
  if (y == (head)->first) {						 \
    (head)->first = RB_NEXT(y, field);				         \
  }									 \
  if (y == (head)->last) {						 \
    (head)->last = RB_PREV(y, field);				         \
  }									 \
  if (y->field.left == NULL) {						 \
    x = y->field.right;							 \
  }									 \
  else {								 \
    if (y->field.right == NULL) {					 \
      x = y->field.left;						 \
    }									 \
    else {								 \
      y = y->field.right;						 \
      while (y->field.left != NULL) {					 \
	y = y->field.left;						 \
      }									 \
      x = y->field.right;						 \
    }									 \
  }									 \
  if (y != z) {								 \
    RB_SP(z->field.left, field, y);                                     \
    y->field.left = z->field.left;					 \
    if (y != z->field.right) {						 \
      x_parent = RB_GP(y, field);                                       \
      if (x != NULL) {							 \
	RB_SP(x, field, RB_GP(y, field));                               \
      }									 \
      RB_GP(y, field)->field.left = x;                                  \
      y->field.right = z->field.right;					 \
      RB_SP(z->field.right, field, y);                                  \
    }									 \
    else {								 \
      x_parent = y;							 \
    }									 \
    if ((head)->root == z) {						 \
      (head)->root = y;							 \
    }									 \
    else if (RB_GP(z, field)->field.left == z) {                        \
      RB_GP(z, field)->field.left = y;                                  \
    }									 \
    else {								 \
      RB_GP(z, field)->field.right = y;                                 \
    }									 \
    RB_SP(y, field, RB_GP(z, field));                                   \
									 \
    swapColor = RB_GC(y, field);                                        \
    RB_SC(y, field, RB_GC(z, field));                                   \
    RB_SC(z, field, swapColor);                                         \
									 \
    y = z;								 \
  }									 \
  else {								 \
    x_parent = RB_GP(y, field);                                         \
    if (x != NULL) {							 \
      RB_SP(x, field, RB_GP(y, field));                                 \
    }									 \
    if ((head)->root == z) {						 \
      (head)->root = x;							 \
    }									 \
    else {								 \
      if (RB_GP(z, field)->field.left == z) {                           \
	RB_GP(z, field)->field.left = x;                                \
      }									 \
      else {								 \
	RB_GP(z, field)->field.right = x;                               \
      }									 \
    }									 \
  }									 \
									 \
  if (RB_GC(y, field) != RB_TREE_NODE_RED) {                            \
    while (x != (head)->root && 					 \
	   (x == NULL || RB_GC(x, field) == RB_TREE_NODE_BLACK)) {      \
      if (x == x_parent->field.left) {					 \
	w = x_parent->field.right;					 \
	if (RB_GC(w, field) == RB_TREE_NODE_RED) {                      \
	  RB_SC(w, field, RB_TREE_NODE_BLACK);                          \
	  RB_SC(x_parent, field, RB_TREE_NODE_RED);                     \
	  RB_ROTATE_LEFT(x_parent, field, (head)->root);		 \
	  w = x_parent->field.right;					 \
	}								 \
	if ((w->field.left == NULL || 					 \
	     RB_GC(w->field.left, field) == RB_TREE_NODE_BLACK) &&      \
	    (w->field.right == NULL ||                                  \
	     RB_GC(w->field.right, field) == RB_TREE_NODE_BLACK)) {     \
									 \
	  RB_SC(w, field, RB_TREE_NODE_RED);                            \
	  x = x_parent;							 \
	  x_parent = RB_GP(x_parent, field);                            \
	} else {							 \
	  if (w->field.right == NULL || 				 \
	      RB_GC(w->field.right, field) == RB_TREE_NODE_BLACK) {     \
									 \
	    if (w->field.left) {					 \
	      RB_SC(w->field.left, field, RB_TREE_NODE_BLACK);          \
	    }								 \
	    RB_SC(w, field, RB_TREE_NODE_RED);                          \
	    RB_ROTATE_RIGHT(w, field, (head)->root);			 \
	    w = x_parent->field.right;					 \
	  }								 \
	  RB_SC(w, field, RB_GC(x_parent, field));                      \
	  RB_SC(x_parent, field, RB_TREE_NODE_BLACK);                   \
	  if (w->field.right) {						 \
	    RB_SC(w->field.right, field, RB_TREE_NODE_BLACK);           \
	  }								 \
	  RB_ROTATE_LEFT(x_parent, field, (head)->root);		 \
	  break;							 \
	}								 \
      }									 \
      else {								 \
	w = x_parent->field.left;					 \
	if (RB_GC(w, field) == RB_TREE_NODE_RED) {                      \
	  RB_SC(w, field, RB_TREE_NODE_BLACK);                        \
	  RB_SC(x_parent, field, RB_TREE_NODE_RED);                     \
	  RB_ROTATE_RIGHT(x_parent, field, (head)->root);		 \
	  w = x_parent->field.left;					 \
	}								 \
	if ((w->field.right == NULL || 					 \
	     RB_GC(w->field.right, field) == RB_TREE_NODE_BLACK) &&        \
	    (w->field.left == NULL || 					 \
	     RB_GC(w->field.left, field) == RB_TREE_NODE_BLACK)) {      \
									 \
	  RB_SC(w, field, RB_TREE_NODE_RED);                            \
	  x = x_parent;							 \
	  x_parent = RB_GP(x_parent, field);                            \
	}								 \
	else {								 \
	  if (w->field.left == NULL || 					 \
	      RB_GC(w->field.left, field) == RB_TREE_NODE_BLACK) {      \
									 \
	    if (w->field.right) {					 \
	      RB_SC(w->field.right, field, RB_TREE_NODE_BLACK);         \
	    }								 \
	    RB_SC(w, field, RB_TREE_NODE_RED);                          \
	    RB_ROTATE_LEFT(w, field, (head)->root);			 \
	    w = x_parent->field.left;					 \
	  }								 \
	  RB_SC(w, field, RB_GC(x_parent, field));                      \
	  RB_SC(x_parent, field, RB_TREE_NODE_BLACK);                   \
	  if (w->field.left) {						 \
	    RB_SC(w->field.left, field, RB_TREE_NODE_BLACK);            \
	  }								 \
	  RB_ROTATE_RIGHT(x_parent, field, (head)->root);		 \
	  break;							 \
	}								 \
      }									 \
    }									 \
    if (x) {								 \
      RB_SC(x, field, RB_TREE_NODE_BLACK);                              \
    }									 \
  }									 \
} while(0)

/**
 * Remove the given node
 */
#define RB_REMOVE_NFL(head, e, field)					 \
do {									 \
  int swapColor;							 \
  typeof(e) x, y, z = e, x_parent, w;					 \
									 \
  y = z;								 \
  if (y->field.left == NULL) {						 \
    x = y->field.right;							 \
  }									 \
  else {								 \
    if (y->field.right == NULL) {					 \
      x = y->field.left;						 \
    }									 \
    else {								 \
      y = y->field.right;						 \
      while (y->field.left != NULL) {					 \
	y = y->field.left;						 \
      }									 \
      x = y->field.right;						 \
    }									 \
  }									 \
  if (y != z) {								 \
    RB_SP(z->field.left, field, y);                                     \
    y->field.left = z->field.left;					 \
    if (y != z->field.right) {						 \
      x_parent = RB_GP(y, field);                                       \
      if (x != NULL) {							 \
	RB_SP(x, field, RB_GP(y, field));                               \
      }									 \
      RB_GP(y, field)->field.left = x;                                  \
      y->field.right = z->field.right;					 \
      RB_SP(z->field.right, field, y);                                  \
    }									 \
    else {								 \
      x_parent = y;							 \
    }									 \
    if ((head)->root == z) {						 \
      (head)->root = y;							 \
    }									 \
    else if (RB_GP(z, field)->field.left == z) {                        \
      RB_GP(z, field)->field.left = y;                                  \
    }									 \
    else {								 \
      RB_GP(z, field)->field.right = y;                                 \
    }									 \
    RB_SP(y, field, RB_GP(z, field));                                   \
									 \
    swapColor = RB_GC(y, field);                                        \
    RB_SC(y, field, RB_GC(z, field));                                   \
    RB_SC(z, field, swapColor);                                         \
									 \
    y = z;								 \
  }									 \
  else {								 \
    x_parent = RB_GP(y, field);                                         \
    if (x != NULL) {							 \
      RB_SP(x, field, RB_GP(y, field));                                 \
    }									 \
    if ((head)->root == z) {						 \
      (head)->root = x;							 \
    }									 \
    else {								 \
      if (RB_GP(z, field)->field.left == z) {                           \
	RB_GP(z, field)->field.left = x;                                \
      }									 \
      else {								 \
	RB_GP(z, field)->field.right = x;                               \
      }									 \
    }									 \
  }									 \
									 \
  if (RB_GC(y, field) != RB_TREE_NODE_RED) {                            \
    while (x != (head)->root && 					 \
	   (x == NULL || RB_GC(x, field) == RB_TREE_NODE_BLACK)) {      \
      if (x == x_parent->field.left) {					 \
	w = x_parent->field.right;					 \
	if (RB_GC(w, field) == RB_TREE_NODE_RED) {                      \
	  RB_SC(w, field, RB_TREE_NODE_BLACK);                          \
	  RB_SC(x_parent, field, RB_TREE_NODE_RED);                     \
	  RB_ROTATE_LEFT(x_parent, field, (head)->root);		 \
	  w = x_parent->field.right;					 \
	}								 \
	if ((w->field.left == NULL || 					 \
	     RB_GC(w->field.left, field) == RB_TREE_NODE_BLACK) &&      \
	    (w->field.right == NULL ||                                  \
	     RB_GC(w->field.right, field) == RB_TREE_NODE_BLACK)) {     \
									 \
	  RB_SC(w, field, RB_TREE_NODE_RED);                            \
	  x = x_parent;							 \
	  x_parent = RB_GP(x_parent, field);                            \
	} else {							 \
	  if (w->field.right == NULL || 				 \
	      RB_GC(w->field.right, field) == RB_TREE_NODE_BLACK) {     \
									 \
	    if (w->field.left) {					 \
	      RB_SC(w->field.left, field, RB_TREE_NODE_BLACK);          \
	    }								 \
	    RB_SC(w, field, RB_TREE_NODE_RED);                          \
	    RB_ROTATE_RIGHT(w, field, (head)->root);			 \
	    w = x_parent->field.right;					 \
	  }								 \
	  RB_SC(w, field, RB_GC(x_parent, field));                      \
	  RB_SC(x_parent, field, RB_TREE_NODE_BLACK);                   \
	  if (w->field.right) {						 \
	    RB_SC(w->field.right, field, RB_TREE_NODE_BLACK);           \
	  }								 \
	  RB_ROTATE_LEFT(x_parent, field, (head)->root);		 \
	  break;							 \
	}								 \
      }									 \
      else {								 \
	w = x_parent->field.left;					 \
	if (RB_GC(w, field) == RB_TREE_NODE_RED) {                      \
	  RB_SC(w, field, RB_TREE_NODE_BLACK);                        \
	  RB_SC(x_parent, field, RB_TREE_NODE_RED);                     \
	  RB_ROTATE_RIGHT(x_parent, field, (head)->root);		 \
	  w = x_parent->field.left;					 \
	}								 \
	if ((w->field.right == NULL || 					 \
	     RB_GC(w->field.right, field) == RB_TREE_NODE_BLACK) &&        \
	    (w->field.left == NULL || 					 \
	     RB_GC(w->field.left, field) == RB_TREE_NODE_BLACK)) {      \
									 \
	  RB_SC(w, field, RB_TREE_NODE_RED);                            \
	  x = x_parent;							 \
	  x_parent = RB_GP(x_parent, field);                            \
	}								 \
	else {								 \
	  if (w->field.left == NULL || 					 \
	      RB_GC(w->field.left, field) == RB_TREE_NODE_BLACK) {      \
									 \
	    if (w->field.right) {					 \
	      RB_SC(w->field.right, field, RB_TREE_NODE_BLACK);         \
	    }								 \
	    RB_SC(w, field, RB_TREE_NODE_RED);                          \
	    RB_ROTATE_LEFT(w, field, (head)->root);			 \
	    w = x_parent->field.left;					 \
	  }								 \
	  RB_SC(w, field, RB_GC(x_parent, field));                      \
	  RB_SC(x_parent, field, RB_TREE_NODE_BLACK);                   \
	  if (w->field.left) {						 \
	    RB_SC(w->field.left, field, RB_TREE_NODE_BLACK);            \
	  }								 \
	  RB_ROTATE_RIGHT(x_parent, field, (head)->root);		 \
	  break;							 \
	}								 \
      }									 \
    }									 \
    if (x) {								 \
      RB_SC(x, field, RB_TREE_NODE_BLACK);                              \
    }									 \
  }									 \
} while(0)



/**
 * Finds a node
 */
#define RB_FIND(head, skel, field, cmpfunc)	\
({						\
  int res;                                        \
  typeof(skel) c = (head)->root;		\
  while(c != NULL) {				\
    res = cmpfunc(skel, c);			\
    if(res < 0) {				\
      c = c->field.left;			\
    } else if(res > 0) {			\
      c = c->field.right;			\
    } else {					\
      break;					\
    }						\
  }						\
 c;						\
}) 



/**
 * Finds first node greater than 'skel'
 */
#define RB_FIND_GT(head, skel, field, cmpfunc)	  \
({						  \
  int res;                                        \
  typeof(skel) c = (head)->root, x = NULL;	  \
  while(c != NULL) {				  \
    res = cmpfunc(skel, c);			  \
    if(res < 0) {				  \
      x = c;					  \
      c = c->field.left;			  \
    } else if(res > 0) {			  \
      c = c->field.right;			  \
    } else {					  \
      x = RB_NEXT(c, field);			  \
      break;					  \
    }						  \
  }						  \
 x;						  \
})

/**
 * Finds a node greater or equal to 'skel'
 */
#define RB_FIND_GE(head, skel, field, cmpfunc)	  \
({						  \
  int res;                                        \
  typeof(skel) c = (head)->root, x = NULL;	  \
  while(c != NULL) {				  \
    res = cmpfunc(skel, c);			  \
    if(res < 0) {				  \
      x = c;					  \
      c = c->field.left;			  \
    } else if(res > 0) {			  \
      c = c->field.right;			  \
    } else {					  \
      x = c;                   			  \
      break;					  \
    }						  \
  }						  \
 x;						  \
})


/**
 * Finds first node lesser than 'skel'
 */
#define RB_FIND_LT(head, skel, field, cmpfunc)	  \
({						  \
  int res;                                        \
  typeof(skel) c = (head)->root, x = NULL;	  \
  while(c != NULL) {				  \
    res = cmpfunc(skel, c);			  \
    if(res < 0) {				  \
      c = c->field.left;			  \
    } else if(res > 0) {			  \
      x = c;					  \
      c = c->field.right;			  \
    } else {					  \
      x = RB_PREV(c, field);			  \
      break;					  \
    }						  \
  }						  \
 x;						  \
})

/**
 * Finds a node lesser or equal to 'skel'
 */
#define RB_FIND_LE(head, skel, field, cmpfunc)	  \
({						  \
  int res;                                        \
  typeof(skel) c = (head)->root, x = NULL;	  \
  while(c != NULL) {				  \
    res = cmpfunc(skel, c);			  \
    if(res < 0) {				  \
      c = c->field.left;			  \
    } else if(res > 0) {			  \
      x = c;					  \
      c = c->field.right;			  \
    } else {					  \
      x = c;                   			  \
      break;					  \
    }						  \
  }						  \
 x;						  \
})

