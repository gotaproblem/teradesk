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

#include "desk.h"						/* vdi_handle */
#include "btheme.h"


/*
 * The theme presets. Colours are VDI-16 indices; the metrics currently
 * match the taskbar's existing constants so the retrofit does not shift
 * any geometry. Add a row here to add a theme - the settings-page
 * picker enumerates them via bt_count()/bt_name().
 */

static const BTHEME themes[] =
{
	/* 0: GEM Grey - the classic look. With this active the retrofit is
	 *    a visual no-op, so any difference on screen flags a bug. */
	{ "GEM Grey",
	  G_WHITE, G_BLACK, G_WHITE, G_LBLACK,
	  G_LBLACK, G_WHITE, G_RED, G_WHITE,
	  G_LWHITE, G_LBLACK, G_WHITE,
	  6, 3, 8, 0 },

	/* 1: Slate - darker panels, blue selection */
	{ "Slate",
	  G_LWHITE, G_BLACK, G_WHITE, G_BLACK,
	  G_BLUE, G_WHITE, G_RED, G_WHITE,
	  G_LBLACK, G_BLACK, G_WHITE,
	  6, 3, 8, 0 },

	/* 2: Flat - no bevels, blue selection, white cells */
	{ "Flat",
	  G_WHITE, G_BLACK, G_WHITE, G_WHITE,
	  G_BLUE, G_WHITE, G_RED, G_WHITE,
	  G_WHITE, G_BLUE, G_WHITE,
	  7, 3, 8, 1 },
};

#define NTHEMES		((_WORD) (sizeof(themes) / sizeof(themes[0])))

static _WORD cur = 0;


void bt_use(_WORD id)
{
	if (id < 0)
		id = 0;
	if (id >= NTHEMES)
		id = NTHEMES - 1;

	cur = id;
}

_WORD bt_current(void)
{
	return cur;
}

_WORD bt_count(void)
{
	return NTHEMES;
}

const char *bt_name(_WORD id)
{
	if (id < 0 || id >= NTHEMES)
		return "";

	return themes[id].name;
}

const BTHEME *bt(void)
{
	return &themes[cur];
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
 * Draw a one-pixel 3D edge around r. On a mono / low-colour screen the
 * grey bevel colours collapse, so both edges fall back to a black frame
 * (matching what the panels did with the >= 16 colour test).
 */

void bt_bevel(GRECT *r, _WORD kind)
{
	const BTHEME *t = bt();
	_WORD x1 = r->g_x;
	_WORD y1 = r->g_y;
	_WORD x2 = r->g_x + r->g_w - 1;
	_WORD y2 = r->g_y + r->g_h - 1;
	_WORD tl, br;

	if (t->flat)
		return;

	if (xd_ncolours < 16)
	{
		tl = br = G_BLACK;
	} else if (kind == BT_RAISED)
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
