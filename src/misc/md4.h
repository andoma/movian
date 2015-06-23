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

#if ENABLE_COMMONCRYPTO

#include <CommonCrypto/CommonDigest.h>

#define md4_decl(ctx) CC_MD4_CTX ctx

#define md4_init(ctx) CC_MD4_Init(&ctx)

#define md4_update(ctx, data, len) CC_MD4_Update(&ctx, data, len)

#define md4_final(ctx, output) CC_MD4_Final(output, &ctx)

#elif ENABLE_POLARSSL

#include "polarssl/md4.h"

#define md4_decl(ctx) md4_context *ctx = alloca(sizeof(md4_context));

#define md4_init(ctx) md4_starts(ctx);

#define md4_final(ctx, output) md4_finish(ctx, output);

#elif ENABLE_OPENSSL

#include <openssl/md4.h>

#define md4_decl(ctx) MD4_CTX ctx

#define md4_init(ctx) MD4_Init(&ctx)

#define md4_update(ctx, data, len) MD4_Update(&ctx, data, len)

#define md4_final(ctx, output) MD4_Final(output, &ctx)

#else
#error No md4 crypto
#endif
