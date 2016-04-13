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
#ifndef NAVIGATOR_H__
#define NAVIGATOR_H__

#include "prop/prop.h"
#include "misc/rstr.h"

#define NAV_HOME "page:home"


/**
 *
 */
void nav_init(void);

void nav_fini(void);

prop_t *nav_spawn(void);

void nav_open(const char *url, const char *view);


int nav_open_error(prop_t *root, const char *msg);

int nav_open_errorf(prop_t *root, rstr_t *fmt, ...);

void nav_redirect(prop_t *root, const char *url);

#endif /* NAVIGATOR_H__ */
