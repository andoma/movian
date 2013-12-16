/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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


#ifndef KEYRING_H__
#define KEYRING_H__

void keyring_init(void);


#define KEYRING_USER_REJECTED -1
#define KEYRING_OK             0
#define KEYRING_NOT_FOUND      1

int keyring_lookup(const char *id, char **username, char **password,
		   char **domain, int *remember_me, const char *source,
		   const char *reason, int flags);

#define KEYRING_QUERY_USER       0x1
#define KEYRING_SHOW_REMEMBER_ME 0x2
#define KEYRING_REMEMBER_ME_SET  0x4
#define KEYRING_ONE_SHOT         0x8

#endif /* KEYRING__H_ */
