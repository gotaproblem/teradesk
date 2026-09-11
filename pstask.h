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

#ifndef __PSTASK_H__
#define __PSTASK_H__

/*
 * Bespoke Desktop taskbar v2 panels: the JIT panel (live engine figures
 * plus the ST/TT memory gauges, toggled by the taskbar's JIT button)
 * and the PiSTorm system-tasks menu (toggled by the PiSTorm button).
 * Both refresh from the taskbar's 500 ms tick.
 */

void mn_open(const GRECT *from);			/* open (or top) the JIT panel above "from" */
void mn_toggle(const GRECT *from);		/* JIT button click (the button's rect) */
void mn_tick(void);						/* periodic refresh while open */
void mn_close(void);					/* close the panel */

void sm_toggle(void);					/* PiSTorm button click */
void sm_close(void);					/* close the system menu */

#endif
