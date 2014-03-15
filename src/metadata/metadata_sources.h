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
