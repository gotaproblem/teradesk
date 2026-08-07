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

#ifndef __BKGIMG_H__
#define __BKGIMG_H__

/*
 * Bespoke Desktop wallpaper: a PNG or JPG desktop background, decoded,
 * scaled and pixel-format-converted host-side by the PiSTorm emulator
 * through the PSIMG NatFeat, then blitted by a userdef object drawing
 * the desktop root.
 */

void bk_init(void);						/* load the wallpaper (after config load) */
void bk_drop(void);						/* uninstall + free (before a desk switch) */
bool bk_install(void);					/* make the desktop root draw it; TRUE = done */

#endif
