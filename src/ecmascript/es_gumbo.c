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
#include <assert.h>

#include "ext/gumbo-parser/src/gumbo.h"
#include "ext/gumbo-parser/src/error.h"
#include "ecmascript.h"
#include "misc/str.h"

typedef struct {
  atomic_t ego_refcount;
  GumboOutput *ego_output;
} es_gumbo_output_t;


typedef struct es_gumbo_node {
  GumboNode *node;
  es_gumbo_output_t *output;
} es_gumbo_node_t;


/**
 *
 */
static void
es_gumbo_output_release(es_gumbo_output_t *ego)
{
  if(atomic_dec(&ego->ego_refcount))
    return;

  gumbo_destroy_output(&kGumboDefaultOptions, ego->ego_output);
  free(ego);
}


/**
 *
 */
static es_gumbo_node_t *
es_gumbo_node_create(GumboNode *n, es_gumbo_output_t *ego)
{
  es_gumbo_node_t *egn = malloc(sizeof(es_gumbo_node_t));
  egn->node = n;
  egn->output = ego;
  atomic_inc(&ego->ego_refcount);
  return egn;
}



/**
 *
 */
static void
es_gumbo_node_release(es_gumbo_node_t *n)
{
  es_gumbo_output_release(n->output);
  free(n);
}

ES_NATIVE_CLASS(gumbo_node, &es_gumbo_node_release);

/**
 *
 */
static void
push_gumbo_node(duk_context *ctx, GumboNode *n, es_gumbo_output_t *ego)
{
  es_push_native_obj(ctx, &es_native_gumbo_node, es_gumbo_node_create(n, ego));
}


/**
 *
 */
static int
es_gumbo_parse(duk_context *ctx)
{
  duk_size_t len;
  const char *str = duk_to_lstring(ctx, 0, &len);
  es_gumbo_output_t *ego = calloc(1, sizeof(es_gumbo_output_t));
  atomic_set(&ego->ego_refcount, 1);
  ego->ego_output =
    gumbo_parse_with_options(&kGumboDefaultOptions, str, len);

  /*
  for(int i = 0; i < ego->ego_output->errors.length; i++) {
    GumboError *ge = ego->ego_output->errors.data[i];
  }
  */
  duk_pop(ctx);

  duk_push_object(ctx);

  push_gumbo_node(ctx, ego->ego_output->document, ego);
  duk_put_prop_string(ctx, -2, "document");

  push_gumbo_node(ctx, ego->ego_output->root, ego);
  duk_put_prop_string(ctx, -2, "root");

  es_gumbo_output_release(ego);
  return 1;
}


/**
 *
 */
static int
es_gumbo_node_type(duk_context *ctx)
{
  es_gumbo_node_t *egn = es_get_native_obj(ctx, 0, &es_native_gumbo_node);
  int o = -1;
  switch(egn->node->type) {
  case GUMBO_NODE_DOCUMENT:   o = 9; break;
  case GUMBO_NODE_TEMPLATE:
  case GUMBO_NODE_ELEMENT:    o = 1; break;
  case GUMBO_NODE_WHITESPACE:
  case GUMBO_NODE_CDATA:
  case GUMBO_NODE_TEXT:       o = 3; break;
  case GUMBO_NODE_COMMENT:    o = 8; break;
  }
  duk_push_int(ctx, o);
  return 1;
}


/**
 *
 */
static int
es_gumbo_node_name(duk_context *ctx)
{
  es_gumbo_node_t *egn = es_get_native_obj(ctx, 0, &es_native_gumbo_node);
  const GumboNode *node = egn->node;
  switch(node->type) {
  case GUMBO_NODE_DOCUMENT:
    duk_push_string(ctx, node->v.document.name);
    break;
  case GUMBO_NODE_ELEMENT:
    duk_push_string(ctx, gumbo_normalized_tagname(node->v.element.tag));
    break;
  default:
    duk_push_string(ctx, node->v.text.text);
    break;
  }

  return 1;
}


/**
 *
 */
static int
es_gumbo_node_childs(duk_context *ctx)
{
  es_gumbo_node_t *egn = es_get_native_obj(ctx, 0, &es_native_gumbo_node);
  int all = duk_get_boolean(ctx, 1);
  const GumboNode *node = egn->node;
  duk_push_array(ctx);

  if(node->type == GUMBO_NODE_ELEMENT || node->type == GUMBO_NODE_TEMPLATE) {
    const GumboElement *e = &node->v.element;
    int num = 0;
    for(int i = 0; i < e->children.length; i++) {
      GumboNode *child = e->children.data[i];
      if(child->type == GUMBO_NODE_WHITESPACE)
        continue;
      if(!all) {
        if(child->type == GUMBO_NODE_TEXT ||
           child->type == GUMBO_NODE_CDATA ||
           child->type == GUMBO_NODE_COMMENT)
          continue;
      }
      push_gumbo_node(ctx, child, egn->output);
      duk_put_prop_index(ctx, -2, num++);
    }
  }
  return 1;
}


/**
 *
 */
static int
es_gumbo_node_attributes(duk_context *ctx)
{
  es_gumbo_node_t *egn = es_get_native_obj(ctx, 0, &es_native_gumbo_node);
  const GumboNode *node = egn->node;
  duk_push_array(ctx);

  if(node->type == GUMBO_NODE_ELEMENT || node->type == GUMBO_NODE_TEMPLATE) {
    const GumboElement *e = &node->v.element;
    for(int i = 0; i < e->attributes.length; i++) {
      GumboAttribute *attrib = e->attributes.data[i];
      duk_push_object(ctx);

      duk_push_string(ctx, attrib->name);
      duk_put_prop_string(ctx, -2, "name");
      duk_push_string(ctx, attrib->value);
      duk_put_prop_string(ctx, -2, "value");
      duk_put_prop_index(ctx, -2, i);
    }
  }
  return 1;
}

static int
es_gumbo_node_textContent_r(duk_context *ctx, const GumboNode *node)
{
  int sum = 0;
  if(node->type == GUMBO_NODE_ELEMENT || node->type == GUMBO_NODE_TEMPLATE) {
    const GumboElement *e = &node->v.element;
    for(int i = 0; i < e->children.length; i++) {
      GumboNode *child = e->children.data[i];

      switch(child->type) {
      case GUMBO_NODE_TEXT:
      case GUMBO_NODE_CDATA:
      case GUMBO_NODE_COMMENT:
        duk_push_string(ctx, child->v.text.text);
        sum++;
        break;
      case GUMBO_NODE_ELEMENT:
      case GUMBO_NODE_TEMPLATE:
        sum += es_gumbo_node_textContent_r(ctx, child);
        break;
      default:
        break;
      }
    }
  }
  return sum;
}

/**
 *
 */
static int
es_gumbo_node_textContent(duk_context *ctx)
{
  es_gumbo_node_t *egn = es_get_native_obj(ctx, 0, &es_native_gumbo_node);
  const GumboNode *node = egn->node;
  int num = es_gumbo_node_textContent_r(ctx, node);
  if(num == 0)
    return 0;
  duk_concat(ctx, num);
  return 1;
}


/**
 *
 */
static GumboNode *
es_gumbo_find_by_id_r(GumboNode *node, const char *id)
{
  if(node->type != GUMBO_NODE_ELEMENT && node->type != GUMBO_NODE_TEMPLATE)
    return NULL;

  const GumboElement *e = &node->v.element;
  GumboAttribute *a = gumbo_get_attribute(&e->attributes, "id");

  if(a != NULL && !strcmp(a->value, id))
    return node;

  for(int i = 0; i < e->children.length; i++) {
    GumboNode *r = es_gumbo_find_by_id_r(e->children.data[i], id);
    if(r != NULL)
      return r;
  }
  return NULL;
}


/**
 *
 */
static int
es_gumbo_find_by_id(duk_context *ctx)
{
  es_gumbo_node_t *egn = es_get_native_obj(ctx, 0, &es_native_gumbo_node);
  const char *id = duk_to_string(ctx, 1);
  GumboNode *r = es_gumbo_find_by_id_r(egn->node, id);
  if(r == NULL)
    return 0;
  push_gumbo_node(ctx, r, egn->output);
  return 1;
}


/**
 *
 */
static void
es_gumbo_find_by_tag_name_r(GumboNode *node, GumboTag tag, duk_context *ctx,
                            int *idxp, es_gumbo_output_t *ego)
{
  if(node->type != GUMBO_NODE_ELEMENT && node->type != GUMBO_NODE_TEMPLATE)
    return;

  const GumboElement *e = &node->v.element;
  if(e->tag == tag) {
    push_gumbo_node(ctx, node, ego);
    duk_put_prop_index(ctx, -2, (*idxp)++);
  }

  for(int i = 0; i < e->children.length; i++)
    es_gumbo_find_by_tag_name_r(e->children.data[i], tag, ctx, idxp, ego);
}


/**
 *
 */
static int
es_gumbo_find_by_tag_name(duk_context *ctx)
{
  es_gumbo_node_t *egn = es_get_native_obj(ctx, 0, &es_native_gumbo_node);
  const char *tagstr = duk_to_string(ctx, 1);
  GumboTag tag = gumbo_tag_enum(tagstr);
  if(tag == GUMBO_TAG_UNKNOWN)
    duk_error(ctx, DUK_ERR_ERROR, "Unknown tag %s", tagstr);
  int idx = 0;
  duk_push_array(ctx);
  es_gumbo_find_by_tag_name_r(egn->node, tag, ctx, &idx, egn->output);
  return 1;
}


/**
 *
 */
static void
es_gumbo_find_by_class_r(GumboNode *node, char **classes, duk_context *ctx,
                         int *idxp, es_gumbo_output_t *ego)
{
  if(node->type != GUMBO_NODE_ELEMENT && node->type != GUMBO_NODE_TEMPLATE)
    return;

  const GumboElement *e = &node->v.element;
  GumboAttribute *a = gumbo_get_attribute(&e->attributes, "class");

  if(a != NULL) {
    char **list = strvec_split(a->value, ' ');
    for(int i = 0; classes[i] != NULL; i++) {
      int found = 0;
      for(int j = 0; list[j] != NULL; j++) {
        if(!strcmp(list[j], classes[i])) {
          found = 1;
          break;
        }
      }
      if(!found)
        goto notfound;
    }
    push_gumbo_node(ctx, node, ego);
    duk_put_prop_index(ctx, -2, (*idxp)++);

  notfound:
    strvec_free(list);
  }

  for(int i = 0; i < e->children.length; i++)
    es_gumbo_find_by_class_r(e->children.data[i], classes, ctx, idxp, ego);
}


/**
 *
 */
static int
es_gumbo_find_by_class(duk_context *ctx)
{
  es_gumbo_node_t *egn = es_get_native_obj(ctx, 0, &es_native_gumbo_node);
  const char *cls = duk_to_string(ctx, 1);
  int idx = 0;
  duk_push_array(ctx);
  char **classlist = strvec_split(cls, ' ');
  es_gumbo_find_by_class_r(egn->node, classlist, ctx, &idx, egn->output);
  strvec_free(classlist);
  return 1;
}



static const duk_function_list_entry fnlist_gumbo[] = {
  { "parse",      es_gumbo_parse, 1 },
  { "nodeType",   es_gumbo_node_type, 1 },
  { "nodeName",   es_gumbo_node_name, 1 },
  { "nodeChilds", es_gumbo_node_childs, 2 },
  { "nodeAttributes", es_gumbo_node_attributes, 1 },
  { "nodeTextContent", es_gumbo_node_textContent, 1 },
  { "findById",   es_gumbo_find_by_id, 2 },
  { "findByTagName", es_gumbo_find_by_tag_name, 2 },
  { "findByClassName", es_gumbo_find_by_class, 2 },
  { NULL, NULL, 0}
};

ES_MODULE("gumbo", fnlist_gumbo);
