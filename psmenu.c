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

#include "resource.h"
#include "desk.h"
#include "error.h"
#include "xfilesys.h"					/* XFILE, needed by window.h */
#include "file.h"
#include "lists.h"						/* LSTYPE, needed by filetype.h */
#include "font.h"						/* XDFONT, needed by window.h */
#include "window.h"						/* wd_upd_type, needed by applik.h */
#include "filetype.h"					/* FTYPE; must be before applik.h */
#include "prgtype.h"
#include "applik.h"
#include "psmenu.h"


/*
 * The "PiSTorm" menu.
 *
 * GEM menus are ordinary object trees, so instead of editing the five
 * language desktop.rsc files, the menu bar is extended at runtime: the
 * tree loaded from the resource is copied into a larger array, a new
 * G_TITLE is linked after the last title, and a dropdown G_BOX with one
 * G_STRING per application is linked after the last dropdown. Geometry
 * (dropdown y position, box style) is copied from the existing menu
 * objects, so the result follows the AES's own rendering.
 *
 * The dropdown is filled by scanning the directory named by the 'psdr'
 * key in teradesk.inf (e.g. the HOSTFS share with the PiSTorm GEM
 * binaries) for programs, using the same criteria as directory windows
 * (prg_isprogram()). Selecting an entry launches it through the normal
 * application-start machinery, so GEM/TOS detection, ARGV and memory
 * limits all behave as for a double-click.
 *
 * The menu appears only when NatFeats answer the probe (i.e. under
 * PiSTorm-Atari-JIT or another emulator) and the scan finds at least
 * one program; on a real ST the menu bar is unchanged.
 */

#define PSM_MAXAPPS 20					/* at most this many menu entries */

static char psm_title_txt[] = " PiSTorm ";

static _WORD psm_title = -1;			/* object index of the added title */
static _WORD psm_first = -1;			/* object index of the first app item */
static _WORD psm_napps = 0;				/* number of menu entries */

static LNAME psm_text[PSM_MAXAPPS];		/* item strings ("  NAME") */
static char *psm_path[PSM_MAXAPPS];		/* full program paths (allocated) */


/*
 * Scan options.psdir for programs; fill psm_text/psm_path (sorted by name)
 */

static void psm_scan(void)
{
	XDIR *dir;
	XATTR attr;
	_WORD error, i, j;
	char *name;

	if (options.psdir[0] == 0)
		return;

	if ((dir = x_opendir(options.psdir, &error)) == NULL)
		return;

	while (psm_napps < PSM_MAXAPPS)
	{
		if (x_xreaddir(dir, &name, sizeof(VLNAME), &attr) != 0)
			break;

		if (((attr.st_mode & S_IFMT) == S_IFREG) && prg_isprogram(name))
		{
			char *full = fn_make_path(options.psdir, name);

			if (full == NULL)
				break;

			/* insertion sort by name, case-insensitive not needed (8+3) */

			i = 0;
			while (i < psm_napps && strcmp(&psm_text[i][2], name) < 0)
				i++;

			for (j = psm_napps; j > i; j--)
			{
				strcpy(psm_text[j], psm_text[j - 1]);
				psm_path[j] = psm_path[j - 1];
			}

			strcpy(psm_text[i], "  ");
			strsncpy(&psm_text[i][2], name, sizeof(LNAME) - 4);
			psm_path[i] = full;

			psm_napps++;
		}
	}

	x_closedir(dir);
}


/*
 * Build the extended menu tree and install it. Call once, after the
 * configuration has been loaded (needs options.psdir) and before the
 * event loop starts.
 */

void ps_menu_init(void)
{
	OBJECT *m;
	_WORD nold, total, bar, active, screen;
	_WORD firsttitle, lasttitle, firstbox, lastbox;
	_WORD title, box, it, i, w, maxw;

	/* Only on an emulator host (PiSTorm, ARAnyM...) */

	if (nf_init() == NULL)
		return;

	psm_scan();

	if (psm_napps == 0)
		return;

	/* Count the objects of the current menu tree */

	nold = 0;

	while ((menu[nold].ob_flags & OF_LASTOB) == 0)
		nold++;

	nold++;

	total = nold + 2 + psm_napps;		/* + title + box + items */

	if ((m = malloc_chk((size_t) total * sizeof(OBJECT))) == NULL)
		return;

	memcpy(m, menu, (size_t) nold * sizeof(OBJECT));
	m[nold - 1].ob_flags &= ~OF_LASTOB;

	/* Landmarks in the canonical AES menu tree layout */

	bar = m[ROOT].ob_head;				/* the menu bar box */
	active = m[bar].ob_head;			/* parent of the titles */
	screen = m[bar].ob_next;			/* parent of the dropdown boxes */
	firsttitle = m[active].ob_head;
	lasttitle = m[active].ob_tail;
	firstbox = m[screen].ob_head;
	lastbox = m[screen].ob_tail;

	/* Sanity: bail out if the tree does not have the canonical layout */

	if (m[bar].ob_type != G_BOX || m[firsttitle].ob_type != G_TITLE || m[firstbox].ob_type != G_BOX)
	{
		free(m);
		return;
	}

	title = nold;
	box = nold + 1;

	/* The new title, linked after the last existing one */

	m[title].ob_next = active;
	m[title].ob_head = -1;
	m[title].ob_tail = -1;
	m[title].ob_type = G_TITLE;
	m[title].ob_flags = OF_NONE;
	m[title].ob_state = OS_NORMAL;
	m[title].ob_spec.free_string = psm_title_txt;
	m[title].ob_x = m[lasttitle].ob_x + m[lasttitle].ob_width;
	m[title].ob_y = m[lasttitle].ob_y;
	m[title].ob_width = (_WORD) strlen(psm_title_txt) * xd_fnt_w;
	m[title].ob_height = m[lasttitle].ob_height;

	m[lasttitle].ob_next = title;
	m[active].ob_tail = title;

	/* Width of the widest item decides the dropdown width */

	maxw = 0;

	for (i = 0; i < psm_napps; i++)
	{
		w = (_WORD) strlen(psm_text[i]) + 2;

		if (w > maxw)
			maxw = w;
	}

	maxw *= xd_fnt_w;

	/* The dropdown box: style copied from the first existing dropdown */

	m[box].ob_next = screen;
	m[box].ob_head = box + 1;
	m[box].ob_tail = box + psm_napps;
	m[box].ob_type = G_BOX;
	m[box].ob_flags = OF_NONE;
	m[box].ob_state = OS_NORMAL;
	m[box].ob_spec = m[firstbox].ob_spec;
	m[box].ob_x = m[firstbox].ob_x + (m[title].ob_x - m[firsttitle].ob_x);
	m[box].ob_y = m[firstbox].ob_y;
	m[box].ob_width = maxw;
	m[box].ob_height = psm_napps * xd_fnt_h;

	m[lastbox].ob_next = box;
	m[screen].ob_tail = box;

	/* The items */

	for (i = 0; i < psm_napps; i++)
	{
		it = box + 1 + i;

		m[it].ob_next = (i == psm_napps - 1) ? box : it + 1;
		m[it].ob_head = -1;
		m[it].ob_tail = -1;
		m[it].ob_type = G_STRING;
		m[it].ob_flags = (i == psm_napps - 1) ? OF_LASTOB : OF_NONE;
		m[it].ob_state = OS_NORMAL;
		m[it].ob_spec.free_string = psm_text[i];
		m[it].ob_x = 0;
		m[it].ob_y = i * xd_fnt_h;
		m[it].ob_width = maxw;
		m[it].ob_height = xd_fnt_h;
	}

	/* Install: swap the global tree and redisplay the menu bar */

	menu = m;
	psm_title = title;
	psm_first = box + 1;

	menu_bar(menu, 1);
}


/*
 * Handle a menu selection if it belongs to the PiSTorm menu.
 * Returns TRUE when the event was consumed.
 */

bool ps_menu_select(_WORD title, _WORD item, _WORD kstate)
{
	_WORD i = item - psm_first;

	if (psm_title < 0 || title != psm_title)
		return FALSE;

	if (i >= 0 && i < psm_napps && psm_path[i] != NULL)
		app_exec(psm_path[i], NULL, NULL, NULL, 0, kstate);

	return TRUE;
}
