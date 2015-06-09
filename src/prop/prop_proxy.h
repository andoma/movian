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

typedef struct prop_proxy_connection prop_proxy_connection_t;
struct prop_sub;
struct prop;
struct net_addr;
struct prop_sub;

struct prop *prop_proxy_connect(struct net_addr *addr);

prop_proxy_connection_t *ppc_retain(prop_proxy_connection_t *ppc);

void ppc_release(prop_proxy_connection_t *ppc);

struct prop *prop_proxy_make(prop_proxy_connection_t *ppc, uint32_t id,
                             struct prop_sub *s, char **pfx);

void prop_proxy_subscribe(prop_proxy_connection_t *ppc, struct prop_sub *s,
                          struct prop *p, const char **name);

void prop_proxy_unsubscribe(struct prop_sub *s);

void prop_proxy_destroy(struct prop *p);
