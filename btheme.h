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

	/* APJ-OS Fluent roles. Every preset carries them; the classic
	 * presets derive them from their bevel colours so they draw exactly
	 * as before. These are the roles the 12 above could not express and
	 * that a flat, bordered, hover-aware look needs. */

	_WORD border;		/* 1px outline of a flat control             */
	_WORD hover;		/* control face under the pointer            */
	_WORD pressed;		/* control face while the button is down     */
	_WORD focus;		/* keyboard-focus ring                       */
	_WORD disabled;		/* text of an unavailable control            */
	_WORD elevation;	/* shadow / drop tint under raised surfaces  */
	_WORD accent;		/* brand colour: default button, links,      */
						/*   toggles - distinct from sel_bg          */

	_WORD hpad;			/* text inset, px                            */
	_WORD vpad;			/* cell top/bottom inset, px                 */
	_WORD gap;			/* gap between cells, px                     */
	_WORD flat;			/* 0 = 3D bevels, 1 = flat (no bevel lines)  */
	_WORD radius;		/* corner rounding, px (0 = square)          */
} BTHEME;

/* bevel kinds for bt_bevel() */

#define BT_SUNK		0	/* dark top/left, light bottom/right (cells)   */
#define BT_RAISED	1	/* light top/left, dark bottom/right (buttons) */

/* First of the reserved VDI colour registers a theme's RGB is loaded
 * into (BT_PAL_N consecutive). High enough to avoid the desktop's own
 * low indices, low enough that the block still fits a 256-entry
 * palette; the wallpaper is direct-RGB at truecolour, so untouched. */

#define BT_PAL_N	19
#define BT_PAL_BASE	(256 - BT_PAL_N)	/* 237..255 */
#define BT_PEN_DESK	(BT_PAL_BASE - 1)	/* 236: a Fluent preset's desktop colour */

void          bt_use(_WORD id);			/* select the active theme (clamped) */
_WORD         bt_current(void);			/* active theme index */
_WORD         bt_count(void);			/* number of themes */
_WORD         bt_themed(void);			/* 0 = GEM Grey / native look */
_WORD         bt_labelchar(_WORD c);	/* icon-label ib_char for the theme */
const char   *bt_name(_WORD id);		/* theme name for the picker */
const BTHEME *bt(void);					/* the active theme */

void bt_fill(GRECT *r, _WORD colour);	/* solid fill (clipping-safe) */
void bt_bevel(GRECT *r, _WORD kind);	/* 1px 3D edge; nothing if flat */
void bt_border(GRECT *r, _WORD colour);	/* 1px outline, radius-aware    */

/* RGB of a role in the ACTIVE preset, 0xRRGGBB. What a renderer that
 * takes true colour (XaAES render_apj) asks for, rather than the VDI
 * index the BTHEME fields hold. role is a BT_R_* value. */

long bt_rgb(_WORD role);

/* Ask XaAES to draw this client with the APJ-OS renderer (bespoke
 * appl_control opcode 110). Call once, right after appl_init() and
 * before any window exists. bt_apj_render() then says whether it took:
 * 0 on a stock or older kernel, where the desktop themes the classic
 * way through opcodes 108/109. */

void  bt_attach_apj_render(void);
_WORD bt_apj_render(void);

/* Draw text antialiased through XaAES (opcode 114) when we are on the
 * APJ renderer with a Fluent-class theme. (x,y) = cell top-left, the
 * current VDI text colour and font apply, the current xd clip applies.
 * Returns 0 if the caller must draw it with v_gtext instead. */

_WORD bt_text(_WORD x, _WORD y, const char *s);
_WORD bt_fluent(void);				/* APJ-OS: Fluent-class preset under render_apj */
_WORD bt_deskpen(void);				/* APJ-OS: BT_PEN_DESK under a Fluent preset, else -1 */

enum
{
	BT_R_FACE, BT_R_TEXT, BT_R_LIGHT, BT_R_DARK, BT_R_SELBG, BT_R_SELFG,
	BT_R_ALBG, BT_R_ALFG, BT_R_PANEL, BT_R_TITBG, BT_R_TITFG, BT_R_PAPER,
	BT_R_BORDER, BT_R_HOVER, BT_R_PRESSED, BT_R_FOCUS, BT_R_DISABLED,
	BT_R_ELEVATION, BT_R_ACCENT,
	BT_R_N
};

#endif
