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

#include "config.h"
#if ENABLE_PLUGINS
struct prop;

void plugins_init(char **devplugins);

void plugins_load_all(void);

int plugins_upgrade_check(void);

void plugin_open_file(struct prop *page, const char *url);

void plugins_reload_dev_plugin(void);

void plugin_props_from_file(struct prop *prop, const char *zipfile);

void plugin_select_view(const char *plugin_id, const char *filename);

void plugin_uninstall(const char *id);

struct fa_handle;
void plugin_probe_for_autoinstall(struct fa_handle *fh, const uint8_t *buf,
                                  size_t len, const char *url);

int plugin_check_prefix_for_autoinstall(const char *url);

#endif
