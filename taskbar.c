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
#include "events.h"
#include "lists.h"						/* LSTYPE, needed by slider.h */
#include "slider.h"						/* SLIDER, needed by icon.h */
#include "xfilesys.h"					/* XFILE, needed by window.h/icon.h */
#include "config.h"
#include "font.h"
#include "screen.h"
#include "main.h"
#include "window.h"
#include "icon.h"
#include "taskbar.h"
#include "pstask.h"


/*
 * The Bespoke Desktop taskbar.
 *
 * A borderless window (type BAR_WIND) along the bottom edge of the AES
 * desktop work area. The strip is reserved by shrinking xd_desk *before*
 * dsk_init() builds the desktop object tree, so both the desktop
 * background and all of TeraDesk's own windows respect the bar without
 * further surgery. If the configuration then says the bar is off, the
 * space is given back and the desktop regenerated (tb_apply).
 *
 * Contents, refreshed on the 500 ms MU_TIMER tick from evntloop():
 * a clock (always), and - when the PSCTRL NatFeat answers the probe,
 * i.e. when running on PiSTorm-Atari-JIT - CPU temperature of the Pi,
 * JIT translation-cache use, and the Pi load average. On any other
 * machine (real ST, ARAnyM...) only the clock appears.
 *
 * Updates stall while a modal dialog is open or during long file
 * operations (cooperative GEM); the first tick afterwards shows current
 * values, never stale ones, because PSCTRL data is sampled host-side.
 */

/* PSCTRL NatFeat interface - keep in sync with the emulator's psctrl.h */

#define PSCTRL_GETINT		1L

#define PS_CFG_CPU_MODEL	2L
#define PS_CFG_FPU_MODEL	3L
#define PS_STAT_CACHE_USED	39L
#define PS_STAT_CACHE_TOTAL	40L
#define PS_HOST_SOC_TEMP_MC	64L
#define PS_HOST_TIME_DOS	68L
#define PS_HOST_DATE_DOS	69L

#define DEGREE_CH			'\370'		/* 0xF8: degree sign in the Atari charset */

#define TB_NCELLS			4		/* badge, cpu/fpu, jit cache, temp */
#define TB_CTEXT			28		/* max cell text length */
#define TB_VPAD				3		/* pixels above/below cell */
#define TB_HPAD				6		/* pixels left/right of cell text */
#define TB_GAP				8		/* pixels between cells */

typedef struct
{
	XW_INTVARS;
} TB_WINDOW;

_WORD tb_height = 0;				/* reserved strip height; 0 = no bar */

static WINDOW *tb_window = NULL;	/* the bar window, NULL until opened */
static GRECT tb_rect;				/* outer bar rectangle (screen coords) */
static long tb_psid = 0;			/* PSCTRL feature id; 0 = not present */
static struct nf_ops *tb_nf = NULL;	/* NatFeats call table */

static char tb_cell[TB_NCELLS][TB_CTEXT];	/* left-hand cell texts */
static char tb_clock[24];					/* right-hand date + clock text */
static char tb_cpustr[16];					/* "68040/FPU" etc., built once */
static GRECT tb_badge;						/* screen rect of the PiSTorm button */
static bool tb_dirty = FALSE;				/* content changed since drawn */
static _WORD tb_cw;							/* actual text cell metrics in use */
static _WORD tb_ch;


/*
 * Call PS_GETINT for one index. Only valid if tb_psid != 0.
 */

static long tb_ps(long index)
{
	return tb_nf->call(tb_psid | PSCTRL_GETINT, index);
}


/*
 * Public PS_GETINT for other modules (the monitor window);
 * returns -1 when PSCTRL is not present
 */

long tb_psget(long index)
{
	if (tb_psid == 0)
		return -1L;

	return tb_ps(index);
}


/*
 * Set the GEMDOS clock from the Pi's (NTP-synced) wall clock. The
 * Atari has no battery RTC, so without this the system time counts
 * from 00:00 at power-on - wrong on the taskbar and on every file
 * timestamp. Called at startup and hourly against drift. Harmless
 * no-op on an emulator without the time indices (returns -1).
 */

static void tb_timesync(void)
{
	long dd, dt;

	if (tb_psid == 0)
		return;

	dd = tb_ps(PS_HOST_DATE_DOS);
	dt = tb_ps(PS_HOST_TIME_DOS);

	if (dd > 0 && dt >= 0)
	{
		Tsetdate((_UWORD) dd);
		Tsettime((_UWORD) dt);
	}
}


/*
 * Draw a horizontal or vertical line in the given VDI colour
 */

static void tb_line(_WORD x1, _WORD y1, _WORD x2, _WORD y2, _WORD colour)
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
 * Draw one sunken cell with text in it, at *x (which is advanced).
 * Text is drawn in the default (window text) font.
 */

static void tb_drawcell(_WORD *x, char *text, bool raised)
{
	_WORD dark = (xd_ncolours >= 16) ? G_LBLACK : G_BLACK;
	_WORD w = (_WORD) strlen(text) * tb_cw + 2 * TB_HPAD;
	_WORD y1 = tb_rect.g_y + TB_VPAD;
	_WORD y2 = tb_rect.g_y + tb_rect.g_h - TB_VPAD - 1;
	GRECT in;

	/* cell interior */

	in.g_x = *x + 1;
	in.g_y = y1 + 1;
	in.g_w = w - 2;
	in.g_h = y2 - y1 - 1;
	clr_object(&in, G_WHITE, -1);

	if (raised)
	{
		/* raised bevel: this cell is a button (the PiSTorm badge) */

		tb_line(*x, y1, *x + w - 1, y1, G_WHITE);
		tb_line(*x, y1, *x, y2, G_WHITE);
		tb_line(*x, y2, *x + w - 1, y2, dark);
		tb_line(*x + w - 1, y1 + 1, *x + w - 1, y2, dark);
	} else
	{
		/* sunken bevel: dark top/left, light bottom/right */

		tb_line(*x, y1, *x + w - 1, y1, dark);
		tb_line(*x, y1, *x, y2, dark);
		tb_line(*x, y2, *x + w - 1, y2, G_WHITE);
		tb_line(*x + w - 1, y1 + 1, *x + w - 1, y2, G_WHITE);
	}

	/* the text; the font is set to top-of-cell alignment */

	w_transptext(*x + TB_HPAD, in.g_y + (in.g_h - tb_ch) / 2, text);

	*x += w + TB_GAP;
}


/*
 * Select the bar text font: the default (system) font scaled up so the
 * character cell fills the bar interior. vst_height() returns the
 * metrics actually granted, which the layout then uses - so a VDI
 * without a suitably large font stays consistent, just smaller.
 */

static void tb_setfont(void)
{
	_WORD chw, chh, celw, celh;
	_WORD want = tb_rect.g_h - 2 * TB_VPAD - 8;

	set_txt_default(&def_font);

	if (want < def_font.ch)
		want = def_font.ch;

	vst_height(vdi_handle, want, &chw, &chh, &celw, &celh);

	tb_cw = celw;
	tb_ch = celh;
}


/*
 * Draw the complete bar, clipped to *clip. This is the only routine
 * that actually renders; callers do the update/mouse/clip bracketing.
 */

static void tb_drawpart(GRECT *clip)
{
	_WORD i, x;
	_WORD dark = (xd_ncolours >= 16) ? G_LBLACK : G_BLACK;
	_WORD grey = (xd_ncolours >= 16) ? G_LWHITE : G_WHITE;
	GRECT in;

	xd_clip_on(clip);
	tb_setfont();

	/* bar background with a raised top edge and a dark bottom edge */

	in = tb_rect;
	clr_object(&in, grey, -1);

	tb_line(tb_rect.g_x, tb_rect.g_y, tb_rect.g_x + tb_rect.g_w - 1, tb_rect.g_y, G_WHITE);
	tb_line(tb_rect.g_x, tb_rect.g_y + tb_rect.g_h - 1,
			tb_rect.g_x + tb_rect.g_w - 1, tb_rect.g_y + tb_rect.g_h - 1, dark);

	/* left-hand cells; cell 0 (the PiSTorm badge) is a raised button
	 * that opens the monitor window, so remember where it is */

	x = tb_rect.g_x + TB_GAP;

	for (i = 0; i < TB_NCELLS; i++)
	{
		if (tb_cell[i][0] != 0)
		{
			_WORD x0 = x;

			tb_drawcell(&x, tb_cell[i], (i == 0));

			if (i == 0)
			{
				tb_badge.g_x = x0;
				tb_badge.g_y = tb_rect.g_y + TB_VPAD;
				tb_badge.g_w = x - TB_GAP - x0;
				tb_badge.g_h = tb_rect.g_h - 2 * TB_VPAD;
			}
		}
	}

	/* the clock, right-aligned */

	if (tb_clock[0] != 0)
	{
		x = tb_rect.g_x + tb_rect.g_w - TB_GAP -
			((_WORD) strlen(tb_clock) * tb_cw + 2 * TB_HPAD);
		tb_drawcell(&x, tb_clock, FALSE);
	}

	xd_clip_off();
}


/*
 * Redraw the bar over the rectangle list of its window.
 * If area is NULL the whole bar is redrawn.
 */

static void tb_update(GRECT *area)
{
	GRECT r1, r2, in;

	if (tb_window == NULL)
		return;

	r1 = (area != NULL) ? *area : tb_rect;

	xd_begupdate();
	xd_mouse_off();

	xw_getfirst(tb_window, &r2);

	while (r2.g_w != 0 && r2.g_h != 0)
	{
		if (xd_rcintersect(&r1, &r2, &in))
			tb_drawpart(&in);

		xw_getnext(tb_window, &r2);
	}

	xd_mouse_on();
	xd_endupdate();

	tb_dirty = FALSE;
}


/*
 * WM_REDRAW handler for the bar window (called from xw_hndlmessage
 * with the damaged area; the rectangle walk is done here)
 */

static void tb_redraw(WINDOW *w, GRECT *area)
{
	(void) w;
	tb_update(area);
}


/*
 * Button clicks on the bar: the PiSTorm badge opens the monitor window
 */

static void tb_button(WINDOW *w, _WORD x, _WORD y, _WORD n, _WORD bstate, _WORD kstate)
{
	(void) w;
	(void) n;
	(void) bstate;
	(void) kstate;

	if (tb_badge.g_w > 0 &&
		x >= tb_badge.g_x && x < tb_badge.g_x + tb_badge.g_w &&
		y >= tb_badge.g_y && y < tb_badge.g_y + tb_badge.g_h)
	{
		mn_open();
	}
}


static WD_FUNC tb_functions = {
	0L,									/* handle keypress */
	tb_button,							/* handle button */
	tb_redraw,							/* redraw */
	xw_nop1,							/* topped */
	xw_nop1,							/* bottomed */
	xw_nop1,							/* newtop */
	0L,									/* closed */
	0L,									/* fulled */
	xw_nop2,							/* arrowed */
	0L,									/* hslid */
	0L,									/* vslid */
	0L,									/* sized */
	0L,									/* moved */
	0L,									/* hndlmenu */
	0L,									/* top */
	0L,									/* iconify */
	0L									/* uniconify */
};


/*
 * Append exactly two decimal digits (00..99) to a string
 */

static char *tb_two(char *d, _WORD v)
{
	*d++ = (char) ('0' + (v / 10) % 10);
	*d++ = (char) ('0' + v % 10);
	return d;
}


/*
 * Append a string (no padding)
 */

static char *tb_app(char *d, const char *s)
{
	while (*s)
		*d++ = *s++;

	return d;
}


/*
 * Day of week, 0 = Sunday (Sakamoto's method)
 */

static _WORD tb_dow(_WORD y, _WORD m, _WORD d)
{
	static const _WORD t[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };

	if (m < 3)
		y -= 1;

	return (_WORD) ((y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7);
}


/*
 * Rebuild the cell texts. Returns TRUE if anything changed.
 *
 * Note: all formatting here is done with ltoa()/manual digits, in the
 * style of datimstr() in dir.c - TeraDesk replaces sprintf() with a
 * minimal formatter (stringf.c) that supports neither zero-padding
 * nor '%%', so the standard idioms would render wrongly.
 */

static bool tb_build(void)
{
	static const char *const dayn[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
	static const char *const monn[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
										  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	char new_cell[TB_NCELLS][TB_CTEXT];
	char new_clock[24];
	unsigned short t;
	_WORD hh, mm;
	char *p;
	bool changed = FALSE;
	_WORD i;

	memclr(new_cell, sizeof(new_cell));

	/*
	 * Date and clock, from GEMDOS time/date words, as datimstr() does.
	 * Formats (teradesk.inf):
	 *   tbdf = 0  "Thu 6 Aug ..."  (default)
	 *   tbdf = 1  "Thu Aug 6 ..."
	 *   tbdf = 2  no date, clock only
	 *   tbtf = 0  24-hour "02:35" (default)
	 *   tbtf = 1  12-hour "2:35am"
	 */

	t = (unsigned short) Tgettime();
	hh = (_WORD) ((t >> 11) & 0x1F);
	mm = (_WORD) ((t >> 5) & 0x3F);

	p = new_clock;

	if (options.tbdf != 2)
	{
		unsigned short dt = (unsigned short) Tgetdate();
		_WORD yy = (_WORD) (1980 + ((dt >> 9) & 0x7F));
		_WORD mo = (_WORD) ((dt >> 5) & 0x0F);
		_WORD dd = (_WORD) (dt & 0x1F);

		if (mo < 1)
			mo = 1;
		if (mo > 12)
			mo = 12;
		if (dd < 1)
			dd = 1;

		p = tb_app(p, dayn[tb_dow(yy, mo, dd)]);
		*p++ = ' ';

		if (options.tbdf == 1)
		{
			p = tb_app(p, monn[mo - 1]);	/* "Thu Aug 6" */
			*p++ = ' ';

			if (dd >= 10)
				*p++ = (char) ('0' + dd / 10);
			*p++ = (char) ('0' + dd % 10);
		} else
		{
			if (dd >= 10)					/* "Thu 6 Aug" */
				*p++ = (char) ('0' + dd / 10);
			*p++ = (char) ('0' + dd % 10);
			*p++ = ' ';
			p = tb_app(p, monn[mo - 1]);
		}

		*p++ = ' ';
	}

	if (options.tbtf == 1)
	{
		_WORD h12 = hh % 12;				/* 12-hour with am/pm */

		if (h12 == 0)
			h12 = 12;

		if (h12 >= 10)
			*p++ = (char) ('0' + h12 / 10);
		*p++ = (char) ('0' + h12 % 10);
		*p++ = ':';
		p = tb_two(p, mm);
		p = tb_app(p, (hh >= 12) ? "pm" : "am");
	} else
	{
		p = tb_two(p, hh);					/* 24-hour */
		*p++ = ':';
		p = tb_two(p, mm);
	}

	*p = 0;

	/* PiSTorm cells, only when PSCTRL answered the probe */

	if (tb_psid != 0)
	{
		long temp = tb_ps(PS_HOST_SOC_TEMP_MC);
		long used = tb_ps(PS_STAT_CACHE_USED);
		long total = tb_ps(PS_STAT_CACHE_TOTAL);

		strcpy(new_cell[0], "PiSTorm");

		if (tb_cpustr[0] != 0)
			strcpy(new_cell[1], tb_cpustr);

		if (total > 0)
		{
			strcpy(new_cell[2], "JIT ");
			ltoa((used * 100L) / total, &new_cell[2][4], 10);
			strcat(new_cell[2], "%");
		}

		if (temp > 0)
		{
			p = new_cell[3];
			strcpy(p, "Temp ");
			ltoa(temp / 1000L, p + 5, 10);
			p += strlen(p);
			*p++ = '.';
			*p++ = (char) ('0' + (temp % 1000L) / 100L);
			*p++ = DEGREE_CH;
			*p++ = 'C';
			*p = 0;
		}
	}

	for (i = 0; i < TB_NCELLS; i++)
	{
		if (strcmp(new_cell[i], tb_cell[i]) != 0)
		{
			strcpy(tb_cell[i], new_cell[i]);
			changed = TRUE;
		}
	}

	if (strcmp(new_clock, tb_clock) != 0)
	{
		strcpy(tb_clock, new_clock);
		changed = TRUE;
	}

	return changed;
}


/*
 * Reserve the taskbar strip: shrink the AES desktop work area kept in
 * xd_desk. Must be called after xd_desk is read but BEFORE dsk_init()
 * builds the desktop object tree, so that the tree root and all window
 * size calculations already respect the bar. The bar is on by default;
 * tb_apply() gives the space back if the configuration disables it.
 */

void tb_reserve(void)
{
	/*
	 * Match the AES menu bar: its height is the gap between the top of
	 * the screen and the top of the desktop work area. The bar gets
	 * that much for the cell interior (so bar text is as large as menu
	 * text), plus the sunken bevels and padding around the cells.
	 */

	_WORD mbar = xd_desk.g_y - xd_screen.g_y;

	if (mbar < def_font.ch)				/* implausible: fall back */
		mbar = 2 * def_font.ch;

	tb_height = mbar + 2 * TB_VPAD + 6;

	tb_rect.g_x = xd_desk.g_x;
	tb_rect.g_y = xd_desk.g_y + xd_desk.g_h - tb_height;
	tb_rect.g_w = xd_desk.g_w;
	tb_rect.g_h = tb_height;

	xd_desk.g_h -= tb_height;
}


/*
 * Create and open the bar window over the reserved strip,
 * and probe for the PSCTRL NatFeat.
 */

static void tb_open(void)
{
	GRECT size;
	int error;

	if (tb_window != NULL)
		return;

	tb_nf = nf_init();

	if (tb_nf != NULL)
		tb_psid = nf_get_id("PSCTRL");

	tb_timesync();						/* set the system clock from the Pi */

	/* The CPU/FPU cell text - configuration values, so built once */

	if (tb_psid != 0)
	{
		long cpu = tb_ps(PS_CFG_CPU_MODEL);
		long fpu = tb_ps(PS_CFG_FPU_MODEL);

		if (cpu > 0)
		{
			ltoa(cpu, tb_cpustr, 10);

			if (fpu > 0)
			{
				/* on-die FPU (040/060): "68040+FPU"; external
				 * coprocessor: "68030/68882" */

				if (fpu == cpu)
				{
					strcat(tb_cpustr, "+FPU");
				} else
				{
					strcat(tb_cpustr, "/");
					ltoa(fpu, tb_cpustr + strlen(tb_cpustr), 10);
				}
			}
		}
	}

	size = tb_rect;

	tb_window = xw_create(BAR_WIND, &tb_functions, 0, &size, sizeof(TB_WINDOW), NULL, &error);

	if (tb_window != NULL)
	{
		tb_build();
		xw_open(tb_window, &size);		/* first WM_REDRAW paints it */
	} else
	{
		xform_error(error);
	}
}


/*
 * Act on the loaded configuration: open the bar, or give the reserved
 * space back to the desktop if the bar was disabled in teradesk.inf.
 */

void tb_apply(void)
{
	if (options.tbar != 0)
	{
		/*
		 * Optional configured height ('tbrh' in teradesk.inf; 0 = the
		 * default of twice the system font height). Adjust the already
		 * reserved strip and the desktop tree root before opening.
		 */

		if (options.tbarh != 0 && options.tbarh != tb_height && desktop != NULL)
		{
			_WORD d = options.tbarh - tb_height;

			xd_desk.g_h -= d;
			tb_height = options.tbarh;
			tb_rect.g_y -= d;
			tb_rect.g_h = tb_height;

			desktop[0].ob_height = xd_desk.g_h;
			regen_desktop(desktop);
		}

		tb_open();
	} else if (tb_height != 0)
	{
		xd_desk.g_h += tb_height;
		tb_height = 0;

		/* resize the desktop tree root and redraw */

		if (desktop != NULL)
		{
			desktop[0].ob_x = xd_desk.g_x;
			desktop[0].ob_y = xd_desk.g_y;
			desktop[0].ob_width = xd_desk.g_w;
			desktop[0].ob_height = xd_desk.g_h;
			regen_desktop(desktop);
		}
	}
}


/*
 * The 500 ms timer tick from evntloop(): refresh contents,
 * redraw only if something actually changed.
 */

void tb_tick(void)
{
	static _WORD resync = 0;

	if (tb_window == NULL)
		return;

	/* hourly clock re-sync against drift (7200 ticks of 500 ms) */

	if (++resync >= 7200)
	{
		resync = 0;
		tb_timesync();
	}

	if (tb_build() || tb_dirty)
		tb_update(NULL);

	/* refresh the monitor window, if open */

	mn_tick();
}


/*
 * Close and remove the bar window (at shutdown)
 */

void tb_close(void)
{
	mn_close();							/* the monitor window too */

	if (tb_window != NULL)
	{
		/* note: xw_closedelete() exists in xwindow.h only - its body
		 * is #if 0'd out in xwindow.c, so close and delete separately */

		xw_close(tb_window);
		xw_delete(tb_window);
		tb_window = NULL;
	}
}
