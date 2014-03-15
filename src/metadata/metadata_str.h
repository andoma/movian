#pragma once

#include "misc/rstr.h"

void metadata_filename_to_title(const char *filename,
                                int *yearp, rstr_t **titlep);

int metadata_filename_to_episode(const char *filename,
                                 int *season, int *episode,
                                 rstr_t **titlep);

int metadata_folder_to_season(const char *s,
                              int *seasonp, rstr_t **titlep);

int is_reasonable_movie_name(const char *s);

rstr_t *metadata_remove_postfix_rstr(rstr_t *in);

rstr_t *metadata_remove_postfix(const char *in);
