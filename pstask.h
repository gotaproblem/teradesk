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
 * Bespoke Desktop PiSTorm monitor window: ST/TT memory usage and, under
 * FreeMiNT, a scrollable list of running tasks (from u:\proc, as the
 * standalone PSMON did). Opened by clicking the PiSTorm badge on the
 * taskbar; refreshed from the taskbar's 500 ms tick.
 */

void mn_open(void);						/* open (or top) the monitor window */
void mn_tick(void);						/* periodic refresh while open */
void mn_close(void);					/* close the window */

#endif
