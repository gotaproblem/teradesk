/*
 * Teradesk. Copyright (c)       1993 - 2002  W. Klaren,
 *                               2002 - 2003  H. Robbers,
 *                               2003 - 2013  Dj. Vukovic,
 *           Bespoke Desktop     2026         S. (gotaproblem)
 *
 * This file is part of Teradesk.
 *
 * Teradesk is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Teradesk is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Teradesk; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02111-1307 USA
 */

#ifndef __PSMENU_H__
#define __PSMENU_H__

/*
 * Bespoke Desktop "PiSTorm" menu: a menu title added to the menu bar at
 * runtime, whose dropdown lists the PiSTorm GEM applications found in
 * the directory named by the 'psdr' configuration key (e.g. the HOSTFS
 * share holding MP3GEM.PRG, VIDGEM.PRG, PSMON.APP...).
 */

void ps_menu_init(void);				/* build and install the menu (after config load) */
bool ps_menu_select(_WORD title, _WORD item, _WORD kstate);	/* TRUE = event handled */

#endif
