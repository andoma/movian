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

#define STPP_VERSION 1

// These things are sent over the wire so no changes here please

#define STPP_CMD_HELLO       0
#define STPP_CMD_SUBSCRIBE   1
#define STPP_CMD_UNSUBSCRIBE 2
#define STPP_CMD_SET         3
#define STPP_CMD_NOTIFY      4
#define STPP_CMD_EVENT       5
#define STPP_CMD_REQ_MOVE    6
#define STPP_CMD_WANT_MORE_CHILDS 7


// Notify types (First byte in STPP_CMD_NOTIFY message)
// These things are sent over the wire so no changes (just additions) here

#define STPP_SET_VOID           0
#define STPP_SET_INT            1
#define STPP_SET_FLOAT          2
#define STPP_SET_STRING         3 // First byte is strtype
#define STPP_SET_URI            4
#define STPP_SET_DIR            5
#define STPP_ADD_CHILDS         6
#define STPP_ADD_CHILDS_BEFORE  7
#define STPP_DEL_CHILD          8
#define STPP_MOVE_CHILD         9
#define STPP_SELECT_CHILD       10
#define STPP_ADD_CHILD_SELECTED 11
#define STPP_VALUE_PROP         12
#define STPP_TOGGLE_INT         13
#define STPP_HAVE_MORE_CHILDS_YES 14
#define STPP_HAVE_MORE_CHILDS_NO  15
