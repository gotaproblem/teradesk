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
#include "lists.h"						/* LSTYPE, needed by slider.h */
#include "slider.h"						/* SLIDER, needed by icon.h */
#include "xfilesys.h"					/* XFILE/XDIR/XATTR */
#include "config.h"
#include "font.h"
#include "screen.h"
#include "window.h"
#include "icon.h"						/* dsk_sweep() */
#include "dir.h"						/* mn_font: the user's window font */
#include "taskbar.h"
#include "pstask.h"


/*
 * Taskbar v2: the two PiSTorm panels.
 *
 * 1. The JIT panel (mn_*), opened by clicking the taskbar's JIT button
 *    and closed by its closer or another click of the button: the live
 *    engine figures (speed, hit rate, idle, cache + flushes, compile
 *    and SMC-invalidation rates) followed by the ST-RAM and TT-RAM
 *    gauges inherited from the retired PiSTorm monitor. Totals come
 *    from the low-memory system variables under Supexec(), free space
 *    from GEMDOS Mxalloc(-1).
 *
 * 2. The system-tasks menu (sm_*), opened by the PiSTorm button:
 *    shortcuts to XaAES's task manager and recover (bespoke opcodes
 *    104/105) and the full-screen sweep.
 *
 * Both sample/refresh on the taskbar's 500 ms tick; redraws only when
 * something changed. The old u:\proc task list is gone - the XaAES
 * task manager does that job properly.
 */

#define MN_COLS			44				/* window text columns */
#define MN_DEFROWS		11				/* total content rows */
#define MN_MINPT		9				/* smallest font: 9 points */
#define MN_KIND			(NAME | CLOSER | MOVER)

/* Margins between the window frame and the content, so text never
 * touches the edges; scale with the font like everything else here */

#define MN_HPAD			(mn_font.cw)
#define MN_VPAD			(mn_font.ch / 2)

#define RAMVALID_MAGIC	0x1357BD13L
#define TTRAM_BASE		0x01000000L
#define GEM_EINVFN		-32L
#define PS_CFG_TTRAM	4L				/* PSCTRL: configured TT-RAM bytes */

typedef struct
{
	XW_INTVARS;
} MON_WINDOW;

static WINDOW *mn_win = NULL;
static char mn_title[] = " PiSTorm JIT ";

static XDFONT mn_font;					/* the window font, min. MN_MINPT points */
static _WORD mn_req_id = -1;			/* last requested font id/size */
static _WORD mn_req_pt = -1;
static _WORD mn_used_ch = 0;			/* metrics the open window was sized for */
static _WORD mn_used_cw = 0;


/*
 * Select the monitor font: the user's directory-window font (set through
 * TeraDesk's window-font dialog, saved in the config), clamped to at
 * least MN_MINPT points so the display stays legible.
 */

static void mn_setfont(void)
{
	_WORD want = dir_font.size;

	if (want < MN_MINPT)
		want = MN_MINPT;

	if (dir_font.id != mn_req_id || want != mn_req_pt)
	{
		mn_req_id = dir_font.id;
		mn_req_pt = want;

		fnt_setfont(dir_font.id, want, &mn_font);
		mn_font.colour = dir_font.colour;
		mn_font.effects = dir_font.effects;
	}
}

static long sv_phystop, sv_ramtop, sv_ramvalid;
static long st_total, st_free, tt_total, tt_free;
static _WORD have_mxalloc = -1;			/* -1 = not probed yet */
static long mn_sig = -1;				/* last drawn content signature */

/* JIT figures sampled from PSCTRL; -1 = index unknown (old emulator) */

static long jt_khz = -1, jt_hit = -1, jt_idle = -1;
static long jt_cpct = -1, jt_flush = -1, jt_comp = -1, jt_smc = -1;

#define PS_CACHE_USED	39L
#define PS_CACHE_TOTAL	40L
#define PS_COMPILES		41L
#define PS_EFF_KHZ		71L
#define PS_HIT_X10		72L
#define PS_IDLE_X10		73L
#define PS_FLUSH_TOTAL	76L
#define PS_SMC_INV		77L


/* ---- formatting helpers (no sprintf - see stringf.c) ------------------- */

/* Copy s into d blank-padded/truncated to exactly w characters */

static char *mn_pads(char *d, const char *s, _WORD w)
{
	_WORD i;

	for (i = 0; i < w; i++)
		*d++ = (*s != 0) ? *s++ : ' ';

	return d;
}


/* Write v right-aligned in exactly w characters */

static char *mn_padn(char *d, long v, _WORD w)
{
	char t[16];
	_WORD l;

	ltoa(v, t, 10);
	l = (_WORD) strlen(t);

	while (w > l)
	{
		*d++ = ' ';
		w--;
	}

	return mn_pads(d, t, l);
}


/* "123K" / "12M" style size */

static void mn_kb(char *b, long bytes)
{
	if (bytes >= 1048576L)
	{
		ltoa(bytes / 1048576L, b, 10);
		strcat(b, "M");
	} else
	{
		ltoa(bytes / 1024L, b, 10);
		strcat(b, "K");
	}
}


static _WORD mn_pct(long used, long total)
{
	long p;

	if (total <= 0 || used <= 0)
		return 0;
	if (used >= total)
		return 100;

	if (used < 21000000L)
		p = (used * 100L) / total;
	else
		p = (used / 1024L) * 100L / (total / 1024L);

	return (p < 0) ? 0 : ((p > 100) ? 100 : (_WORD) p);
}


/* ---- sampling ---------------------------------------------------------- */

/* Runs in supervisor mode through Supexec() */

static long mn_sysvars(void)
{
	sv_phystop = *(volatile long *) 0x42EL;
	sv_ramtop = *(volatile long *) 0x5A4L;
	sv_ramvalid = *(volatile long *) 0x5A8L;
	return 0;
}





static void mn_sample(void)
{
	long v;

	if (have_mxalloc < 0)
		have_mxalloc = (Mxalloc(-1L, 0) != GEM_EINVFN) ? 1 : 0;

	Supexec(mn_sysvars);

	st_total = sv_phystop;

	if (sv_ramvalid == RAMVALID_MAGIC && sv_ramtop > TTRAM_BASE)
		tt_total = sv_ramtop - TTRAM_BASE;
	else
		tt_total = 0;

	/* If TOS did not validate ramtop, believe the emulator's configured
	 * TT-RAM size - seeing "0% used of 128M" flags a TOS that is not
	 * initialising Fast RAM, which is worth knowing. */

	if (tt_total == 0)
	{
		v = tb_psget(PS_CFG_TTRAM);

		if (v > 0)
			tt_total = v;
	}

	if (have_mxalloc)
	{
		v = Mxalloc(-1L, 0);
		st_free = (v > 0) ? v : 0;
		v = (tt_total > 0) ? Mxalloc(-1L, 1) : 0;
		tt_free = (v > 0) ? v : 0;
	} else
	{
		v = Malloc(-1L);				/* TOS 1.x: largest free block only */
		st_free = (v > 0) ? v : 0;
		tt_free = 0;
	}

	/* the JIT engine figures for the panel's top block */

	jt_khz = tb_psget(PS_EFF_KHZ);
	jt_hit = tb_psget(PS_HIT_X10);
	jt_idle = tb_psget(PS_IDLE_X10);

	{
		long u = tb_psget(PS_CACHE_USED);
		long t = tb_psget(PS_CACHE_TOTAL);

		jt_cpct = (t > 0 && u >= 0) ? (u * 100L) / t : -1L;
	}

	jt_flush = tb_psget(PS_FLUSH_TOTAL);
	jt_comp = tb_psget(PS_COMPILES);	/* per 500 ms window */
	jt_smc = tb_psget(PS_SMC_INV);		/* per 500 ms window */
}


static long mn_signature(void)
{
	long s = st_free;

	s = s * 31L + tt_free;
	s = s * 31L + jt_khz;
	s = s * 31L + jt_hit;
	s = s * 31L + jt_idle;
	s = s * 31L + jt_cpct;
	s = s * 31L + jt_flush;
	s = s * 31L + jt_comp;
	s = s * 31L + jt_smc;

	return s;
}


/* ---- drawing ----------------------------------------------------------- */

static void mn_text(GRECT *work, _WORD col, _WORD row, char *s)
{
	w_transptext(work->g_x + MN_HPAD + col * mn_font.cw,
				 work->g_y + MN_VPAD + row * mn_font.ch, s);
}


/* A simple percent bar drawn with v_bar */

static void mn_bar(GRECT *work, _WORD col, _WORD row, _WORD cols, _WORD p)
{
	_WORD x0 = work->g_x + MN_HPAD + col * mn_font.cw;
	_WORD y0 = work->g_y + MN_VPAD + row * mn_font.ch + 2;
	_WORD w = cols * mn_font.cw;
	_WORD h = mn_font.ch - 4;
	_WORD fill;
	_WORD f[10];
	GRECT r;

	r.g_x = x0;
	r.g_y = y0;
	r.g_w = w;
	r.g_h = h;
	clr_object(&r, G_WHITE, -1);

	/* frame */

	vsl_color(vdi_handle, G_BLACK);
	f[0] = x0;
	f[1] = y0;
	f[2] = x0 + w - 1;
	f[3] = y0;
	f[4] = x0 + w - 1;
	f[5] = y0 + h - 1;
	f[6] = x0;
	f[7] = y0 + h - 1;
	f[8] = x0;
	f[9] = y0;
	v_pline(vdi_handle, 5, f);

	fill = (_WORD) (((long) (w - 4) * p) / 100L);

	if (fill > 0)
	{
		r.g_x = x0 + 2;
		r.g_y = y0 + 2;
		r.g_w = fill;
		r.g_h = h - 4;
		clr_object(&r, G_BLACK, -1);
	}
}


/* Static label texts (w_transptext wants non-const char pointers) */

static char s_stram[] = "ST RAM";
static char s_ttram[] = "TT RAM";
static char s_ttnone[] = "TT RAM  (none)                        ";
static char s_blank[] = "                                      ";
static char s_freeof[] = " free of ";


/* One "  <free> free of <total>" line, blank-padded to the full width */

static void mn_memline(GRECT *work, _WORD row, long freeb, long totalb)
{
	char l[MN_COLS + 4], a[16], b[16];
	char *p;

	mn_kb(a, freeb);
	mn_kb(b, totalb);

	p = mn_pads(l, "", 2);
	p = mn_pads(p, a, (_WORD) strlen(a));
	p = mn_pads(p, s_freeof, (_WORD) strlen(s_freeof));
	p = mn_pads(p, b, (_WORD) strlen(b));
	p = mn_pads(p, "", (_WORD) ((MN_COLS - 4) - (_WORD) (p - l)));
	*p = 0;

	mn_text(work, 1, row, l);
}


/* One memory gauge row: label, bar, percentage */

static void mn_memrow(GRECT *work, _WORD row, char *label, long freeb, long totalb)
{
	char l[16];
	char *p;
	_WORD pc = mn_pct(totalb - freeb, totalb);

	mn_text(work, 1, row, label);
	mn_bar(work, 9, row, 10, pc);

	p = mn_padn(l, (long) pc, 4);
	*p++ = '%';
	*p = 0;
	mn_text(work, 20, row, l);

	mn_memline(work, row + 1, freeb, totalb);
}


/* Draw the complete contents into the (already clipped) work area */

/* One JIT line: 9-char label + value, padded to the full width */

static void mn_jline(GRECT *work, _WORD row, const char *label, const char *val)
{
	char l[MN_COLS + 4];
	char *p;

	p = mn_pads(l, label, 9);
	p = mn_pads(p, val, (_WORD) strlen(val));
	p = mn_pads(p, "", (_WORD) ((MN_COLS - 4) - (_WORD) (p - l)));
	*p = 0;

	mn_text(work, 1, row, l);
}


/* Tenths-of-a-percent ("96.4%"), or n/a on an old emulator */

static void mn_x10(char *d, long x10)
{
	char *p;

	if (x10 < 0)
	{
		strcpy(d, "n/a");
		return;
	}

	ltoa(x10 / 10L, d, 10);
	p = d + strlen(d);
	*p++ = '.';
	*p++ = (char) ('0' + (_WORD) (x10 % 10L));
	*p++ = '%';
	*p = 0;
}


/* Draw the complete contents into the (already clipped) work area */

static void mn_contents(GRECT *work)
{
	char v[MN_COLS];
	char *p;

	set_txt_default(&mn_font);

	/* the engine block */

	if (jt_khz > 0)
	{
		ltoa(jt_khz / 1000L, v, 10);
		p = v + strlen(v);
		p = mn_pads(p, " MHz  (", 7);
		ltoa(jt_khz / 8000L, p, 10);
		strcat(p, "x ST)");
	} else
		strcpy(v, "n/a");
	mn_jline(work, 0, "Speed  : ", v);

	mn_x10(v, jt_hit);
	mn_jline(work, 1, "JIT hit: ", v);

	mn_x10(v, jt_idle);
	mn_jline(work, 2, "Idle   : ", v);

	if (jt_cpct >= 0)
	{
		ltoa(jt_cpct, v, 10);
		strcat(v, "% used");

		if (jt_flush >= 0)
		{
			p = v + strlen(v);
			p = mn_pads(p, ", ", 2);
			ltoa(jt_flush, p, 10);
			strcat(p, " flushes");
		}
	} else
		strcpy(v, "n/a");
	mn_jline(work, 3, "Cache  : ", v);

	if (jt_comp >= 0)
	{
		ltoa(jt_comp * 2L, v, 10);		/* 500 ms window -> per second */
		strcat(v, " blk/s");
	} else
		strcpy(v, "n/a");
	mn_jline(work, 4, "Compile: ", v);

	if (jt_smc >= 0)
	{
		ltoa(jt_smc * 2L, v, 10);
		strcat(v, " /s");
	} else
		strcpy(v, "n/a");
	mn_jline(work, 5, "SMC inv: ", v);

	/* the memory gauges, exactly as the retired monitor drew them */

	mn_memrow(work, 7, s_stram, st_free, st_total);

	if (tt_total > 0)
	{
		mn_memrow(work, 9, s_ttram, tt_free, tt_total);
	} else
	{
		mn_text(work, 1, 9, s_ttnone);
		mn_text(work, 1, 10, s_blank);
	}
}


/*
 * Redraw over the AES rectangle list, clipped to area (NULL = all)
 */

static void mn_draw(GRECT *area)
{
	GRECT r1, r2, in, work;

	if (mn_win == NULL)
		return;

	xw_getwork(mn_win, &work);

	r1 = (area != NULL) ? *area : work;

	xd_begupdate();
	xd_mouse_off();

	xw_getfirst(mn_win, &r2);

	while (r2.g_w != 0 && r2.g_h != 0)
	{
		if (xd_rcintersect(&r1, &r2, &in))
		{
			xd_clip_on(&in);
			clr_object(&in, G_WHITE, -1);
			mn_contents(&work);
			xd_clip_off();
		}

		xw_getnext(mn_win, &r2);
	}

	xd_mouse_on();
	xd_endupdate();
}


/* ---- window handlers ---------------------------------------------------- */

static void mn_redraw(WINDOW *w, GRECT *area)
{
	(void) w;
	mn_draw(area);
}


static void mn_topped(WINDOW *w)
{
	xw_set_topbot(w, WF_TOP);
}


static void mn_closed(WINDOW *w, _WORD mode)
{
	(void) w;
	(void) mode;
	mn_close();
}


static void mn_moved(WINDOW *w, GRECT *newpos)
{
	xw_setsize(w, newpos);
	mn_draw(NULL);
}





static WD_FUNC mn_functions = {
	0L,									/* handle keypress */
	0L,									/* handle button */
	mn_redraw,							/* redraw */
	mn_topped,							/* topped */
	xw_nop1,							/* bottomed */
	mn_topped,							/* newtop */
	mn_closed,							/* closed */
	0L,									/* fulled */
	xw_nop2,							/* arrowed */
	0L,									/* hslid */
	xw_nop2,							/* vslid */
	0L,									/* sized */
	mn_moved,							/* moved */
	0L,									/* hndlmenu */
	0L,									/* top */
	0L,									/* iconify */
	0L									/* uniconify */
};


/* ---- public entry points ------------------------------------------------ */

/* Outer size for MN_COLS x MN_DEFROWS of the current font, clamped */

static void mn_calcsize(GRECT *size)
{
	GRECT wrk;

	wrk.g_x = xd_desk.g_x;
	wrk.g_y = xd_desk.g_y;
	wrk.g_w = MN_COLS * mn_font.cw + 2 * MN_HPAD;
	wrk.g_h = MN_DEFROWS * mn_font.ch + 2 * MN_VPAD;

	wind_calc_grect(WC_BORDER, MN_KIND, &wrk, size);

	if (size->g_w > xd_desk.g_w)
		size->g_w = xd_desk.g_w;
	if (size->g_h > xd_desk.g_h)
		size->g_h = xd_desk.g_h;
}


void mn_open(void)
{
	GRECT size;
	int error;

	if (mn_win != NULL)
	{
		xw_set_topbot(mn_win, WF_TOP);
		return;
	}

	mn_setfont();
	mn_sample();
	mn_sig = -1;

	mn_calcsize(&size);

	/* Home position: bottom-left of the desktop, directly above the
	 * PiSTorm button on the taskbar it was opened from */

	size.g_x = xd_desk.g_x;
	size.g_y = xd_desk.g_y + xd_desk.g_h - size.g_h;

	mn_used_cw = mn_font.cw;
	mn_used_ch = mn_font.ch;

	mn_win = xw_create(MON_WIND, &mn_functions, MN_KIND, &size, sizeof(MON_WINDOW), NULL, &error);

	if (mn_win == NULL)
	{
		xform_error(error);
		return;
	}

	wind_set_str(xw_handle(mn_win), WF_NAME, mn_title);
	xw_open(mn_win, &size);

	/* Bespoke XaAES workspaces: the monitor is a system tool - sticky
	 * on every desktop (opcode 103; harmlessly refused elsewhere). */

	appl_control(-1, 103, (void *) (long) xw_handle(mn_win));
}


/*
 * Resize the open window to the (changed) font, keeping its x position
 * and its bottom edge where they are
 */

static void mn_fit(void)
{
	GRECT cur, size;

	xw_getsize(mn_win, &cur);
	mn_calcsize(&size);

	size.g_x = cur.g_x;
	size.g_y = cur.g_y + cur.g_h - size.g_h;

	if (size.g_y < xd_desk.g_y)
		size.g_y = xd_desk.g_y;

	mn_used_cw = mn_font.cw;
	mn_used_ch = mn_font.ch;

	xw_setsize(mn_win, &size);
	mn_draw(NULL);
}


void mn_tick(void)
{
	long sig;

	if (mn_win == NULL)
		return;

	/* Follow window-font changes; resize the window to match */

	mn_setfont();

	if (mn_font.cw != mn_used_cw || mn_font.ch != mn_used_ch)
		mn_fit();

	mn_sample();

	sig = mn_signature();

	if (sig != mn_sig)
	{
		mn_sig = sig;
		mn_draw(NULL);
	}
}


void mn_close(void)
{
	if (mn_win != NULL)
	{
		xw_close(mn_win);
		xw_delete(mn_win);
		mn_win = NULL;
	}
}


void mn_toggle(void)
{
	if (mn_win != NULL)
		mn_close();
	else
		mn_open();
}


/* ---- the PiSTorm system-tasks menu -------------------------------------- */

#define SM_KIND			(NAME | CLOSER | MOVER)
#define SM_NITEMS		4
#define SM_COLS			16

typedef struct
{
	XW_INTVARS;
} SM_WINDOW;

static WINDOW *sm_win = NULL;
static char sm_title[] = " PiSTorm System ";
static char sm_titlebuf[28];			/* " APJ-OS v0.1.1 " when known */

static char *sm_items[SM_NITEMS] = {
	"Task Manager",
	"Recover GUI",
	"Sweep screen",
	"Set wallpaper"
};


/*
 * Choose a wallpaper for the CURRENT desktop via the file selector,
 * then ask stretch-vs-fit; an image applies live and is remembered per
 * desk. The alert also offers to remove the current wallpaper.
 */

static void sm_wallpaper(void)
{
	char *path;
	VLNAME name;
	VLNAME start;
	_WORD b;

	/* [Choose] a new image, [Remove] the current one, or [Cancel] */

	b = form_alert(1, "[2][Desktop wallpaper|for this desk][Choose|Remove|Cancel]");

	if (b == 3)
		return;

	if (b == 2)
	{
		dsk_wall_set("", 0);			/* remove */
		return;
	}

	/* start the selector in the current wallpaper's folder, or the
	 * bespoke default S:\BG (the hostfs share the installer populates) */

	if (options.wallp[0] != 0)
		strsncpy(start, options.wallp, sizeof(start));
	else
		strcpy(start, "S:\\BG\\*.JPG");

	name[0] = 0;

	path = xfileselector(start, name, "Desktop wallpaper");

	if (path == NULL)
		return;

	if (name[0] != 0)
	{
		/* stretch to fill, or fit keeping aspect */

		b = form_alert(2, "[1][Scale the wallpaper|to the screen how?][Stretch|Fit|Cancel]");

		if (b != 3)
			dsk_wall_set(path, (b == 2) ? 1 : 0);
	}

	free(path);
}


static void sm_contents(GRECT *work)
{
	char l[SM_COLS + 4];
	char *p;
	_WORD i;

	set_txt_default(&mn_font);

	for (i = 0; i < SM_NITEMS; i++)
	{
		p = mn_pads(l, sm_items[i], (_WORD) strlen(sm_items[i]));
		p = mn_pads(p, "", (_WORD) (SM_COLS - (_WORD) (p - l)));
		*p = 0;

		mn_text(work, 0, i, l);
	}
}


static void sm_draw(GRECT *area)
{
	GRECT r1, r2, in, work;

	if (sm_win == NULL)
		return;

	xw_getwork(sm_win, &work);

	r1 = (area != NULL) ? *area : work;

	xd_begupdate();
	xd_mouse_off();

	xw_getfirst(sm_win, &r2);

	while (r2.g_w != 0 && r2.g_h != 0)
	{
		if (xd_rcintersect(&r1, &r2, &in))
		{
			xd_clip_on(&in);
			clr_object(&in, G_WHITE, -1);
			sm_contents(&work);
			xd_clip_off();
		}

		xw_getnext(sm_win, &r2);
	}

	xd_mouse_on();
	xd_endupdate();
}


static void sm_redraw(WINDOW *w, GRECT *area)
{
	(void) w;
	sm_draw(area);
}


static void sm_topped(WINDOW *w)
{
	xw_set_topbot(w, WF_TOP);
}


static void sm_closed(WINDOW *w, _WORD mode)
{
	(void) w;
	(void) mode;
	sm_close();
}


static void sm_moved(WINDOW *w, GRECT *newpos)
{
	xw_setsize(w, newpos);
	sm_draw(NULL);
}


/* An item was clicked: close the menu first (so a full-screen action
 * like the sweep does not repaint underneath an open menu), then act */

static void sm_button(WINDOW *w, _WORD x, _WORD y, _WORD n, _WORD bstate, _WORD kstate)
{
	GRECT work;
	_WORD row;

	(void) w;
	(void) x;
	(void) n;
	(void) bstate;
	(void) kstate;

	xw_getwork(sm_win, &work);
	row = (_WORD) ((y - work.g_y - MN_VPAD) / mn_font.ch);

	sm_close();

	switch (row)
	{
	case 0:
		appl_control(-1, 104, NULL);	/* XaAES task manager (bespoke) */
		break;
	case 1:
		appl_control(-1, 105, NULL);	/* XaAES recover (bespoke) */
		break;
	case 2:
		dsk_sweep();					/* full-screen redraw broadcast */
		break;
	case 3:
		sm_wallpaper();					/* set this desk's wallpaper */
		break;
	default:
		break;
	}
}


static WD_FUNC sm_functions = {
	0L,									/* handle keypress */
	sm_button,							/* handle button */
	sm_redraw,							/* redraw */
	sm_topped,							/* topped */
	xw_nop1,							/* bottomed */
	sm_topped,							/* newtop */
	sm_closed,							/* closed */
	0L,									/* fulled */
	xw_nop2,							/* arrowed */
	0L,									/* hslid */
	xw_nop2,							/* vslid */
	0L,									/* sized */
	sm_moved,							/* moved */
	0L,									/* hndlmenu */
	0L,									/* top */
	0L,									/* iconify */
	0L									/* uniconify */
};


void sm_close(void)
{
	if (sm_win != NULL)
	{
		xw_close(sm_win);
		xw_delete(sm_win);
		sm_win = NULL;
	}
}


void sm_toggle(void)
{
	GRECT wrk, size;
	int error;

	if (sm_win != NULL)
	{
		sm_close();
		return;
	}

	mn_setfont();

	wrk.g_x = xd_desk.g_x;
	wrk.g_y = xd_desk.g_y;
	wrk.g_w = (SM_COLS + 2) * mn_font.cw + 2 * MN_HPAD;
	wrk.g_h = SM_NITEMS * mn_font.ch + 2 * MN_VPAD;

	wind_calc_grect(WC_BORDER, SM_KIND, &wrk, &size);

	/* directly above the PiSTorm button that opened it */

	size.g_x = xd_desk.g_x;
	size.g_y = xd_desk.g_y + xd_desk.g_h - size.g_h;

	sm_win = xw_create(SM_WIND, &sm_functions, SM_KIND, &size, sizeof(SM_WINDOW), NULL, &error);

	if (sm_win == NULL)
	{
		xform_error(error);
		return;
	}

	{
		/* title shows the APJ-OS release when the version file is
		 * present, else the plain system-menu name */

		char *ver = tb_apjtitle();

		if (ver != NULL)
		{
			strcpy(sm_titlebuf, " ");
			strcat(sm_titlebuf, ver);
			strcat(sm_titlebuf, " ");
			wind_set_str(xw_handle(sm_win), WF_NAME, sm_titlebuf);
		} else
		{
			wind_set_str(xw_handle(sm_win), WF_NAME, sm_title);
		}
	}

	xw_open(sm_win, &size);

	/* one-click items even when the menu is not the top window (same
	 * WF_BEVENT trick as the bar itself) */

#ifndef WF_BEVENT
#define WF_BEVENT 24
#endif
	wind_set(xw_handle(sm_win), WF_BEVENT, 1, 0, 0, 0);

	/* sticky on all workspaces, like the bar it belongs to */

	appl_control(-1, 103, (void *) (long) xw_handle(sm_win));
}
