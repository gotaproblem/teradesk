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

#include <library.h>
#include <xdialog.h>

#include "resource.h"					/* menu constants desk.h needs */
#include "desk.h"						/* vdi_handle */
#include "btheme.h"

/* colour roles, in the order they are loaded into the reserved VDI
 * registers (BT_PAL_BASE + role). The first R_N are the classic set
 * every preset spells out; the BT_R_* values past R_N are the APJ-OS
 * Fluent roles, which a preset may spell out (ext[]) or leave to be
 * derived from its bevel colours. */

enum
{
	R_FACE, R_TEXT, R_LIGHT, R_DARK, R_SELBG, R_SELFG,
	R_ALBG, R_ALFG, R_PANEL, R_TITBG, R_TITFG, R_PAPER, R_N
};

#define R_EXT_N		(BT_R_N - R_N)		/* the Fluent roles */

typedef struct
{
	char name[16];
	_WORD pal;						/* 1 = load rgb[] (colour screen) */
	unsigned char rgb[R_N][3];		/* 0-255 per component */
	_WORD hpad, vpad, gap, flat;
	/* --- APJ-OS. (-Wextra insists every preset initialises these) --- */
	_WORD radius;					/* corner rounding, px */
	_WORD has_ext;					/* 1 = ext[] is filled in */
	unsigned char ext[R_EXT_N][3];	/* border hover pressed focus disabled elevation accent */
	/* APJ-OS: the desktop colour a Fluent-class preset brings with it -
	 * the flat stand-in for its wallpaper (skins/wall/<FILE>.PNG in
	 * apj-os-tools is the same picture as a gradient). Loaded into
	 * BT_PEN_DESK; the classic presets leave the saved pattern/colour. */
	unsigned char desk[3];
	/* APJ-OS: the wallpaper that goes with it, BT_WALL_DIR<wall>.PNG,
	 * applied when the desk has no wallpaper or one of ours; "" = none */
	char wall[6];
} PRESET;

/* where the APJ-OS desktop backgrounds live (skins/mkwall.py output) */
#define BT_WALL_DIR		"S:\\APJ-OS\\BG\\"


/* If the reserved register block and the role count ever disagree,
 * bt_resolve() would write past the block - make that a compile error. */

typedef char bt_pal_block_matches_roles[(BT_PAL_N == BT_R_N) ? 1 : -1];
typedef char bt_classic_roles_match_header[((int) R_N == (int) BT_R_BORDER) ? 1 : -1];

/*
 * The presets. GEM Grey keeps pal = 0 and uses the standard VDI-16
 * indices directly (bulletproof, the classic look). The rest carry true
 * RGB (common modern UI palettes) loaded into reserved colour registers
 * on a colour screen; on mono they fall back to the GEM Grey indices.
 */

static const PRESET presets[] =
{
	/* 0: GEM Grey - standard indices, no palette load */
	{ "GEM Grey", 0,
	  {{0,0,0}}, 6, 3, 8, 0 ,  0, 0, {{0,0,0}}, {0,0,0}, "" },

	/* 1: Slate - dark grey, white text, cyan selection (the approved
	 *    mockup: face/paper 555555, light C0C0C0, dark black, sel cyan
	 *    on black, title black bg / cyan text) */
	{ "Slate", 1,
	  { {0x55,0x55,0x55}, {0xFF,0xFF,0xFF}, {0xC0,0xC0,0xC0}, {0x00,0x00,0x00},
	    {0x00,0xFF,0xFF}, {0x00,0x00,0x00}, {0xDC,0x26,0x26}, {0xFF,0xFF,0xFF},
	    {0x55,0x55,0x55}, {0x00,0x00,0x00}, {0x00,0xFF,0xFF}, {0x55,0x55,0x55} },
	  6, 3, 8, 0 ,  0, 0, {{0,0,0}}, {0,0,0}, "" },

	/* 2: Midnight - near-black, purple accent */
	{ "Midnight", 1,
	  { {0x27,0x27,0x2A}, {0xFA,0xFA,0xFA}, {0x3F,0x3F,0x46}, {0x09,0x09,0x0B},
	    {0xA8,0x55,0xF7}, {0xFF,0xFF,0xFF}, {0xDC,0x26,0x26}, {0xFF,0xFF,0xFF},
	    {0x18,0x18,0x1B}, {0x09,0x09,0x0B}, {0xFA,0xFA,0xFA}, {0x18,0x18,0x1B} },
	  6, 3, 8, 0 ,  0, 0, {{0,0,0}}, {0,0,0}, "" },

	/* 3: Navy Gold - high-trust navy with gold selection */
	{ "Navy Gold", 1,
	  { {0x12,0x36,0x5A}, {0xF8,0xFA,0xFC}, {0x1E,0x4E,0x80}, {0x07,0x1A,0x2E},
	    {0xC7,0xA8,0x4B}, {0x0A,0x25,0x40}, {0xB9,0x1C,0x1C}, {0xFF,0xFF,0xFF},
	    {0x0A,0x25,0x40}, {0x0A,0x25,0x40}, {0xC7,0xA8,0x4B}, {0x0A,0x25,0x40} },
	  6, 3, 8, 0 ,  0, 0, {{0,0,0}}, {0,0,0}, "" },

	/* 4: Paper - warm light, editorial */
	{ "Paper", 1,
	  { {0xFA,0xFA,0xF8}, {0x1C,0x19,0x17}, {0xFF,0xFF,0xFF}, {0xD6,0xD3,0xD1},
	    {0x03,0x69,0xA1}, {0xFF,0xFF,0xFF}, {0xC2,0x41,0x0C}, {0xFF,0xFF,0xFF},
	    {0xF5,0xF0,0xEB}, {0xE7,0xE0,0xD8}, {0x1C,0x19,0x17}, {0xFF,0xFF,0xFF} },
	  6, 3, 8, 0 ,  0, 0, {{0,0,0}}, {0,0,0}, "" },

	/* 5: SaaS Light - clean blue-on-white */
	{ "SaaS Light", 1,
	  { {0xFF,0xFF,0xFF}, {0x0F,0x17,0x2A}, {0xFF,0xFF,0xFF}, {0xCB,0xD5,0xE1},
	    {0x25,0x63,0xEB}, {0xFF,0xFF,0xFF}, {0xDC,0x26,0x26}, {0xFF,0xFF,0xFF},
	    {0xF1,0xF5,0xF9}, {0xE2,0xE8,0xF0}, {0x0F,0x17,0x2A}, {0xFF,0xFF,0xFF} },
	  6, 3, 8, 0 ,  0, 0, {{0,0,0}}, {0,0,0}, "" },

	/* 6: Atari Blue - the mockup classic: grey face, BLUE titles and
	 *    selection, light interiors */
	{ "Atari Blue", 1,
	  { {0xC0,0xC0,0xC0}, {0x00,0x00,0x00}, {0xFF,0xFF,0xFF}, {0x55,0x55,0x55},
	    {0x00,0x00,0x7F}, {0xFF,0xFF,0xFF}, {0xDC,0x26,0x26}, {0xFF,0xFF,0xFF},
	    {0xC0,0xC0,0xC0}, {0x00,0x00,0xFF}, {0xFF,0xFF,0xFF}, {0xFF,0xFF,0xFF} },
	  6, 3, 8, 0 ,  0, 0, {{0,0,0}}, {0,0,0}, "" },

	/* 7: Terminal - the mockup green-on-black, flat (no bevels) */
	{ "Terminal", 1,
	  { {0x00,0x00,0x00}, {0x00,0xFF,0x00}, {0x00,0xFF,0x00}, {0x00,0x7F,0x00},
	    {0x00,0xFF,0x00}, {0x00,0x00,0x00}, {0xDC,0x26,0x26}, {0xFF,0xFF,0xFF},
	    {0x00,0x00,0x00}, {0x00,0x00,0x00}, {0x00,0xFF,0x00}, {0x00,0x00,0x00} },
	  6, 3, 8, 1 ,  0, 0, {{0,0,0}}, {0,0,0}, "" },

	/* 8-11: the APJ-OS Fluent family. Role for role the palettes of the
	 *    APJSKIN sheets in apj-os-tools/skins/tokens/<skin>.json (FLTL, FLTD,
	 *    GRPH, FUJI), so a skinned app's text drawn through XaAES's pens
	 *    lands on the same colours its sheet was baked with, and
	 *    apjskin picks the sheet whose palette is nearest this table.
	 *    Flat, 1px borders in place of bevels, 4px corners. */

	/* 8: Fluent Light - Windows 11 light: Mica-grey panel, white
	 *    surfaces, 0F6CBD accent */
	{ "Fluent Light", 1,
	  { {0xFF,0xFF,0xFF}, {0x15,0x1B,0x22}, {0xFF,0xFF,0xFF}, {0xC3,0xCA,0xD3},
	    {0xCF,0xE4,0xF7}, {0x0A,0x2A,0x45}, {0xF7,0xD9,0xD6}, {0x6D,0x21,0x1B},
	    {0xF2,0xF4,0xF7}, {0xE8,0xEC,0xF1}, {0x19,0x21,0x2A}, {0xFF,0xFF,0xFF} },
	  8, 4, 6, 1,
	  4, 1,
	  { {0xD7,0xDD,0xE5},	/* border    */
	    {0xEA,0xEF,0xF5},	/* hover     */
	    {0xDB,0xE4,0xEE},	/* pressed   */
	    {0x0F,0x6C,0xBD},	/* focus     */
	    {0x9A,0xA5,0xB1},	/* disabled  */
	    {0xB6,0xBF,0xCA},	/* elevation */
	    {0x0F,0x6C,0xBD} },	/* accent    */
	  {0xBF,0xD0,0xE5},	/* desk: pale steel blue */
	  "FLTL" },

	/* 9: Fluent Dark - Windows 11 dark: charcoal-blue, 4CC2FF accent */
	{ "Fluent Dark", 1,
	  { {0x2B,0x32,0x3A}, {0xF0,0xF4,0xF8}, {0x3F,0x49,0x54}, {0x12,0x16,0x1B},
	    {0x0F,0x3B,0x57}, {0xEA,0xF6,0xFF}, {0x7A,0x2F,0x2A}, {0xFF,0xEC,0xE9},
	    {0x1F,0x24,0x2B}, {0x16,0x1B,0x21}, {0xE8,0xEE,0xF5}, {0x16,0x1B,0x21} },
	  8, 4, 6, 1,
	  4, 1,
	  { {0x39,0x42,0x4D},	/* border    */
	    {0x33,0x3B,0x45},	/* hover     */
	    {0x10,0x20,0x2B},	/* pressed   */
	    {0x4C,0xC2,0xFF},	/* focus     */
	    {0x6C,0x7A,0x89},	/* disabled  */
	    {0x0A,0x0D,0x11},	/* elevation */
	    {0x4C,0xC2,0xFF} },	/* accent    */
	  {0x0D,0x1E,0x2E},	/* desk: deep navy */
	  "FLTD" },

	/* 10: Graphite - neutral dark greys, silver accent */
	{ "Graphite", 1,
	  { {0x2D,0x32,0x36}, {0xE9,0xEB,0xED}, {0x43,0x4A,0x50}, {0x13,0x16,0x19},
	    {0x34,0x3B,0x42}, {0xFF,0xFF,0xFF}, {0x6B,0x3B,0x38}, {0xFF,0xEC,0xEB},
	    {0x20,0x23,0x26}, {0x17,0x19,0x1C}, {0xE3,0xE7,0xEA}, {0x17,0x19,0x1C} },
	  8, 4, 6, 1,
	  4, 1,
	  { {0x3B,0x41,0x47},	/* border    */
	    {0x36,0x3C,0x42},	/* hover     */
	    {0x19,0x1C,0x1F},	/* pressed   */
	    {0xCF,0xD8,0xE0},	/* focus     */
	    {0x75,0x7D,0x84},	/* disabled  */
	    {0x0B,0x0D,0x0E},	/* elevation */
	    {0xCF,0xD8,0xE0} },	/* accent    */
	  {0x19,0x1D,0x21},	/* desk: graphite */
	  "GRPH" },

	/* 11: Fuji - warm near-black, Atari-red accent */
	{ "Fuji", 1,
	  { {0x30,0x26,0x23}, {0xF5,0xEC,0xE9}, {0x47,0x39,0x35}, {0x14,0x0F,0x0E},
	    {0x4D,0x1D,0x17}, {0xFF,0xE9,0xE4}, {0x7D,0x2A,0x22}, {0xFF,0xE9,0xE4},
	    {0x22,0x1B,0x19}, {0x18,0x12,0x11}, {0xF2,0xE6,0xE2}, {0x18,0x12,0x11} },
	  8, 4, 6, 1,
	  4, 1,
	  { {0x45,0x38,0x35},	/* border    */
	    {0x3A,0x2E,0x2B},	/* hover     */
	    {0x1C,0x14,0x13},	/* pressed   */
	    {0xC8,0x38,0x2F},	/* focus     */
	    {0x7C,0x6A,0x66},	/* disabled  */
	    {0x0C,0x08,0x07},	/* elevation */
	    {0xC8,0x38,0x2F} },	/* accent    */
	  {0x2B,0x15,0x10},	/* desk: dark ember */
	  "FUJI" },
};

#define NPRESETS	((_WORD) (sizeof(presets) / sizeof(presets[0])))

/* The GEM Grey / mono fallback: standard VDI-16 indices per role. */

static const _WORD fb[BT_R_N] =
{
	G_WHITE,	/* face     */
	G_BLACK,	/* text     */
	G_WHITE,	/* light    */
	G_LBLACK,	/* dark     */
	G_LBLACK,	/* sel_bg   */
	G_WHITE,	/* sel_fg   */
	G_RED,		/* alert_bg */
	G_WHITE,	/* alert_fg */
	G_LWHITE,	/* panel    */
	G_LBLACK,	/* title_bg */
	G_WHITE,	/* title_fg */
	G_WHITE,	/* paper    */
	G_LBLACK,	/* border    */
	G_WHITE,	/* hover     */
	G_LBLACK,	/* pressed   */
	G_LBLACK,	/* focus     */
	G_LBLACK,	/* disabled  */
	G_LBLACK,	/* elevation */
	G_LBLACK	/* accent    */
};

/* Which classic role a preset's Fluent roles fall back to when it does
 * not spell them out: border/pressed/disabled/elevation take the bevel
 * shadow, hover the bevel highlight, focus/accent the selection colour.
 * The classic presets therefore draw exactly as they did. */

static const _WORD derive[R_EXT_N] =
{
	R_DARK,		/* border    */
	R_LIGHT,	/* hover     */
	R_DARK,		/* pressed   */
	R_SELBG,	/* focus     */
	R_DARK,		/* disabled  */
	R_DARK,		/* elevation */
	R_SELBG		/* accent    */
};

/* Nominal RGB of the standard pens the fallback table names, for
 * bt_rgb() on GEM Grey / mono. Only the five pens fb[] uses. */

static long pen_rgb(_WORD pen)
{
	switch (pen)
	{
		case G_BLACK:	return 0x000000L;
		case G_RED:		return 0xFF0000L;
		case G_LWHITE:	return 0xBFBFBFL;
		case G_LBLACK:	return 0x7F7F7FL;
		default:		return 0xFFFFFFL;	/* G_WHITE */
	}
}

static unsigned char cur_rgb[BT_R_N][3];	/* the active preset, all roles */

static _WORD cur = 0;
static _WORD apj_render = 0;			/* 1 = XaAES draws us with render_apj */
static BTHEME active;					/* resolved colour indices + metrics */

/*
 * Theming GEM: FIELD LESSON (2026-08-14 screenshot). Colour icons are
 * palette-expanded per draw from the standard pens, so remapping pens
 * 0/1/8/9 on OUR workstation re-tinted every desk and directory icon -
 * unacceptable. TeraDesk's own pens therefore stay NATIVE, always:
 * icons and dialogs keep their classic colours. What we theme instead:
 *  - our custom UI: the reserved theme pens (BT_PAL_BASE+) as before;
 *  - directory-window interiors + icon label colour: directly, see
 *    wd_set_obj0/set_obji in window.c (bt_themed() gates them);
 *  - the WINDOW CHROME (titles, borders, sliders, menu bar): XaAES
 *    draws it through its OWN workstation - bespoke opcodes 108/109
 *    push a pen remap kernel-side. Chrome pens: 0 -> theme text
 *    (the active title's text and gadget highlights draw in pen 0 -
 *    mapping it to the background made them unreadable), 1 -> text,
 *    8 -> face, 9 -> dark. Old kernels return 0: chrome stays native.
 */

static const _WORD gempen[4] = { G_WHITE, G_BLACK, G_LWHITE, G_LBLACK };

/*
 * The APJ-OS path. When XaAES is drawing us with render_apj (opcode
 * 110 took), a Fluent-class preset hands it every role as true RGB
 * through opcode 111 - the renderer loads them into its own colour
 * registers and draws flat controls with them; 113 then tells it to
 * reskin our window chrome and redraw. A classic preset sends 112
 * instead, so render_apj falls back to the stock look and the pen
 * remap above does the theming as before. Role order = BT_R_*.
 */

void dsk_workarea(void);				/* icon.c (icon.h needs half the desktop's headers) */
void dsk_themechanged(void);			/* icon.c: label boxes and icon grid follow the theme */
void dsk_wall_bind(const char *path, _WORD mode);	/* icon.c: current desk's wallpaper, no redraw */
bool x_exist(const char *file, _WORD flags);		/* xfilesys.h, whose presets[] clashes with ours */
#define BT_EX_FILE	1						/* = EX_FILE */

/*
 * APJ-OS: a Fluent preset brings its wallpaper. The current desk takes
 * BT_WALL_DIR<wall>.PNG when it has no wallpaper, or one of ours from an
 * earlier theme; a picture the user chose is never touched, and a
 * missing file changes nothing (the preset's flat desk colour shows
 * instead). The caller redraws - dsk_themechanged() reloads it.
 */
static void bt_wall(const PRESET *p)
{
	char path[sizeof(BT_WALL_DIR) + 12];
	_WORD n = (_WORD) (sizeof(BT_WALL_DIR) - 1);

	if (!p->wall[0])
		return;
	if (options.wallp[0] && strnicmp(options.wallp, BT_WALL_DIR, n) != 0)
		return;

	sprintf(path, "%s%s.PNG", BT_WALL_DIR, p->wall);
	if (stricmp(path, options.wallp) == 0)
		return;
	if (!x_exist(path, BT_EX_FILE))
		return;

	/* 0 = stretch. The desk area is the screen less the menu bar and the
	 * taskbar, so it is not 16:9; "fit" keeps the picture's ratio and
	 * letterboxes it with black bars down both sides. A gradient has no
	 * ratio to keep - stretch it over the whole area. */
	dsk_wall_bind(path, 0);
}

static void bt_apjpush(const PRESET *p)
{
	_WORD i;

	if (!apj_render || xd_ncolours < 16)
		return;

	if (!(p->pal && p->has_ext))
	{
		appl_control(-1, 112, NULL);
		dsk_workarea();				/* menu bar back to stock height */
		dsk_themechanged();			/* classic label boxes and icon cell */
		return;
	}

	for (i = 0; i < BT_R_N; i++)
	{
		long kv = ((long) i << 24) |
		          ((long) cur_rgb[i][0] << 16) |
		          ((long) cur_rgb[i][1] << 8) |
		           (long) cur_rgb[i][2];

		appl_control(-1, 111, (void *) kv);
	}

	/* all roles in: reskin our open windows (chrome) and redraw */
	appl_control(-1, 113, NULL);
	dsk_workarea();					/* XaAES made the menu bar taller */
	bt_wall(p);						/* the preset's wallpaper, if we may */
	dsk_themechanged();				/* label boxes, icon cell, desk colour/wallpaper */
}

static void bt_gempens(const PRESET *p)
{
	_WORD i;

	if (xd_ncolours < 16)
		return;

	/* Fluent-class presets (has_ext) do NOT use the pen remap. It maps
	 * pen 0 (white) to the text colour, which on a light theme is near
	 * black - and colour icons are palette-expanded from the standard
	 * pens through the same workstation, so every white pixel in every
	 * icon went dark. Their chrome comes from XaAES render_apj instead;
	 * until that lands the chrome stays native GEM, which is correct. */

	if (p->pal && !p->has_ext)
	{
		/* chrome pen <- role: 0 text, 1 text, 8 face, 9 dark */

		static const _WORD role[4] = { R_TEXT, R_TEXT, R_FACE, R_DARK };

		for (i = 0; i < 4; i++)
		{
			long kv;

			kv = ((long) gempen[i] << 24) |
			     ((long) p->rgb[role[i]][0] << 16) |
			     ((long) p->rgb[role[i]][1] << 8) |
			      (long) p->rgb[role[i]][2];
			appl_control(-1, 108, (void *) kv);
		}
	} else
		appl_control(-1, 109, NULL);	/* chrome back to native, exactly */
}


/*
 * Resolve the active theme: on a colour screen, an RGB theme loads its
 * palette into the reserved registers and uses those indices; GEM Grey
 * and mono use the standard-16 fallback.
 */

static void bt_resolve(void)
{
	const PRESET *p = &presets[cur];
	_WORD idx[BT_R_N];
	_WORD i;

	/* the complete role table for this preset: classic roles as given,
	 * Fluent roles as given or derived */

	for (i = 0; i < R_N; i++)
	{
		cur_rgb[i][0] = p->rgb[i][0];
		cur_rgb[i][1] = p->rgb[i][1];
		cur_rgb[i][2] = p->rgb[i][2];
	}
	for (i = 0; i < R_EXT_N; i++)
	{
		const unsigned char *src = p->has_ext ? p->ext[i] : p->rgb[derive[i]];

		cur_rgb[R_N + i][0] = src[0];
		cur_rgb[R_N + i][1] = src[1];
		cur_rgb[R_N + i][2] = src[2];
	}

	if (p->pal && xd_ncolours >= 16)
	{
		for (i = 0; i < BT_R_N; i++)
		{
			_WORD rgb[3];

			/* VDI colour components are 0-1000, not 0-255 */
			rgb[0] = (_WORD) ((long) cur_rgb[i][0] * 1000L / 255L);
			rgb[1] = (_WORD) ((long) cur_rgb[i][1] * 1000L / 255L);
			rgb[2] = (_WORD) ((long) cur_rgb[i][2] * 1000L / 255L);

			vs_color(vdi_handle, BT_PAL_BASE + i, rgb);
			idx[i] = BT_PAL_BASE + i;
		}

		/* APJ-OS: the preset's desktop colour, one register below the
		 * role block (see bt_deskpen) */
		if (p->has_ext)
		{
			_WORD rgb[3];

			rgb[0] = (_WORD) ((long) p->desk[0] * 1000L / 255L);
			rgb[1] = (_WORD) ((long) p->desk[1] * 1000L / 255L);
			rgb[2] = (_WORD) ((long) p->desk[2] * 1000L / 255L);
			vs_color(vdi_handle, BT_PEN_DESK, rgb);
		}
	} else
	{
		for (i = 0; i < BT_R_N; i++)
			idx[i] = fb[i];
	}

	active.face     = idx[R_FACE];
	active.text     = idx[R_TEXT];
	active.light    = idx[R_LIGHT];
	active.dark     = idx[R_DARK];
	active.sel_bg   = idx[R_SELBG];
	active.sel_fg   = idx[R_SELFG];
	active.alert_bg = idx[R_ALBG];
	active.alert_fg = idx[R_ALFG];
	active.panel    = idx[R_PANEL];
	active.title_bg = idx[R_TITBG];
	active.title_fg = idx[R_TITFG];
	active.paper    = idx[R_PAPER];

	active.border    = idx[BT_R_BORDER];
	active.hover     = idx[BT_R_HOVER];
	active.pressed   = idx[BT_R_PRESSED];
	active.focus     = idx[BT_R_FOCUS];
	active.disabled  = idx[BT_R_DISABLED];
	active.elevation = idx[BT_R_ELEVATION];
	active.accent    = idx[BT_R_ACCENT];

	/* APJ-OS: Fluent-class presets scale their spacing with the screen -
	 * 8px of padding designed at 480 lines is a sliver at 1080. Classic
	 * presets keep their pixel metrics; they were tuned by eye as pixels. */
	{
		_WORD num = 2, den = 2;		/* x1 */

		if (p->has_ext)
		{
			if (xd_desk.g_h >= 1000)
				num = 4;			/* x2 at 1080 lines and up */
			else if (xd_desk.g_h >= 700)
				num = 3;			/* x1.5 at 720 */
		}
		active.hpad   = (p->hpad * num) / den;
		active.vpad   = (p->vpad * num) / den;
		active.gap    = (p->gap * num) / den;
		active.radius = (p->radius * num) / den;
	}
	active.flat   = p->flat;

	bt_gempens(p);
	bt_apjpush(p);
}


void bt_attach_apj_render(void)
{
	/* self-only opcode; returns 0 when the module is unavailable, and a
	 * stock XaAES answers 0 to any opcode it does not know - both mean
	 * "no", so there is nothing to tell apart */
	apj_render = (appl_control(-1, 110, NULL) != 0) ? 1 : 0;
}

_WORD bt_apj_render(void)
{
	return apj_render;
}

/* layout is the contract with XaAES render_apj.h - keep identical */

typedef struct
{
	_WORD x, y;
	_WORD pen;
	_WORD cw, ch;
	_WORD clip[4];
	const char *s;
} APJ_TEXTREQ;

/* APJ-OS: 1 while XaAES draws us and the active preset is Fluent-class */
_WORD bt_fluent(void)
{
	return (apj_render && presets[cur].has_ext && xd_ncolours >= 16) ? 1 : 0;
}

/* APJ-OS: the pen holding a Fluent-class preset's own desktop colour,
 * or -1 when the saved pattern/colour apply (classic preset, mono, or
 * a kernel without the renderer) */
_WORD bt_deskpen(void)
{
	return bt_fluent() ? BT_PEN_DESK : -1;
}


_WORD bt_text(_WORD x, _WORD y, const char *s)
{
	APJ_TEXTREQ rq;
	_WORD attr[10];
	GRECT clip;

#ifdef BT_NO_AATEXT
	(void) rq; (void) attr; (void) clip; (void) x; (void) y; (void) s;
	return 0;					/* bisect build: never hand text to XaAES */
#endif
	if (!apj_render || !presets[cur].has_ext || xd_ncolours < 16)
		return 0;

	vqt_attributes(vdi_handle, attr);	/* [1] colour, [8] cell w, [9] cell h */

	rq.x = x;
	rq.y = y;
	rq.pen = attr[1];
	rq.cw = attr[8];
	rq.ch = attr[9];
	rq.s = s;

	if (!xd_clip_get(&clip))
		clip = xd_desk;
	rq.clip[0] = clip.g_x;
	rq.clip[1] = clip.g_y;
	rq.clip[2] = clip.g_x + clip.g_w - 1;
	rq.clip[3] = clip.g_y + clip.g_h - 1;

	return (appl_control(-1, 114, &rq) != 0) ? 1 : 0;
}


void bt_use(_WORD id)
{
	void icn_theme_labels(void);		/* icon.c; avoids icon.h's include chain */

	if (id < 0)
		id = 0;
	if (id >= NPRESETS)
		id = NPRESETS - 1;

	cur = id;
	bt_resolve();

	/* desktop icons exist by now (options and icons load before the
	 * taskbar applies the saved theme) - restamp their label colours */
	icn_theme_labels();
}

_WORD bt_current(void)
{
	return cur;
}

_WORD bt_count(void)
{
	return NPRESETS;
}

const char *bt_name(_WORD id)
{
	if (id < 0 || id >= NPRESETS)
		return "";

	return presets[id].name;
}

const BTHEME *bt(void)
{
	return &active;
}

_WORD bt_themed(void)
{
	return presets[cur].pal;
}

/*
 * Icon labels. FIELD LESSON 2: icon objects - artwork AND label text -
 * are drawn by the AES (objc_draw), i.e. through XaAES's workstation
 * and its themed chrome pens: a native BLACK label fg came out WHITE
 * (chrome pen 1 = text) inside the label's opaque WHITE box (pen 0) -
 * invisible everywhere. So themed labels use colours that read
 * dark-on-light through EITHER workstation: fg = pen 9 (LBLACK: dark
 * grey native, theme-dark under the chrome remap), bg = pen 0 (WHITE:
 * white in both since the chrome push maps 0 to the light text
 * colour). GEM Grey: the icon's own colours, untouched.
 * ib_char layout: bits 15-12 fg, 11-8 bg, 7-0 character.
 */

_WORD bt_labelchar(_WORD c)
{
	if (presets[cur].pal)
		return (_WORD) ((c & 0x00FF) | 0x9000);

	return c;
}


void bt_fill(GRECT *r, _WORD colour)
{
	clr_object(r, colour, -1);
}


static void bt_line(_WORD x1, _WORD y1, _WORD x2, _WORD y2, _WORD colour)
{
	_WORD pxy[4];

	pxy[0] = x1;
	pxy[1] = y1;
	pxy[2] = x2;
	pxy[3] = y2;

	vsl_color(vdi_handle, colour);
	v_pline(vdi_handle, 2, pxy);
}


/*
 * Draw a one-pixel 3D edge around r. Flat themes draw nothing.
 */

void bt_bevel(GRECT *r, _WORD kind)
{
	const BTHEME *t = &active;
	_WORD x1 = r->g_x;
	_WORD y1 = r->g_y;
	_WORD x2 = r->g_x + r->g_w - 1;
	_WORD y2 = r->g_y + r->g_h - 1;
	_WORD tl, br;

	if (t->flat)
		return;

	if (kind == BT_RAISED)
	{
		tl = t->light;
		br = t->dark;
	} else
	{
		tl = t->dark;
		br = t->light;
	}

	bt_line(x1, y1, x2, y1, tl);			/* top    */
	bt_line(x1, y1, x1, y2, tl);			/* left   */
	bt_line(x1, y2, x2, y2, br);			/* bottom */
	bt_line(x2, y1 + 1, x2, y2, br);		/* right  */
}


/*
 * True RGB of a role in the active preset, 0xRRGGBB - for a consumer
 * that draws in colour rather than through our VDI registers (XaAES
 * render_apj takes RGB directly; the chrome remap of opcode 108 does
 * too). GEM Grey and mono report the nominal colour of the standard
 * pen the role falls back to.
 */

long bt_rgb(_WORD role)
{
	if (role < 0 || role >= BT_R_N)
		return 0L;

	if (presets[cur].pal && xd_ncolours >= 16)
		return ((long) cur_rgb[role][0] << 16) |
		       ((long) cur_rgb[role][1] << 8) |
		        (long) cur_rgb[role][2];

	return pen_rgb(fb[role]);
}


/*
 * A one-pixel outline in the given colour - the flat look's edge, where
 * bt_bevel() would draw a 3D one. With a corner radius the four corner
 * pixels are left out, which at 1px reads as rounding without needing
 * arcs; a real radius > 1 is the renderer's job (render_apj), not ours.
 */

void bt_border(GRECT *r, _WORD colour)
{
	_WORD x1 = r->g_x;
	_WORD y1 = r->g_y;
	_WORD x2 = r->g_x + r->g_w - 1;
	_WORD y2 = r->g_y + r->g_h - 1;
	_WORD c = (active.radius > 0) ? 1 : 0;

	if (r->g_w < 3 || r->g_h < 3)
		c = 0;

	bt_line(x1 + c, y1, x2 - c, y1, colour);		/* top    */
	bt_line(x1 + c, y2, x2 - c, y2, colour);		/* bottom */
	bt_line(x1, y1 + c, x1, y2 - c, colour);		/* left   */
	bt_line(x2, y1 + c, x2, y2 - c, colour);		/* right  */
}
