/*
 *  Browser interface
 *  Copyright (C) 2008 Andreas Öman
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
 */

#ifndef BROWSER_PROBE_H
#define BROWSER_PROBE_H

void browser_probe_init(void);

void browser_probe_autoview_enqueue(browser_node_t *bn);

void browser_probe_from_list(struct browser_node_list *l);

void browser_probe_deinit(void);

#endif /* BROWSER_PROBE_H */
