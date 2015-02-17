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
