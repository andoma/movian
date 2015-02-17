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
#include "ppapi/c/pp_var.h"

const char *pepper_errmsg(int err);

struct net_addr;
struct rstr;

int pepper_NetAddress_to_net_addr(struct net_addr *dst, int src);
int pepper_Resolver_to_net_addr(struct net_addr *dst, int src);

struct rstr *nacl_var_dict_get_str(struct PP_Var dict, const char *key);

int64_t nacl_var_dict_get_int64(struct PP_Var dict, const char *key,
                                int64_t def);

void nacl_fsinfo(uint64_t *size, uint64_t *avail, const char *fs);

void nacl_dict_set_str(struct PP_Var var_dict, const char *key,
                       const char *value);

void nacl_dict_set_int(struct PP_Var var_dict, const char *key, int i);

void nacl_dict_set_int64(struct PP_Var var_dict, const char *key, int64_t i);
