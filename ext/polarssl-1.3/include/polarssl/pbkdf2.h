/**
 * \file pbkdf2.h
 *
 * \brief Password-Based Key Derivation Function 2 (from PKCS#5)
 *
 * \deprecated Use pkcs5.h instead.
 *
 * \author Mathias Olsson <mathias@kompetensum.com>
 *
 *  Copyright (C) 2006-2012, ARM Limited, All Rights Reserved
 *
 *  This file is part of mbed TLS (https://tls.mbed.org)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#ifndef POLARSSL_PBKDF2_H
#define POLARSSL_PBKDF2_H

#include "md.h"

#include <stddef.h>

#if defined(_MSC_VER) && !defined(EFIX64) && !defined(EFI32)
#include <basetsd.h>
typedef UINT32 uint32_t;
#else
#include <inttypes.h>
#endif

#define POLARSSL_ERR_PBKDF2_BAD_INPUT_DATA                 -0x007C  /**< Bad input parameters to function. */

#ifdef __cplusplus
extern "C" {
#endif

#if ! defined(POLARSSL_DEPRECATED_REMOVED)
#if defined(POLARSSL_DEPRECATED_WARNING)
#define DEPRECATED    __attribute__((deprecated))
#else
#define DEPRECATED
#endif
/**
 * \brief          PKCS#5 PBKDF2 using HMAC
 *
 * \deprecated     Use pkcs5_pbkdf2_hmac() instead
 *
 * \param ctx      Generic HMAC context
 * \param password Password to use when generating key
 * \param plen     Length of password
 * \param salt     Salt to use when generating key
 * \param slen     Length of salt
 * \param iteration_count       Iteration count
 * \param key_length            Length of generated key
 * \param output   Generated key. Must be at least as big as key_length
 *
 * \returns        0 on success, or a POLARSSL_ERR_xxx code if verification fails.
 */
int pbkdf2_hmac( md_context_t *ctx, const unsigned char *password,
                 size_t plen, const unsigned char *salt, size_t slen,
                 unsigned int iteration_count,
                 uint32_t key_length, unsigned char *output ) DEPRECATED;

/**
 * \brief          Checkup routine
 *
 * \deprecated     Use pkcs5_self_test() instead
 *
 * \return         0 if successful, or 1 if the test failed
 */
int pbkdf2_self_test( int verbose ) DEPRECATED;
#undef DEPRECATED
#endif /* POLARSSL_DEPRECATED_REMOVED */

#ifdef __cplusplus
}
#endif

#endif /* pbkdf2.h */
