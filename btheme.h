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

#ifndef __BTHEME_H__
#define __BTHEME_H__

/*
 * The Bespoke Desktop theme layer.
 *
 * One place that owns every colour, bevel and metric the custom UI -
 * the taskbar, its cells and pager, the tooltips, the PiSTorm system
 * menu, the JIT panel and its gauges - draws with, so switching the
 * active theme re-skins the whole desktop at once.
 *
 * Themes carry true RGB. On a colour screen (>= 16 colours) a theme's
 * RGB is loaded into a reserved block of VDI colour registers (indices
 * BT_PAL_BASE..) with vs_color, and the panels draw with those indices;
 * on mono, or for the default GEM Grey theme, the standard VDI-16
 * indices are used directly. The returned BTHEME therefore always holds
 * ready-to-use colour indices, whatever the depth.
 */

typedef struct
{
	_WORD face;			/* control / cell face                       */
	_WORD text;			/* normal text                               */
	_WORD light;		/* bevel highlight (top/left when raised)    */
	_WORD dark;			/* bevel shadow                              */
	_WORD sel_bg;		/* selected fill (active pager, chosen seg)  */
	_WORD sel_fg;		/* selected text                             */
	_WORD alert_bg;		/* alert (throttle) fill                     */
	_WORD alert_fg;		/* alert text                                */
	_WORD panel;		/* bar / chrome background                   */
	_WORD title_bg;		/* panel title bar                           */
	_WORD title_fg;		/* panel title text                          */
	_WORD paper;		/* window + tooltip interior                 */
	_WORD hpad;			/* text inset, px                            */
	_WORD vpad;			/* cell top/bottom inset, px                 */
	_WORD gap;			/* gap between cells, px                     */
	_WORD flat;			/* 0 = 3D bevels, 1 = flat (no bevel lines)  */
} BTHEME;

/* bevel kinds for bt_bevel() */

#define BT_SUNK		0	/* dark top/left, light bottom/right (cells)   */
#define BT_RAISED	1	/* light top/left, dark bottom/right (buttons) */

/* First of the reserved VDI colour registers a theme's RGB is loaded
 * into (12 consecutive). High enough to avoid the desktop's own low
 * indices; the wallpaper is direct-RGB at truecolour, so untouched. */

#define BT_PAL_BASE	240

void          bt_use(_WORD id);			/* select the active theme (clamped) */
_WORD         bt_current(void);			/* active theme index */
_WORD         bt_count(void);			/* number of themes */
_WORD         bt_themed(void);			/* 0 = GEM Grey / native look */
_WORD         bt_labelchar(_WORD c);	/* icon-label ib_char for the theme */
const char   *bt_name(_WORD id);		/* theme name for the picker */
const BTHEME *bt(void);					/* the active theme */

void bt_fill(GRECT *r, _WORD colour);	/* solid fill (clipping-safe) */
void bt_bevel(GRECT *r, _WORD kind);	/* 1px 3D edge; nothing if flat */

#endif
