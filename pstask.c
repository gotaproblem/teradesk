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
#include "dir.h"						/* mn_font: the user's window font */
#include "taskbar.h"
#include "pstask.h"


/*
 * The PiSTorm monitor window (successor of the standalone PSMON.PRG).
 *
 * Shows ST-RAM and TT-RAM usage - totals from the low-memory system
 * variables read under Supexec(), free space from GEMDOS Mxalloc(-1)
 * (with the TOS 1.x largest-block caveat flagged) - and, under
 * FreeMiNT, the running tasks enumerated from u:\proc: entries are
 * "<name>.<pid>", st_size is the process memory use and st_attr is the
 * kernel's run-queue state. The list is sorted biggest-first and
 * scrolled with a real GEM slider. On plain TOS the task section says
 * so and the memory block still works.
 *
 * Sampling happens on the taskbar's 500 ms tick; redraws only when
 * something changed.
 */

#define MN_COLS			44				/* window text columns */
#define MN_MEMROWS		5				/* memory block + blank line */
#define MN_DEFROWS		18				/* default total rows at open */
#define MN_MINPT		9				/* smallest font: 9 points */
#define MN_KIND			(NAME | CLOSER | MOVER | VSLIDE | UPARROW | DNARROW)

#define RAMVALID_MAGIC	0x1357BD13L
#define TTRAM_BASE		0x01000000L
#define GEM_EINVFN		-32L
#define PS_CFG_TTRAM	4L				/* PSCTRL: configured TT-RAM bytes */

#if _MINT_
#define MAX_TASKS		64
#define PROC_NAMEMAX	20

typedef struct
{
	char name[PROC_NAMEMAX];
	long pid;
	long size;
	_WORD attr;
} TASKENT;
#endif

typedef struct
{
	XW_INTVARS;
} MON_WINDOW;

static WINDOW *mn_win = NULL;
static char mn_title[] = " PiSTorm Monitor ";

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

#if _MINT_
static TASKENT tasks[MAX_TASKS];
static _WORD ntasks = 0;
static _WORD have_proc = 1;				/* cleared when u:\proc is absent */
static _WORD top_task = 0;				/* first visible task row */
#endif


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


#if _MINT_

static const char *mn_state(_WORD attr)
{
	switch (attr & 0x27)
	{
	case 0x00:
		return "run";
	case 0x01:
		return "ready";
	case 0x20:
		return "wait";
	case 0x21:
		return "io";
	case 0x22:
		return "zombie";
	case 0x02:
		return "tsr";
	case 0x24:
		return "stop";
	default:
		return "?";
	}
}


/*
 * Enumerate u:\proc: "<name>.<pid>" entries, st_size = memory use,
 * st_attr = run state. Sorted biggest-first (memory is the only
 * per-process figure available, so it is the ranking column).
 */

static void mn_tasks(void)
{
	XDIR *dir;
	XATTR attr;
	_WORD error, i, j;
	char *name;

	ntasks = 0;

	if (!have_proc || !mint)
	{
		have_proc = 0;
		return;
	}

	if ((dir = x_opendir("U:\\PROC", &error)) == NULL)
	{
		have_proc = 0;
		return;
	}

	while (ntasks < MAX_TASKS)
	{
		char *dot;
		long pid = 0;

		if (x_xreaddir(dir, &name, sizeof(VLNAME), &attr) != 0)
			break;

		if (name[0] == 0 || name[0] == '.')
			continue;

		/* the pid is the digits after the last dot of the name */

		dot = strrchr(name, '.');

		if (dot != NULL)
		{
			char *q = dot + 1;

			while (*q >= '0' && *q <= '9')
				pid = pid * 10L + (long) (*q++ - '0');

			*dot = 0;
		}

		/* insertion sort, biggest memory first */

		for (i = 0; i < ntasks; i++)
			if (attr.st_size > tasks[i].size)
				break;

		for (j = ntasks; j > i; j--)
			tasks[j] = tasks[j - 1];

		strsncpy(tasks[i].name, name, PROC_NAMEMAX);
		tasks[i].pid = pid;
		tasks[i].size = attr.st_size;
		tasks[i].attr = attr.st_attr;
		ntasks++;
	}

	x_closedir(dir);
}

#endif /* _MINT_ */


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

#if _MINT_
	mn_tasks();
#endif
}


static long mn_signature(void)
{
	long s = st_free;

	s = s * 31L + tt_free;
#if _MINT_
	s = s * 31L + ntasks;
	s = s * 31L + top_task;

	if (ntasks > 0)
		s = s * 31L + tasks[0].pid + tasks[0].size;
#endif
	return s;
}


/* ---- geometry and slider ----------------------------------------------- */

#if _MINT_

/* Task rows that fit in the current work area */

static _WORD mn_visrows(void)
{
	GRECT work;

	if (mn_win == NULL)
		return 0;

	xw_getwork(mn_win, &work);

	{
		_WORD n = (work.g_h / mn_font.ch) - (MN_MEMROWS + 2);

		return (n < 0) ? 0 : n;
	}
}


static void mn_clamp(void)
{
	_WORD vis = mn_visrows();
	_WORD maxtop = ntasks - vis;

	if (maxtop < 0)
		maxtop = 0;
	if (top_task > maxtop)
		top_task = maxtop;
	if (top_task < 0)
		top_task = 0;
}


static void mn_slider(void)
{
	_WORD vis = mn_visrows();
	_WORD size, pos;

	if (mn_win == NULL)
		return;

	if (ntasks <= vis || ntasks <= 0)
	{
		size = 1000;
		pos = 0;
	} else
	{
		size = (_WORD) ((long) vis * 1000L / (long) ntasks);

		if (size < 1)
			size = 1;

		pos = (_WORD) ((long) top_task * 1000L / (long) (ntasks - vis));
	}

	wind_set(xw_handle(mn_win), WF_VSLSIZE, size, 0, 0, 0);
	wind_set(xw_handle(mn_win), WF_VSLIDE, pos, 0, 0, 0);
}

#endif /* _MINT_ */


/* ---- drawing ----------------------------------------------------------- */

static void mn_text(GRECT *work, _WORD col, _WORD row, char *s)
{
	w_transptext(work->g_x + col * mn_font.cw, work->g_y + row * mn_font.ch, s);
}


/* A simple percent bar drawn with v_bar */

static void mn_bar(GRECT *work, _WORD col, _WORD row, _WORD cols, _WORD p)
{
	_WORD x0 = work->g_x + col * mn_font.cw;
	_WORD y0 = work->g_y + row * mn_font.ch + 2;
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
static char s_taskhdr[] = "NAME          PID     MEM  STATE      ";
static char s_notasks[] = "No task list (not FreeMiNT)           ";
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

static void mn_contents(GRECT *work)
{
	set_txt_default(&mn_font);

	mn_memrow(work, 0, s_stram, st_free, st_total);

	if (tt_total > 0)
	{
		mn_memrow(work, 2, s_ttram, tt_free, tt_total);
	} else
	{
		mn_text(work, 1, 2, s_ttnone);
		mn_text(work, 1, 3, s_blank);
	}

#if _MINT_
	if (have_proc)
	{
		char l[MN_COLS + 4], b[16];
		char *p;
		_WORD vis = mn_visrows();
		_WORD i;

		p = mn_pads(l, "Tasks (", 7);
		ltoa((long) ntasks, p, 10);
		strcat(p, ")    ");
		mn_text(work, 1, MN_MEMROWS, l);
		mn_text(work, 1, MN_MEMROWS + 1, s_taskhdr);

		for (i = 0; i < vis; i++)
		{
			_WORD t = top_task + i;

			if (t >= ntasks)
			{
				/* blank leftover rows so a shrinking list leaves no debris */

				p = mn_pads(l, "", MN_COLS - 4);
				*p = 0;
			} else
			{
				mn_kb(b, tasks[t].size);

				p = mn_pads(l, tasks[t].name, 12);	/* NAME  */
				p = mn_padn(p, tasks[t].pid, 5);	/* PID   */
				{
					_WORD lb = (_WORD) strlen(b);	/* MEM right-aligned in 8 */
					_WORD sp = 8 - lb;

					while (sp-- > 0)
						*p++ = ' ';

					p = mn_pads(p, b, lb);
				}
				p = mn_pads(p, "", 2);
				p = mn_pads(p, mn_state(tasks[t].attr), 8);	/* STATE */
				*p = 0;
			}

			mn_text(work, 1, MN_MEMROWS + 2 + i, l);
		}
	} else
#endif
	{
		mn_text(work, 1, MN_MEMROWS, s_notasks);
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


#if _MINT_

static void mn_arrowed(WINDOW *w, _WORD arrows)
{
	_WORD vis = mn_visrows();

	(void) w;

	switch (arrows)
	{
	case WA_UPLINE:
		top_task -= 1;
		break;
	case WA_DNLINE:
		top_task += 1;
		break;
	case WA_UPPAGE:
		top_task -= vis;
		break;
	case WA_DNPAGE:
		top_task += vis;
		break;
	default:
		break;
	}

	mn_clamp();
	mn_slider();
	mn_draw(NULL);
}


static void mn_vslid(WINDOW *w, _WORD newpos)
{
	_WORD vis = mn_visrows();

	(void) w;

	if (ntasks > vis)
		top_task = (_WORD) (((long) newpos * (long) (ntasks - vis) + 500L) / 1000L);
	else
		top_task = 0;

	mn_clamp();
	mn_slider();
	mn_draw(NULL);
}

#endif /* _MINT_ */


static WD_FUNC mn_functions = {
	0L,									/* handle keypress */
	0L,									/* handle button */
	mn_redraw,							/* redraw */
	mn_topped,							/* topped */
	xw_nop1,							/* bottomed */
	mn_topped,							/* newtop */
	mn_closed,							/* closed */
	0L,									/* fulled */
#if _MINT_
	mn_arrowed,							/* arrowed */
#else
	xw_nop2,							/* arrowed */
#endif
	0L,									/* hslid */
#if _MINT_
	mn_vslid,							/* vslid */
#else
	xw_nop2,							/* vslid */
#endif
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
	wrk.g_w = MN_COLS * mn_font.cw;
	wrk.g_h = MN_DEFROWS * mn_font.ch;

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

#if _MINT_
	top_task = 0;
	mn_clamp();
	mn_slider();
#endif
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

#if _MINT_
	mn_clamp();
	mn_slider();
#endif

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
