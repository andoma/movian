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
struct prop;
struct rstr;

void kvstore_init(void);

void kvstore_fini(void);

void kv_prop_bind_create(struct prop *p, const char *url);

// Direct access

#define KVSTORE_DOMAIN_SYS     1
#define KVSTORE_DOMAIN_PROP    2
#define KVSTORE_DOMAIN_PLUGIN  3
#define KVSTORE_DOMAIN_SETTING 4

struct rstr *kv_url_opt_get_rstr(const char *url, int domain, const char *key);

int kv_url_opt_get_int(const char *url, int domain, const char *key, int def);

int64_t kv_url_opt_get_int64(const char *url, int domain,
                             const char *key, int64_t def);

#define KVSTORE_SET_STRING 1
#define KVSTORE_SET_INT    2
#define KVSTORE_SET_VOID   3
#define KVSTORE_SET_INT64  4

#define KVSTORE_UNIMPORTANT 0x100

void kv_url_opt_set(const char *url, int domain, const char *key,
		    int type, ...);


void kvstore_deferred_flush(void);
