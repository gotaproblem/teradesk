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
 * registers (BT_PAL_BASE + role) */

enum
{
	R_FACE, R_TEXT, R_LIGHT, R_DARK, R_SELBG, R_SELFG,
	R_ALBG, R_ALFG, R_PANEL, R_TITBG, R_TITFG, R_PAPER, R_N
};

typedef struct
{
	char name[16];
	_WORD pal;						/* 1 = load rgb[] (colour screen) */
	unsigned char rgb[R_N][3];		/* 0-255 per component */
	_WORD hpad, vpad, gap, flat;
} PRESET;

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
};

#define NPRESETS	((_WORD) (sizeof(presets) / sizeof(presets[0])))

/* The GEM Grey / mono fallback: standard VDI-16 indices per role. */

static const _WORD fb[R_N] =
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
	G_WHITE		/* paper    */
};

static _WORD cur = 0;
static BTHEME active;					/* resolved colour indices + metrics */


/*
 * Resolve the active theme: on a colour screen, an RGB theme loads its
 * palette into the reserved registers and uses those indices; GEM Grey
 * and mono use the standard-16 fallback.
 */

static void bt_resolve(void)
{
	const PRESET *p = &presets[cur];
	_WORD idx[R_N];
	_WORD i;

	if (p->pal && xd_ncolours >= 16)
	{
		for (i = 0; i < R_N; i++)
		{
			_WORD rgb[3];

			/* VDI colour components are 0-1000, not 0-255 */
			rgb[0] = (_WORD) ((long) p->rgb[i][0] * 1000L / 255L);
			rgb[1] = (_WORD) ((long) p->rgb[i][1] * 1000L / 255L);
			rgb[2] = (_WORD) ((long) p->rgb[i][2] * 1000L / 255L);

			vs_color(vdi_handle, BT_PAL_BASE + i, rgb);
			idx[i] = BT_PAL_BASE + i;
		}
	} else
	{
		for (i = 0; i < R_N; i++)
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

	active.hpad = p->hpad;
	active.vpad = p->vpad;
	active.gap  = p->gap;
	active.flat = p->flat;
}


void bt_use(_WORD id)
{
	if (id < 0)
		id = 0;
	if (id >= NPRESETS)
		id = NPRESETS - 1;

	cur = id;
	bt_resolve();
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
