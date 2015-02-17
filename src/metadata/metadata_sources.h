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
#include "metadata.h"

/**
 *
 */
typedef struct metadata_source {
  TAILQ_ENTRY(metadata_source) ms_link;
  char *ms_name;
  char *ms_description;
  int ms_prio;
  int ms_id;
  int ms_enabled;
  int ms_type;

  const metadata_source_funcs_t *ms_funcs;
  struct prop *ms_settings;

  int64_t ms_cfgid;

  uint64_t ms_partial_props;
  uint64_t ms_complete_props;
} metadata_source_t;

extern struct metadata_source_queue metadata_sources[METADATA_TYPE_num];

extern hts_mutex_t metadata_sources_mutex;

void metadata_sources_init(void);

metadata_source_t *metadata_add_source(const char *name,
				       const char *description,
				       int default_prio, metadata_type_t type,
				       const metadata_source_funcs_t *funcs,
				       uint64_t partials,
				       uint64_t complete);

const metadata_source_t *metadata_source_get(metadata_type_t type, int id);
