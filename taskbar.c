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
#define PS_HOST_THROTTLED	70L
#define PS_PI_MODEL			74L
#define PS_PI_RAM_MB		75L

#define DEGREE_CH			'\370'		/* 0xF8: degree sign in the Atari charset */

#define TB_NCELLS			5		/* badge, pi model, temp, cpu/fpu, jit */
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
static char tb_pistr[16];					/* "Pi4B 2GB" etc., built once */
static GRECT tb_badge;						/* screen rect of the PiSTorm button */
static _WORD tb_throttled = 0;				/* Pi reports active throttling */
static _WORD tb_flash = 0;					/* alert flash phase, toggles per tick */
static GRECT tb_clockr;						/* screen rect of the clock cell */

/* Hover state: which rect MU_M1 watches, and what the mouse is over */

#define TB_HOV_NONE		-1
#define TB_HOV_BADGE	0
#define TB_HOV_CLOCK	1
#define TB_HOV_TEMP		2
#define TB_HOV_JIT		3

static _WORD tb_hovin = 0;					/* 0 = mouse outside bar, 1 = inside */
static GRECT tb_hovrect;					/* rect currently watched by MU_M1 */
static _WORD tb_hovtgt = TB_HOV_NONE;		/* what the mouse is hovering over */
static _WORD tb_dwell = 0;					/* ticks spent hovering the target */

static WINDOW *tip_win = NULL;				/* the tooltip window */

#define TIP_MAXLINES	6
#define TIP_MAXLEN		36

static char tip_lines[TIP_MAXLINES][TIP_MAXLEN];	/* tooltip text lines */
static _WORD tip_nlines = 0;
static _WORD tip_kind = 0;					/* 0 none, 1 uptime, 2 throttle */
static _WORD tip_minw = 0;					/* min box width in chars (live tips) */
static GRECT tb_tempr;						/* screen rect of the Temp cell */
static GRECT tb_jitr;						/* screen rect of the JIT cell */
static GRECT tb_pager[DSK_NDESKS];			/* the desktop pager buttons */
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
 * Fire the host-side desk-slide transition (PSCTRL subop 2).
 * dir 1 = old desk exits left (moving to a higher desk), 2 = mirrored.
 * Harmless no-op without PSCTRL or on an emulator without the subop.
 */

void tb_psfx(long dir)
{
	if (tb_psid != 0)
		tb_nf->call(tb_psid | 2L, dir);
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

static void tb_drawcell(_WORD *x, char *text, bool raised, bool alert, bool sel)
{
	_WORD dark = (xd_ncolours >= 16) ? G_LBLACK : G_BLACK;
	_WORD w = (_WORD) strlen(text) * tb_cw + 2 * TB_HPAD;
	_WORD y1 = tb_rect.g_y + TB_VPAD;
	_WORD y2 = tb_rect.g_y + tb_rect.g_h - TB_VPAD - 1;
	_WORD bg = G_WHITE;
	_WORD fg = G_BLACK;
	GRECT in;

	if (alert)
	{
		/* alert phase: red cell, white text (inverted on mono) */

		bg = (xd_ncolours >= 16) ? G_RED : G_BLACK;
		fg = G_WHITE;
	} else if (sel)
	{
		/* selected (the active desk's pager button): dark grey cell,
		 * white text - the sunken-vs-raised bevel alone is a single
		 * pixel of difference and invisible at 1920x1080 */

		bg = dark;
		fg = G_WHITE;
	}

	/* cell interior */

	in.g_x = *x + 1;
	in.g_y = y1 + 1;
	in.g_w = w - 2;
	in.g_h = y2 - y1 - 1;
	clr_object(&in, bg, -1);

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

	vst_color(vdi_handle, fg);
	w_transptext(*x + TB_HPAD, in.g_y + (in.g_h - tb_ch) / 2, text);
	vst_color(vdi_handle, G_BLACK);

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

	/* left-hand cells, taskbar v2 order:
	 * 0 PiSTorm button (system menu), 1 Pi model+RAM, 2 Temp,
	 * 3 CPU/FPU, 4 JIT button (JIT panel) */

	x = tb_rect.g_x + TB_GAP;

	for (i = 0; i < TB_NCELLS; i++)
	{
		if (tb_cell[i][0] != 0)
		{
			_WORD x0 = x;
			bool alert = (i == 2 && tb_throttled != 0 && tb_flash != 0);

			tb_drawcell(&x, tb_cell[i], (i == 0 || i == 4), alert, FALSE);

			if (i == 0)
			{
				tb_badge.g_x = x0;
				tb_badge.g_y = tb_rect.g_y + TB_VPAD;
				tb_badge.g_w = x - TB_GAP - x0;
				tb_badge.g_h = tb_rect.g_h - 2 * TB_VPAD;
			} else if (i == 4)
			{
				/* the JIT button: clicking it toggles the JIT panel */

				tb_jitr.g_x = x0;
				tb_jitr.g_y = tb_rect.g_y + TB_VPAD;
				tb_jitr.g_w = x - TB_GAP - x0;
				tb_jitr.g_h = tb_rect.g_h - 2 * TB_VPAD;
			} else if (i == 2)
			{
				/* the Temp cell: hovering it pops up the throttle events */

				tb_tempr.g_x = x0;
				tb_tempr.g_y = tb_rect.g_y + TB_VPAD;
				tb_tempr.g_w = x - TB_GAP - x0;
				tb_tempr.g_h = tb_rect.g_h - 2 * TB_VPAD;
			}
		}
	}

	/* the desktop pager, centred as a group in the middle of the bar:
	 * one small numbered button per desk, the current one dark */

	{
		_WORD d, cur = dsk_current();
		_WORD pw = (_WORD) (DSK_NDESKS * (tb_cw + 2 * TB_HPAD) +
							(DSK_NDESKS - 1) * TB_GAP);
		char nm[2];

		x = tb_rect.g_x + (tb_rect.g_w - pw) / 2;
		nm[1] = 0;

		for (d = 0; d < DSK_NDESKS; d++)
		{
			_WORD p0 = x;

			nm[0] = (char) ('1' + d);
			tb_drawcell(&x, nm, (d != cur), FALSE, (d == cur));

			tb_pager[d].g_x = p0;
			tb_pager[d].g_y = tb_rect.g_y + TB_VPAD;
			tb_pager[d].g_w = x - TB_GAP - p0;
			tb_pager[d].g_h = tb_rect.g_h - 2 * TB_VPAD;
		}
	}

	/* the clock, right-aligned; remember its rect for hover */

	if (tb_clock[0] != 0)
	{
		_WORD x0;

		x = tb_rect.g_x + tb_rect.g_w - TB_GAP -
			((_WORD) strlen(tb_clock) * tb_cw + 2 * TB_HPAD);
		x0 = x;
		tb_drawcell(&x, tb_clock, FALSE, FALSE, FALSE);

		tb_clockr.g_x = x0;
		tb_clockr.g_y = tb_rect.g_y + TB_VPAD;
		tb_clockr.g_w = x - TB_GAP - x0;
		tb_clockr.g_h = tb_rect.g_h - 2 * TB_VPAD;
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
 * Button clicks on the bar: the PiSTorm button toggles the system-tasks
 * menu, the JIT button toggles the JIT panel, the pager switches desks
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
		sm_toggle();
		return;
	}

	if (tb_jitr.g_w > 0 &&
		x >= tb_jitr.g_x && x < tb_jitr.g_x + tb_jitr.g_w &&
		y >= tb_jitr.g_y && y < tb_jitr.g_y + tb_jitr.g_h)
	{
		mn_toggle();
		return;
	}

	/* the desktop pager */

	{
		_WORD d;

		for (d = 0; d < DSK_NDESKS; d++)
		{
			if (tb_pager[d].g_w > 0 &&
				x >= tb_pager[d].g_x && x < tb_pager[d].g_x + tb_pager[d].g_w &&
				y >= tb_pager[d].g_y && y < tb_pager[d].g_y + tb_pager[d].g_h)
			{
				dsk_switch(d);
				tb_dirty = TRUE;		/* repaint the pager promptly */
				return;
			}
		}
	}
}


/* ---- the uptime tooltip ------------------------------------------------ */

static char *tb_two(char *d, _WORD v);
static char *tb_app(char *d, const char *s);

typedef struct
{
	XW_INTVARS;
} TIP_WINDOW;


/*
 * Draw the tooltip: pale box, black frame, the uptime text
 */

static void tip_draw(WINDOW *w, GRECT *area)
{
	GRECT r1, r2, in, work;
	_WORD f[10];

	(void) area;

	if (tip_win == NULL)
		return;

	xw_getwork(tip_win, &work);

	r1 = work;

	xd_begupdate();
	xd_mouse_off();

	xw_getfirst(tip_win, &r2);

	while (r2.g_w != 0 && r2.g_h != 0)
	{
		if (xd_rcintersect(&r1, &r2, &in))
		{
			xd_clip_on(&in);

			clr_object(&work, G_WHITE, -1);

			vsl_color(vdi_handle, G_BLACK);
			f[0] = work.g_x;
			f[1] = work.g_y;
			f[2] = work.g_x + work.g_w - 1;
			f[3] = work.g_y;
			f[4] = work.g_x + work.g_w - 1;
			f[5] = work.g_y + work.g_h - 1;
			f[6] = work.g_x;
			f[7] = work.g_y + work.g_h - 1;
			f[8] = work.g_x;
			f[9] = work.g_y;
			v_pline(vdi_handle, 5, f);

			tb_setfont();
			vst_color(vdi_handle, G_BLACK);

			/* one character cell of margin left/right, half a cell above
			 * and below, and a little leading between lines - the box is
			 * always visibly larger than the text */

			{
				_WORD li;

				for (li = 0; li < tip_nlines; li++)
					w_transptext(work.g_x + tb_cw,
								 work.g_y + tb_ch / 2 + 2 + li * (tb_ch + 2),
								 tip_lines[li]);
			}

			xd_clip_off();
		}

		xw_getnext(tip_win, &r2);
	}

	xd_mouse_on();
	xd_endupdate();

	(void) w;
}


static WD_FUNC tip_functions = {
	0L,									/* handle keypress */
	0L,									/* handle button */
	tip_draw,							/* redraw */
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


static void tip_close(void)
{
	if (tip_win != NULL)
	{
		xw_close(tip_win);
		xw_delete(tip_win);
		tip_win = NULL;
		tip_kind = 0;
	}
}


/*
 * Open the tooltip window sized to tip_lines[], its right edge aligned
 * with the anchor cell, just above the bar. Padding matches tip_draw:
 * a full character cell horizontally, half a cell vertically, plus a
 * little leading between lines - the box is larger than the text.
 */

static void tip_show(GRECT *anchor)
{
	GRECT size;
	_WORD li, len, maxlen = 0;
	int error;

	if (tip_win != NULL || tip_nlines == 0)
		return;

	for (li = 0; li < tip_nlines; li++)
	{
		len = (_WORD) strlen(tip_lines[li]);

		if (len > maxlen)
			maxlen = len;
	}

	if (maxlen < tip_minw)				/* live tips: room for the longest */
		maxlen = tip_minw;				/* state any line can grow into */

	size.g_w = (maxlen + 2) * tb_cw;
	size.g_h = tip_nlines * (tb_ch + 2) + tb_ch + 2;
	size.g_x = anchor->g_x + anchor->g_w - size.g_w;
	size.g_y = tb_rect.g_y - size.g_h - 2;

	if (size.g_x < tb_rect.g_x)
		size.g_x = tb_rect.g_x;

	tip_win = xw_create(TIP_WIND, &tip_functions, 0, &size, sizeof(TIP_WINDOW), NULL, &error);

	if (tip_win != NULL)
	{
		xw_open(tip_win, &size);

		/* sticky on all workspaces, like the bar it belongs to */

		appl_control(-1, 103, (void *) (long) xw_handle(tip_win));
	}
}


/*
 * The uptime tooltip over the clock cell: "Pi up 3d 04:12"
 */

static void tip_uptime_lines(void)
{
	long up;
	char *p;

	tip_nlines = 0;
	up = tb_ps(67L);					/* PS_HOST_UPTIME_S */

	if (up < 0)
		return;

	p = tb_app(tip_lines[0], "Pi up ");

	if (up >= 86400L)
	{
		ltoa(up / 86400L, p, 10);
		p += strlen(p);
		*p++ = 'd';
		*p++ = ' ';
	}

	p = tb_two(p, (_WORD) ((up % 86400L) / 3600L));
	*p++ = ':';
	p = tb_two(p, (_WORD) ((up % 3600L) / 60L));
	*p = 0;

	tip_nlines = 1;
}


static void tip_uptime(void)
{
	if (tip_win != NULL || tb_psid == 0)
		return;

	tip_uptime_lines();

	if (tip_nlines == 0)
		return;

	tip_kind = 1;
	tip_minw = 0;
	tip_show(&tb_clockr);
}


/*
 * One line of the throttle tooltip: event name, then its state from the
 * firmware get_throttled register - "ACTIVE" (low bit), "since boot"
 * (the same bit shifted up 16), or "-" for never.
 */

static void tip_evline(char *dst, const char *name, long thr, _WORD bit)
{
	char *p = tb_app(dst, name);

	if (thr & (1L << bit))
		strcpy(p, "ACTIVE");
	else if (thr & (1L << (bit + 16)))
		strcpy(p, "since boot");
	else
		strcpy(p, "-");
}


/*
 * The throttle-events tooltip over the Temp cell: everything the Pi
 * firmware reports, happening now and seen since boot.
 */

static void tip_throttle_lines(void)
{
	long thr = tb_ps(PS_HOST_THROTTLED);

	if (thr < 0)
	{
		strcpy(tip_lines[0], "No throttle data");
		tip_nlines = 1;
	} else
	{
		strcpy(tip_lines[0], "Pi throttle events");
		tip_evline(tip_lines[1], "Under-voltage : ", thr, 0);
		tip_evline(tip_lines[2], "Freq capped   : ", thr, 1);
		tip_evline(tip_lines[3], "Throttled     : ", thr, 2);
		tip_evline(tip_lines[4], "Overtemp      : ", thr, 3);
		tip_nlines = 5;
	}

	/* the live ARM core clock: full speed reads 1500 MHz; anything
	 * lower while under load means the firmware is scaling right now */

	{
		long khz = tb_ps(65L);			/* PS_HOST_ARM_FREQ_KHZ */

		if (khz > 0)
		{
			char *p = tb_app(tip_lines[tip_nlines], "ARM clock     : ");

			ltoa(khz / 1000L, p, 10);
			strcat(p, " MHz");
			tip_nlines++;
		}
	}
}


static void tip_throttle(void)
{
	if (tip_win != NULL || tb_psid == 0)
		return;

	tip_throttle_lines();
	tip_kind = 2;
	tip_minw = 26;						/* "Under-voltage : since boot" */
	tip_show(&tb_tempr);
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
		static _WORD tslow = 0;			/* temperature refresh divider */
		long temp;
		long used = tb_ps(PS_STAT_CACHE_USED);
		long total = tb_ps(PS_STAT_CACHE_TOTAL);
		long thr = tb_ps(PS_HOST_THROTTLED);

		/* The temperature is displayed at a gentler cadence - every
		 * 4th tick (2 s) - so the last digit does not flicker; -1
		 * in between means "keep showing the current value". The
		 * throttle alert above stays at full tick rate. */

		temp = ((tslow++ & 3) == 0) ? tb_ps(PS_HOST_SOC_TEMP_MC) : -1L;

		/* Flash the temperature cell while the Pi firmware reports an
		 * ACTIVE throttle condition (low nibble: undervoltage, freq
		 * cap, throttling, soft temp limit). thr < 0 = old emulator. */

		{
			_WORD was = tb_throttled;

			tb_throttled = (thr > 0 && (thr & 0x0FL) != 0) ? 1 : 0;
			tb_flash = tb_throttled ? (_WORD) (tb_flash ^ 1) : 0;

			if (tb_throttled || was)	/* flashing, or clearing the red */
				changed = TRUE;
		}

		strcpy(new_cell[0], "PiSTorm");

		if (tb_pistr[0] != 0)
			strcpy(new_cell[1], tb_pistr);

		if (tb_cpustr[0] != 0)
			strcpy(new_cell[3], tb_cpustr);

		if (total > 0)
		{
			strcpy(new_cell[4], "JIT ");
			ltoa((used * 100L) / total, &new_cell[4][4], 10);
			strcat(new_cell[4], "%");
		}

		if (temp > 0)
		{
			p = new_cell[2];
			strcpy(p, "Temp ");
			ltoa(temp / 1000L, p + 5, 10);
			p += strlen(p);
			*p++ = '.';
			*p++ = (char) ('0' + (temp % 1000L) / 100L);
			*p++ = DEGREE_CH;
			*p++ = 'C';
			*p = 0;
		} else
		{
			/* off-cadence tick: keep showing the current value */

			strcpy(new_cell[2], tb_cell[2]);
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

	/* keyboard desk switches repaint the pager on the next tick */

	{
		static _WORD last_desk = -1;

		if (dsk_current() != last_desk)
		{
			last_desk = dsk_current();
			changed = TRUE;
		}
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

	/* The Pi model cell text ("Pi4B 2GB") - board type and RAM size
	 * from the revision word the emulator decodes (indices 74/75);
	 * absent on an old emulator, and the cell simply stays empty. */

	if (tb_psid != 0)
	{
		long model = tb_ps(PS_PI_MODEL);
		long ram = tb_ps(PS_PI_RAM_MB);

		if (model > 0)
		{
			const char *nm;

			switch ((_WORD) model)
			{
			case 0x08:
				nm = "3B";
				break;
			case 0x0d:
				nm = "3B+";
				break;
			case 0x0e:
				nm = "3A+";
				break;
			case 0x11:
				nm = "4B";
				break;
			case 0x13:
				nm = "400";
				break;
			case 0x14:
				nm = "CM4";
				break;
			case 0x17:
				nm = "5";
				break;
			default:
				nm = "?";
				break;
			}

			strcpy(tb_pistr, "Pi");
			strcat(tb_pistr, nm);

			if (ram >= 1024)
			{
				strcat(tb_pistr, " ");
				ltoa(ram / 1024L, tb_pistr + strlen(tb_pistr), 10);
				strcat(tb_pistr, "GB");
			} else if (ram > 0)
			{
				strcat(tb_pistr, " ");
				ltoa(ram, tb_pistr + strlen(tb_pistr), 10);
				strcat(tb_pistr, "MB");
			}
		}
	}

	size = tb_rect;

	tb_window = xw_create(BAR_WIND, &tb_functions, 0, &size, sizeof(TB_WINDOW), NULL, &error);

	if (tb_window != NULL)
	{
		tb_build();
		xw_open(tb_window, &size);		/* first WM_REDRAW paints it */

		/* THE toolbar essential: without this, a click on the bar while
		 * any other window is topped is not a click at all - the AES
		 * swallows it as a WM_TOPPED request (which the bar ignores),
		 * and every bar button plays dead until all windows are closed.
		 * WF_BEVENT marks the window to receive button events WITHOUT
		 * being topped (AES 4 / XaAES / MagiC; harmlessly refused on
		 * older systems, where a single-tasking desktop rarely has the
		 * bar covered anyway). */

#ifndef WF_BEVENT
#define WF_BEVENT 24
#endif
		wind_set(xw_handle(tb_window), WF_BEVENT, 1, 0, 0, 0);

		/* Bespoke XaAES workspaces: mark the bar sticky (opcode 103) so
		 * it stays visible on every desktop instead of being carried
		 * away by a workspace switch. Answers 0 and does nothing on a
		 * stock AES. */

		appl_control(-1, 103, (void *) (long) xw_handle(tb_window));
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


/* ---- hover: menu-bar-like behaviour via MU_M1 -------------------------- */

/*
 * Arm the mouse-rectangle event for the current hover state. Outside
 * the bar, MU_M1 fires when the mouse ENTERS the bar; inside, it fires
 * when the mouse LEAVES the rect it is over (a cell, or a small box
 * around the pointer in dead space, so cell-to-cell moves re-evaluate).
 */

void tb_track(XDEVENT *ev)
{
	if (tb_window == NULL)
		return;

	if (!tb_hovin)
		tb_hovrect = tb_rect;

	ev->ev_mflags |= MU_M1;
	ev->ev_mm1flags = tb_hovin ? 1 : 0;	/* 1 = on leave, 0 = on enter */
	ev->ev_mm1 = tb_hovrect;
}


/*
 * Handle a MU_M1 event: re-evaluate what the mouse is over
 */

void tb_hover(_WORD x, _WORD y)
{
	_WORD tgt = TB_HOV_NONE;

	if (tb_window == NULL)
		return;

	if (x >= tb_rect.g_x && x < tb_rect.g_x + tb_rect.g_w &&
		y >= tb_rect.g_y && y < tb_rect.g_y + tb_rect.g_h)
	{
		tb_hovin = 1;

		if (tb_badge.g_w > 0 &&
			x >= tb_badge.g_x && x < tb_badge.g_x + tb_badge.g_w &&
			y >= tb_badge.g_y && y < tb_badge.g_y + tb_badge.g_h)
		{
			tgt = TB_HOV_BADGE;
			tb_hovrect = tb_badge;
		} else if (tb_clockr.g_w > 0 &&
			x >= tb_clockr.g_x && x < tb_clockr.g_x + tb_clockr.g_w &&
			y >= tb_clockr.g_y && y < tb_clockr.g_y + tb_clockr.g_h)
		{
			tgt = TB_HOV_CLOCK;
			tb_hovrect = tb_clockr;
		} else if (tb_tempr.g_w > 0 &&
			x >= tb_tempr.g_x && x < tb_tempr.g_x + tb_tempr.g_w &&
			y >= tb_tempr.g_y && y < tb_tempr.g_y + tb_tempr.g_h)
		{
			tgt = TB_HOV_TEMP;
			tb_hovrect = tb_tempr;
		} else
		{
			/* dead space: watch a small box around the pointer */

			tb_hovrect.g_x = x - 4;
			tb_hovrect.g_y = y - 4;
			tb_hovrect.g_w = 8;
			tb_hovrect.g_h = 8;
		}
	} else
	{
		tb_hovin = 0;					/* left the bar */
		tb_hovrect = tb_rect;
	}

	if (tgt != tb_hovtgt)
	{
		tb_hovtgt = tgt;
		tb_dwell = 0;
		tip_close();					/* tooltip belongs to the old target */
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

	/* Poll the pointer ourselves every tick: MU_M1 mouse-rectangle
	 * events are only delivered to the application holding the AES
	 * focus, so with another app on top (the video player, typically)
	 * the hover machinery would never hear the mouse at all - popups
	 * dead until TeraDesk is clicked. graf_mkstate() sees the pointer
	 * regardless of focus, and tick granularity is plenty: the dwell
	 * below already needs two ticks. MU_M1 stays armed as well, for
	 * instant response while TeraDesk is the focused app. */

	{
		_WORD mx, my, dummy;

		graf_mkstate(&mx, &my, &dummy, &dummy);
		tb_hover(mx, my);
	}

	/* Hover dwell: after one tick over a target, act - unless a modal
	 * dialog is open */

	if (tb_hovtgt != TB_HOV_NONE && xd_dialogs == NULL)
	{
		if (tb_dwell < 2 && ++tb_dwell == 2)
		{
			/* buttons (PiSTorm, JIT) act on CLICK only - hover opens
			 * nothing; the info cells keep their hover tooltips */

			if (tb_hovtgt == TB_HOV_CLOCK)
				tip_uptime();
			else if (tb_hovtgt == TB_HOV_TEMP)
				tip_throttle();
		}
	}

	/* Live tooltip: while one is open, rebuild its lines every tick and
	 * repaint only when something actually changed - the throttle tip's
	 * ARM clock and ACTIVE flags move in real time, the uptime tip once
	 * a minute. The box keeps its opening size (tip_minw reserves room
	 * for the longest state a line can grow into). */

	if (tip_win != NULL && tip_kind != 0)
	{
		char old[TIP_MAXLINES][TIP_MAXLEN];
		_WORD n = tip_nlines, li;
		bool diff;

		memcpy(old, tip_lines, sizeof(old));

		if (tip_kind == 1)
			tip_uptime_lines();
		else
			tip_throttle_lines();

		diff = (tip_nlines != n);

		for (li = 0; !diff && li < tip_nlines; li++)
			if (strcmp(old[li], tip_lines[li]) != 0)
				diff = TRUE;

		if (diff)
			tip_draw(tip_win, NULL);
	}

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
	tip_close();						/* the tooltip, */
	mn_close();							/* the JIT panel, */
	sm_close();							/* and the system menu too */

	if (tb_window != NULL)
	{
		/* note: xw_closedelete() exists in xwindow.h only - its body
		 * is #if 0'd out in xwindow.c, so close and delete separately */

		xw_close(tb_window);
		xw_delete(tb_window);
		tb_window = NULL;
	}
}
