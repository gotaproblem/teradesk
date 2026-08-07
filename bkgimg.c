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
#include "xfilesys.h"					/* XFILE, needed by window.h/icon.h */
#include "config.h"
#include "font.h"						/* XDFONT, needed by window.h */
#include "window.h"						/* ITMTYPE, needed by icon.h */
#include "icon.h"
#include "bkgimg.h"


/*
 * The Bespoke Desktop wallpaper.
 *
 * The 'wall' key in teradesk.inf names a PNG or JPG file - either on a
 * HOSTFS drive (S:\PIX\WALL.JPG or /s/pix/wall.jpg) or as a plain host
 * path on the Pi (/home/pistorm/wall.png). At startup the file is
 * decoded, scaled to the desktop work area and converted to the exact
 * screen pixel format by the emulator (PSIMG NatFeat, host-side stb
 * libraries), into a guest RAM buffer allocated here (TT-RAM preferred:
 * a 1920x1080x32 image is ~8 MB).
 *
 * Drawing: the desktop root object becomes a userdef whose draw routine
 * simply vro_cpyfm()s the requested clip rectangle out of the buffer -
 * the fVDI backend executes the blit host-side, so redraws are fast.
 * 'walm' selects scaling: 0 = stretch, 1 = fit (letterbox, black bars
 * are part of the delivered image so the blit always covers the area).
 *
 * Requires a hicolour/truecolour screen (16/32 bpp fVDI modes). Without
 * the PSIMG feature, on failure, or at lower depths, the normal
 * pattern/colour background is kept.
 */

/* PSIMG NatFeat interface - keep in sync with the emulator's psimg.h */

#define PSIMG_LOAD			1L
#define PSIMG_MODE_STRETCH	0
#define PSIMG_MODE_FIT		1

static void *bk_buf = NULL;				/* the wallpaper bitmap, guest RAM */
static _WORD bk_w = 0;					/* its size in pixels */
static _WORD bk_h = 0;
static XUSERBLK bk_xub;					/* userdef data for the desktop root */


/*
 * Userdef draw routine for the desktop root: blit the clip rectangle
 * out of the wallpaper buffer. Object and buffer are the same size.
 */

static _WORD _CDECL bk_draw(PARMBLK *pb)
{
	MFDB src, dst;
	_WORD pxy[8];
	_WORD sx, sy;

	if (bk_buf == NULL || pb->pb_wc <= 0 || pb->pb_hc <= 0)
		return 0;

	sx = pb->pb_xc - pb->pb_x;
	sy = pb->pb_yc - pb->pb_y;

	pxy[0] = sx;
	pxy[1] = sy;
	pxy[2] = sx + pb->pb_wc - 1;
	pxy[3] = sy + pb->pb_hc - 1;
	pxy[4] = pb->pb_xc;
	pxy[5] = pb->pb_yc;
	pxy[6] = pb->pb_xc + pb->pb_wc - 1;
	pxy[7] = pb->pb_yc + pb->pb_hc - 1;

	src.fd_addr = bk_buf;
	src.fd_w = bk_w;
	src.fd_h = bk_h;
	src.fd_wdwidth = (bk_w + 15) / 16;
	src.fd_stand = 0;
	src.fd_nplanes = xd_nplanes;
	src.fd_r1 = 0;
	src.fd_r2 = 0;
	src.fd_r3 = 0;

	dst.fd_addr = NULL;					/* the screen */

	vro_cpyfm(vdi_handle, S_ONLY, pxy, &src, &dst);

	return 0;
}


/*
 * Make the desktop root object draw the wallpaper. Returns TRUE when
 * the wallpaper is active (also called from set_dsk_background(), so a
 * pattern/colour change from the dialog does not undo the wallpaper).
 */

bool bk_install(void)
{
	OBJECT *rt;

	if (bk_buf == NULL || desktop == NULL)
		return FALSE;

	rt = &desktop[0];

	/* If the root already is a userdef, first restore the plain box */

	if ((rt->ob_type & 0x00FF) == G_USERDEF)
	{
		XUSERBLK *cur = (XUSERBLK *) rt->ob_spec.userblk;

		rt->ob_type = cur->ob_type;
		rt->ob_flags = cur->ob_flags;
		rt->ob_spec = cur->ob_spec;
	}

	xd_xuserdef(rt, &bk_xub, bk_draw);

	return TRUE;
}


/*
 * Drop the current wallpaper: restore the desktop root to a plain box
 * (so a wallpaper-less desk shows its pattern/colour again) and free
 * the bitmap buffer. Used when switching desktops - call while the OLD
 * desktop tree is still current, before the pointers are swapped, then
 * bk_init() again for the new desk's wallpaper.
 */

void bk_drop(void)
{
	if (desktop != NULL)
	{
		OBJECT *rt = &desktop[0];

		if ((rt->ob_type & 0x00FF) == G_USERDEF)
		{
			XUSERBLK *cur = (XUSERBLK *) rt->ob_spec.userblk;

			rt->ob_type = cur->ob_type;
			rt->ob_flags = cur->ob_flags;
			rt->ob_spec = cur->ob_spec;
		}
	}

	if (bk_buf != NULL)
	{
		Mfree(bk_buf);
		bk_buf = NULL;
		bk_w = 0;
		bk_h = 0;
	}
}


/*
 * Load the wallpaper named in the configuration through the PSIMG
 * NatFeat. Call after the configuration has been loaded and after the
 * taskbar has reserved its strip, so the desktop root has its final
 * size; regenerates the desktop when successful.
 */

void bk_init(void)
{
	struct nf_ops *ops;
	long id, size, result, mem;
	_WORD w, h, mode;

	if (options.wallp[0] == 0)
		return;

	/* Only in the fVDI hicolour/truecolour modes */

	if (xd_nplanes != 16 && xd_nplanes != 32)
		return;

	if ((ops = nf_init()) == NULL || (id = nf_get_id("PSIMG")) == 0)
		return;

	w = desktop[0].ob_width;
	h = desktop[0].ob_height;
	size = (long) w * (long) h * (long) (xd_nplanes / 8);

	/* Prefer TT-RAM; fall back to any RAM. Give up quietly if scarce */

#if _MINT_
	if (mint || tos_version >= 0x206)
#else
	if (tos_version >= 0x206)
#endif
		mem = (long) Mxalloc(size, 3);
	else
		mem = (long) Malloc(size);

	if (mem <= 0)
		return;

	bk_buf = (void *) mem;

	mode = (options.wallm == 0) ? PSIMG_MODE_STRETCH : PSIMG_MODE_FIT;

	result = ops->call(id | PSIMG_LOAD,
					   (long) (unsigned long) virt_to_phys(options.wallp),
					   (long) (unsigned long) virt_to_phys(bk_buf),
					   (long) w, (long) h, (long) xd_nplanes, (long) mode);

	if (result != 0)
	{
		Mfree(bk_buf);
		bk_buf = NULL;
		return;
	}

	bk_w = w;
	bk_h = h;

	if (bk_install())
		regen_desktop(desktop);
}
