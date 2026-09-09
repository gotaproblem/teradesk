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

#ifndef __TASKBAR_H__
#define __TASKBAR_H__

/*
 * Bespoke Desktop taskbar: a borderless window along the bottom of the
 * screen showing a clock and, when running on PiSTorm-Atari-JIT, live
 * emulator data (CPU temperature, JIT cache use, Pi load) obtained
 * through the PSCTRL NatFeat.
 */

extern _WORD tb_height;					/* reserved strip height; 0 = no bar */

void tb_dbg(const char *s);					/* TIP_DEBUG builds only */
void tb_reserve(void);					/* shrink xd_desk before dsk_init() */
void tb_apply(void);					/* open bar / return space, per options.tbar */
void tb_tick(void);						/* 500 ms timer callback from evntloop() */
void tb_close(void);					/* close the bar window at shutdown */
long tb_psget(long index);				/* PSCTRL PS_GETINT; -1 if absent */
void tb_popup_font(void);				/* set VDI text to the BAR font */
void tb_popup_metrics(_WORD *cw, _WORD *ch);	/* bar font cell metrics */
void ws_startup_push(void);				/* re-apply saved XaAES settings */
void st_close(void);					/* close the settings page */
void tb_psfx(long dir);					/* desk-slide: 1 = old exits left */
char *tb_apjtitle(void);				/* "APJ-OS v0.1.1" or NULL */

/* Hover (mouse-rectangle) support; XDEVENT from xdialog.h */

void tb_track(XDEVENT *ev);				/* arm MU_M1 for the current hover state */
void tb_hover(_WORD x, _WORD y);		/* handle a MU_M1 event at (x,y) */

#endif
