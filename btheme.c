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
	/* --- APJ-OS: trailing so the classic presets need no edits --- */
	_WORD radius;					/* corner rounding, px */
	_WORD has_ext;					/* 1 = ext[] is filled in */
	unsigned char ext[R_EXT_N][3];	/* border hover pressed focus disabled elevation accent */
} PRESET;

/* If the reserved register block and the role count ever disagree,
 * bt_resolve() would write past the block - make that a compile error. */

typedef char bt_pal_block_matches_roles[(BT_PAL_N == BT_R_N) ? 1 : -1];
typedef char bt_classic_roles_match_header[(R_N == BT_R_BORDER) ? 1 : -1];

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
	  {{0,0,0}}, 6, 3, 8, 0 },

	/* 1: Slate - dark grey, white text, cyan selection (the approved
	 *    mockup: face/paper 555555, light C0C0C0, dark black, sel cyan
	 *    on black, title black bg / cyan text) */
	{ "Slate", 1,
	  { {0x55,0x55,0x55}, {0xFF,0xFF,0xFF}, {0xC0,0xC0,0xC0}, {0x00,0x00,0x00},
	    {0x00,0xFF,0xFF}, {0x00,0x00,0x00}, {0xDC,0x26,0x26}, {0xFF,0xFF,0xFF},
	    {0x55,0x55,0x55}, {0x00,0x00,0x00}, {0x00,0xFF,0xFF}, {0x55,0x55,0x55} },
	  6, 3, 8, 0 },

	/* 2: Midnight - near-black, purple accent */
	{ "Midnight", 1,
	  { {0x27,0x27,0x2A}, {0xFA,0xFA,0xFA}, {0x3F,0x3F,0x46}, {0x09,0x09,0x0B},
	    {0xA8,0x55,0xF7}, {0xFF,0xFF,0xFF}, {0xDC,0x26,0x26}, {0xFF,0xFF,0xFF},
	    {0x18,0x18,0x1B}, {0x09,0x09,0x0B}, {0xFA,0xFA,0xFA}, {0x18,0x18,0x1B} },
	  6, 3, 8, 0 },

	/* 3: Navy Gold - high-trust navy with gold selection */
	{ "Navy Gold", 1,
	  { {0x12,0x36,0x5A}, {0xF8,0xFA,0xFC}, {0x1E,0x4E,0x80}, {0x07,0x1A,0x2E},
	    {0xC7,0xA8,0x4B}, {0x0A,0x25,0x40}, {0xB9,0x1C,0x1C}, {0xFF,0xFF,0xFF},
	    {0x0A,0x25,0x40}, {0x0A,0x25,0x40}, {0xC7,0xA8,0x4B}, {0x0A,0x25,0x40} },
	  6, 3, 8, 0 },

	/* 4: Paper - warm light, editorial */
	{ "Paper", 1,
	  { {0xFA,0xFA,0xF8}, {0x1C,0x19,0x17}, {0xFF,0xFF,0xFF}, {0xD6,0xD3,0xD1},
	    {0x03,0x69,0xA1}, {0xFF,0xFF,0xFF}, {0xC2,0x41,0x0C}, {0xFF,0xFF,0xFF},
	    {0xF5,0xF0,0xEB}, {0xE7,0xE0,0xD8}, {0x1C,0x19,0x17}, {0xFF,0xFF,0xFF} },
	  6, 3, 8, 0 },

	/* 5: SaaS Light - clean blue-on-white */
	{ "SaaS Light", 1,
	  { {0xFF,0xFF,0xFF}, {0x0F,0x17,0x2A}, {0xFF,0xFF,0xFF}, {0xCB,0xD5,0xE1},
	    {0x25,0x63,0xEB}, {0xFF,0xFF,0xFF}, {0xDC,0x26,0x26}, {0xFF,0xFF,0xFF},
	    {0xF1,0xF5,0xF9}, {0xE2,0xE8,0xF0}, {0x0F,0x17,0x2A}, {0xFF,0xFF,0xFF} },
	  6, 3, 8, 0 },

	/* 6: Atari Blue - the mockup classic: grey face, BLUE titles and
	 *    selection, light interiors */
	{ "Atari Blue", 1,
	  { {0xC0,0xC0,0xC0}, {0x00,0x00,0x00}, {0xFF,0xFF,0xFF}, {0x55,0x55,0x55},
	    {0x00,0x00,0x7F}, {0xFF,0xFF,0xFF}, {0xDC,0x26,0x26}, {0xFF,0xFF,0xFF},
	    {0xC0,0xC0,0xC0}, {0x00,0x00,0xFF}, {0xFF,0xFF,0xFF}, {0xFF,0xFF,0xFF} },
	  6, 3, 8, 0 },

	/* 7: Terminal - the mockup green-on-black, flat (no bevels) */
	{ "Terminal", 1,
	  { {0x00,0x00,0x00}, {0x00,0xFF,0x00}, {0x00,0xFF,0x00}, {0x00,0x7F,0x00},
	    {0x00,0xFF,0x00}, {0x00,0x00,0x00}, {0xDC,0x26,0x26}, {0xFF,0xFF,0xFF},
	    {0x00,0x00,0x00}, {0x00,0x00,0x00}, {0x00,0xFF,0x00}, {0x00,0x00,0x00} },
	  6, 3, 8, 1 },

	/* 8: Fluent - APJ-OS direction C. Windows 11 light: Mica-grey
	 *    panel F3F3F3, near-white control faces, a 1px E5E5E5 border in
	 *    place of bevels, 0067C0 accent, 4px corners. The only preset
	 *    that spells out the Fluent roles instead of deriving them. */
	{ "Fluent", 1,
	  { {0xFB,0xFB,0xFB}, {0x1B,0x1B,0x1B}, {0xFF,0xFF,0xFF}, {0xE5,0xE5,0xE5},
	    {0x00,0x67,0xC0}, {0xFF,0xFF,0xFF}, {0xC4,0x2B,0x1C}, {0xFF,0xFF,0xFF},
	    {0xF3,0xF3,0xF3}, {0xF3,0xF3,0xF3}, {0x1B,0x1B,0x1B}, {0xFF,0xFF,0xFF} },
	  8, 4, 6, 1,
	  4, 1,
	  { {0xE5,0xE5,0xE5},	/* border    */
	    {0xF6,0xF6,0xF6},	/* hover     */
	    {0xF0,0xF0,0xF0},	/* pressed   */
	    {0x1B,0x1B,0x1B},	/* focus     - Win11 draws a dark 2px ring */
	    {0xA0,0xA0,0xA0},	/* disabled  */
	    {0xD6,0xD6,0xD6},	/* elevation */
	    {0x00,0x67,0xC0} } },	/* accent    */
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

static void bt_gempens(const PRESET *p)
{
	_WORD i;

	if (xd_ncolours < 16)
		return;

	if (p->pal)
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

	active.hpad   = p->hpad;
	active.vpad   = p->vpad;
	active.gap    = p->gap;
	active.flat   = p->flat;
	active.radius = p->radius;

	bt_gempens(p);
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
