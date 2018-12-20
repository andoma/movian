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
#include "prop/prop.h"
#include "htsmsg/htsmsg.h"

#define SETTINGS_INITIAL_UPDATE 0x1
#define SETTINGS_PASSWORD       0x2 // Make a password entry (hidden display)
#define SETTINGS_RAW_NODES      0x4
#define SETTINGS_FIRST          0x8 // Insert at head
#define SETTINGS_DEBUG          0x10
#define SETTINGS_FILE           0x20 // Want a path to a file
#define SETTINGS_DIR            0x40 // Want a path to a dir


typedef void (settings_saver_t)(void *opaque, htsmsg_t *htsmsg);

struct setting;
typedef struct setting setting_t;


prop_t *settings_add_dir(prop_t *parent, prop_t *title,
			 const char *subtype, const char *icon,
			 prop_t *shortdesc, const char *url);

prop_t *settings_add_dir_cstr(prop_t *parent, const char *title,
			      const char *subtype, const char *icon,
			      const char *shortdesc, const char *url);

void settings_add_url(prop_t *parent, prop_t *title,
                      const char *subtype, const char *icon,
                      prop_t *shortdesc, const char *url,
                      int flags);

void settings_create_separator(prop_t *parent, prop_t *caption);

void settings_add_int(setting_t *s, int delta);

int settings_get_type(const setting_t *s);

setting_t *settings_create_action(prop_t *parent, prop_t *title,
                                  const char *subtype,
				  prop_callback_t *cb, void *opaque,
				  int flags, prop_courier_t *pc);

void setting_destroy(setting_t *s);

void setting_destroyp(setting_t **sp);

void setting_detach(setting_t *s);

void settings_init(void);

void settings_create_info(prop_t *parent, const char *image,
			  prop_t *description);


void settings_create_bound_string(prop_t *parent, prop_t *title,
                                  prop_t *value);

prop_t *makesep(prop_t *title);

LIST_HEAD(setting_list, setting);

enum {
  SETTING_TAG_TITLE = 1,
  SETTING_TAG_TITLE_CSTR,
  SETTING_TAG_VALUE,
  SETTING_TAG_VALUE_PROP,
  SETTING_TAG_CALLBACK,
  SETTING_TAG_COURIER,
  SETTING_TAG_STORE,
  SETTING_TAG_RANGE,
  SETTING_TAG_STEP,
  SETTING_TAG_UNIT_CSTR,
  SETTING_TAG_OPTION,
  SETTING_TAG_OPTION_CSTR,
  SETTING_TAG_OPTION_LIST,
  SETTING_TAG_WRITE_INT,
  SETTING_TAG_ZERO_TEXT,
  SETTING_TAG_MUTEX,
  SETTING_TAG_LOCKMGR,
  SETTING_TAG_WRITE_PROP,
  SETTING_TAG_PROP_ENABLER,
  SETTING_TAG_KVSTORE,
  SETTING_TAG_VALUE_ORIGIN,
  SETTING_TAG_GROUP,
  SETTING_TAG_INHERIT,
};

#define SETTING_TITLE(a)                        SETTING_TAG_TITLE, a
#define SETTING_TITLE_CSTR(a)                   SETTING_TAG_TITLE_CSTR, a
#define SETTING_VALUE(a)                        SETTING_TAG_VALUE, a
#define SETTING_VALUE_PROP(a)                   SETTING_TAG_VALUE_PROP, a
#define SETTING_CALLBACK(a, b)                  SETTING_TAG_CALLBACK, a, b
#define SETTING_COURIER(a)                      SETTING_TAG_COURIER, a
#define SETTING_STORE(a, b)                     SETTING_TAG_STORE, a, b
#define SETTING_RANGE(a, b)                     SETTING_TAG_RANGE, a, b
#define SETTING_STEP(a)                         SETTING_TAG_STEP, a
#define SETTING_UNIT_CSTR(a)                    SETTING_TAG_UNIT_CSTR, a
#define SETTING_OPTION(a, b)                    SETTING_TAG_OPTION, a, b
#define SETTING_OPTION_CSTR(a, b)               SETTING_TAG_OPTION_CSTR, a, b
#define SETTING_OPTION_LIST(a)                  SETTING_TAG_OPTION_LIST, a
#define SETTING_WRITE_BOOL(a)                   SETTING_TAG_WRITE_INT, a
#define SETTING_WRITE_INT(a)                    SETTING_TAG_WRITE_INT, a
#define SETTING_ZERO_TEXT(a)                    SETTING_TAG_ZERO_TEXT, a
#define SETTING_MUTEX(a)                        SETTING_TAG_MUTEX, a
#define SETTING_LOCKMGR(a)                      SETTING_TAG_LOCKMGR, a
#define SETTING_WRITE_PROP(a)                   SETTING_TAG_WRITE_PROP, a
#define SETTING_KVSTORE(a, b)                   SETTING_TAG_KVSTORE, a, b
#define SETTING_PROP_ENABLER(a)                 SETTING_TAG_PROP_ENABLER, a
#define SETTING_VALUE_ORIGIN(a)                 SETTING_TAG_VALUE_ORIGIN, a
#define SETTING_GROUP(a)                        SETTING_TAG_GROUP, a
#define SETTING_INHERIT(a)                      SETTING_TAG_INHERIT, a
#define SETTING_END                             NULL


enum {
  SETTING_INT,
  SETTING_STRING,
  SETTING_BOOL,
  SETTING_MULTIOPT,
  SETTING_ACTION,
  SETTING_SEPARATOR,
};

setting_t *setting_create(int type, prop_t *parent, int flags, ...)
  attribute_null_sentinel;

prop_t *setting_add_option(setting_t *s, const char *id,
                           const char *title, int sel);

void setting_set(setting_t *s, int type, ...);

void setting_reset(setting_t *s);

void setting_push_to_ancestor(setting_t *s, const char *ancestor);

void setting_group_destroy(struct setting_list *list);

void setting_group_push_to_ancestor(struct setting_list *list,
                                    const char *ancestor);

void setting_group_reset(struct setting_list *list);

prop_t *setting_get_dir(const char *url);
