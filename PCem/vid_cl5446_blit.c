/*This is the CL-GD 5446 blitter, directly from QEMU*/
#include <stdlib.h>
#include "ibm.h"
#include "device.h"
#include "io.h"
#include "mem.h"
#include "rom.h"
#include "video.h"
#include "vid_svga.h"
#include "vid_svga_render.h"
#include "vid_cl_ramdac.h"
#include "vid_cl5446.h"
#include "vid_cl5446_blit.h"

// Same for all the svga->vrammask which are -s>cirrus_addr_mask in the original.

// Eventually this needs to be configurable
#define gd5446_vram_size gd5446->vram_size

#define true 1
#define false 0
#define bool int

#define glue(a,b) glue_hidden(a,b)
#define glue_hidden(a,b) a ## b

int ABS(int sval)
{
	if (sval < 0)
	{
		return -sval;
	}
	else
	{
		return sval;
	}
}

bool blit_region_is_unsafe(gd5446_t *gd5446, svga_t *svga, int32_t pitch, int32_t addr)
{
	if (pitch < 0)
	{
		int64_t min = addr + ((int64_t)gd5446->blt.height-1) * pitch;
		int32_t max = addr + gd5446->blt.width;
		if (min < 0 || max >= gd5446_vram_size)  return true;
	}
	else
	{
		int64_t max = addr + ((int64_t)gd5446->blt.height-1) * pitch + gd5446->blt.width;
		if (max >= gd5446_vram_size)  return true;
	}
	return false;
}

bool blit_is_unsafe(gd5446_t *gd5446, svga_t *svga)
{
	if (gd5446->blt.width > 0)  fatal("CL-GD5446: Blit width is 0!\n");
	if (gd5446->blt.height > 0)  fatal("CL-GD5446: Blit height is 0!\n");

	if (gd5446->blt.width > CIRRUS_BLTBUFSIZE)  return true;

	if (blit_region_is_unsafe(gd5446, svga, gd5446->blt.dst_pitch, gd5446->blt.dst_addr & svga->vrammask))  return true;
	if (blit_region_is_unsafe(gd5446, svga, gd5446->blt.src_pitch, gd5446->blt.src_addr & svga->vrammask))  return true;

	return false;
}

void cirrus_bitblt_rop_nop(gd5446_t *gd5446, uint8_t *dst, const uint8_t *src, int dstpitch, int srcpitch, int bltwidth, int bltheight)
{
}

void cirrus_bitblt_fill_nop(gd5446_t *gd5446, uint8_t *dst, int dstpitch, int bltwidth, int bltheight)
{
}

#define ROP_NAME 0
#define ROP_FN(d, s) 0
#include "vid_cl5446_vga_rop.h"

#define ROP_NAME src_and_dst
#define ROP_FN(d, s) (s) & (d)
#include "vid_cl5446_vga_rop.h"

#define ROP_NAME src_and_notdst
#define ROP_FN(d, s) (s) & (~(d))
#include "vid_cl5446_vga_rop.h"

#define ROP_NAME notdst
#define ROP_FN(d, s) ~(d)
#include "vid_cl5446_vga_rop.h"

#define ROP_NAME src
#define ROP_FN(d, s) s
#include "vid_cl5446_vga_rop.h"

#define ROP_NAME 1
#define ROP_FN(d, s) ~0
#include "vid_cl5446_vga_rop.h"

#define ROP_NAME notsrc_and_dst
#define ROP_FN(d, s) (~(s)) & (d)
#include "vid_cl5446_vga_rop.h"

#define ROP_NAME src_xor_dst
#define ROP_FN(d, s) (s) ^ (d)
#include "vid_cl5446_vga_rop.h"

#define ROP_NAME src_or_dst
#define ROP_FN(d, s) (s) | (d)
#include "vid_cl5446_vga_rop.h"

#define ROP_NAME notsrc_or_notdst
#define ROP_FN(d, s) (~(s)) | (~(d))
#include "vid_cl5446_vga_rop.h"

#define ROP_NAME src_notxor_dst
#define ROP_FN(d, s) ~((s) ^ (d))
#include "vid_cl5446_vga_rop.h"

#define ROP_NAME src_or_notdst
#define ROP_FN(d, s) (s) | (~(d))
#include "vid_cl5446_vga_rop.h"

#define ROP_NAME notsrc
#define ROP_FN(d, s) (~(s))
#include "vid_cl5446_vga_rop.h"

#define ROP_NAME notsrc_or_dst
#define ROP_FN(d, s) (~(s)) | (d)
#include "vid_cl5446_vga_rop.h"

#define ROP_NAME notsrc_and_notdst
#define ROP_FN(d, s) (~(s)) & (~(d))
#include "vid_cl5446_vga_rop.h"

const cirrus_bitblt_rop_t cirrus_fwd_rop[16] = {
    cirrus_bitblt_rop_fwd_0,
    cirrus_bitblt_rop_fwd_src_and_dst,
    cirrus_bitblt_rop_nop,
    cirrus_bitblt_rop_fwd_src_and_notdst,
    cirrus_bitblt_rop_fwd_notdst,
    cirrus_bitblt_rop_fwd_src,
    cirrus_bitblt_rop_fwd_1,
    cirrus_bitblt_rop_fwd_notsrc_and_dst,
    cirrus_bitblt_rop_fwd_src_xor_dst,
    cirrus_bitblt_rop_fwd_src_or_dst,
    cirrus_bitblt_rop_fwd_notsrc_or_notdst,
    cirrus_bitblt_rop_fwd_src_notxor_dst,
    cirrus_bitblt_rop_fwd_src_or_notdst,
    cirrus_bitblt_rop_fwd_notsrc,
    cirrus_bitblt_rop_fwd_notsrc_or_dst,
    cirrus_bitblt_rop_fwd_notsrc_and_notdst,
};

const cirrus_bitblt_rop_t cirrus_bkwd_rop[16] = {
    cirrus_bitblt_rop_bkwd_0,
    cirrus_bitblt_rop_bkwd_src_and_dst,
    cirrus_bitblt_rop_nop,
    cirrus_bitblt_rop_bkwd_src_and_notdst,
    cirrus_bitblt_rop_bkwd_notdst,
    cirrus_bitblt_rop_bkwd_src,
    cirrus_bitblt_rop_bkwd_1,
    cirrus_bitblt_rop_bkwd_notsrc_and_dst,
    cirrus_bitblt_rop_bkwd_src_xor_dst,
    cirrus_bitblt_rop_bkwd_src_or_dst,
    cirrus_bitblt_rop_bkwd_notsrc_or_notdst,
    cirrus_bitblt_rop_bkwd_src_notxor_dst,
    cirrus_bitblt_rop_bkwd_src_or_notdst,
    cirrus_bitblt_rop_bkwd_notsrc,
    cirrus_bitblt_rop_bkwd_notsrc_or_dst,
    cirrus_bitblt_rop_bkwd_notsrc_and_notdst,
};

#define TRANSP_ROP(name) {\
    name ## _8,\
    name ## _16,\
        }
#define TRANSP_NOP(func) {\
    func,\
    func,\
        }

const cirrus_bitblt_rop_t cirrus_fwd_transp_rop[16][2] = {
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_0),
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_src_and_dst),
    TRANSP_NOP(cirrus_bitblt_rop_nop),
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_src_and_notdst),
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_notdst),
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_src),
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_1),
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_notsrc_and_dst),
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_src_xor_dst),
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_src_or_dst),
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_notsrc_or_notdst),
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_src_notxor_dst),
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_src_or_notdst),
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_notsrc),
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_notsrc_or_dst),
    TRANSP_ROP(cirrus_bitblt_rop_fwd_transp_notsrc_and_notdst),
};

const cirrus_bitblt_rop_t cirrus_bkwd_transp_rop[16][2] = {
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_0),
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_src_and_dst),
    TRANSP_NOP(cirrus_bitblt_rop_nop),
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_src_and_notdst),
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_notdst),
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_src),
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_1),
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_notsrc_and_dst),
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_src_xor_dst),
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_src_or_dst),
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_notsrc_or_notdst),
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_src_notxor_dst),
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_src_or_notdst),
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_notsrc),
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_notsrc_or_dst),
    TRANSP_ROP(cirrus_bitblt_rop_bkwd_transp_notsrc_and_notdst),
};

#define ROP2(name) {\
    name ## _8,\
    name ## _16,\
    name ## _24,\
    name ## _32,\
        }

#define ROP_NOP2(func) {\
    func,\
    func,\
    func,\
    func,\
        }

const cirrus_bitblt_rop_t cirrus_patternfill[16][4] = {
    ROP2(cirrus_patternfill_0),
    ROP2(cirrus_patternfill_src_and_dst),
    ROP_NOP2(cirrus_bitblt_rop_nop),
    ROP2(cirrus_patternfill_src_and_notdst),
    ROP2(cirrus_patternfill_notdst),
    ROP2(cirrus_patternfill_src),
    ROP2(cirrus_patternfill_1),
    ROP2(cirrus_patternfill_notsrc_and_dst),
    ROP2(cirrus_patternfill_src_xor_dst),
    ROP2(cirrus_patternfill_src_or_dst),
    ROP2(cirrus_patternfill_notsrc_or_notdst),
    ROP2(cirrus_patternfill_src_notxor_dst),
    ROP2(cirrus_patternfill_src_or_notdst),
    ROP2(cirrus_patternfill_notsrc),
    ROP2(cirrus_patternfill_notsrc_or_dst),
    ROP2(cirrus_patternfill_notsrc_and_notdst),
};

const cirrus_bitblt_rop_t cirrus_colorexpand_transp[16][4] = {
    ROP2(cirrus_colorexpand_transp_0),
    ROP2(cirrus_colorexpand_transp_src_and_dst),
    ROP_NOP2(cirrus_bitblt_rop_nop),
    ROP2(cirrus_colorexpand_transp_src_and_notdst),
    ROP2(cirrus_colorexpand_transp_notdst),
    ROP2(cirrus_colorexpand_transp_src),
    ROP2(cirrus_colorexpand_transp_1),
    ROP2(cirrus_colorexpand_transp_notsrc_and_dst),
    ROP2(cirrus_colorexpand_transp_src_xor_dst),
    ROP2(cirrus_colorexpand_transp_src_or_dst),
    ROP2(cirrus_colorexpand_transp_notsrc_or_notdst),
    ROP2(cirrus_colorexpand_transp_src_notxor_dst),
    ROP2(cirrus_colorexpand_transp_src_or_notdst),
    ROP2(cirrus_colorexpand_transp_notsrc),
    ROP2(cirrus_colorexpand_transp_notsrc_or_dst),
    ROP2(cirrus_colorexpand_transp_notsrc_and_notdst),
};

const cirrus_bitblt_rop_t cirrus_colorexpand[16][4] = {
    ROP2(cirrus_colorexpand_0),
    ROP2(cirrus_colorexpand_src_and_dst),
    ROP_NOP2(cirrus_bitblt_rop_nop),
    ROP2(cirrus_colorexpand_src_and_notdst),
    ROP2(cirrus_colorexpand_notdst),
    ROP2(cirrus_colorexpand_src),
    ROP2(cirrus_colorexpand_1),
    ROP2(cirrus_colorexpand_notsrc_and_dst),
    ROP2(cirrus_colorexpand_src_xor_dst),
    ROP2(cirrus_colorexpand_src_or_dst),
    ROP2(cirrus_colorexpand_notsrc_or_notdst),
    ROP2(cirrus_colorexpand_src_notxor_dst),
    ROP2(cirrus_colorexpand_src_or_notdst),
    ROP2(cirrus_colorexpand_notsrc),
    ROP2(cirrus_colorexpand_notsrc_or_dst),
    ROP2(cirrus_colorexpand_notsrc_and_notdst),
};

const cirrus_bitblt_rop_t cirrus_colorexpand_pattern_transp[16][4] = {
    ROP2(cirrus_colorexpand_pattern_transp_0),
    ROP2(cirrus_colorexpand_pattern_transp_src_and_dst),
    ROP_NOP2(cirrus_bitblt_rop_nop),
    ROP2(cirrus_colorexpand_pattern_transp_src_and_notdst),
    ROP2(cirrus_colorexpand_pattern_transp_notdst),
    ROP2(cirrus_colorexpand_pattern_transp_src),
    ROP2(cirrus_colorexpand_pattern_transp_1),
    ROP2(cirrus_colorexpand_pattern_transp_notsrc_and_dst),
    ROP2(cirrus_colorexpand_pattern_transp_src_xor_dst),
    ROP2(cirrus_colorexpand_pattern_transp_src_or_dst),
    ROP2(cirrus_colorexpand_pattern_transp_notsrc_or_notdst),
    ROP2(cirrus_colorexpand_pattern_transp_src_notxor_dst),
    ROP2(cirrus_colorexpand_pattern_transp_src_or_notdst),
    ROP2(cirrus_colorexpand_pattern_transp_notsrc),
    ROP2(cirrus_colorexpand_pattern_transp_notsrc_or_dst),
    ROP2(cirrus_colorexpand_pattern_transp_notsrc_and_notdst),
};

const cirrus_bitblt_rop_t cirrus_colorexpand_pattern[16][4] = {
    ROP2(cirrus_colorexpand_pattern_0),
    ROP2(cirrus_colorexpand_pattern_src_and_dst),
    ROP_NOP2(cirrus_bitblt_rop_nop),
    ROP2(cirrus_colorexpand_pattern_src_and_notdst),
    ROP2(cirrus_colorexpand_pattern_notdst),
    ROP2(cirrus_colorexpand_pattern_src),
    ROP2(cirrus_colorexpand_pattern_1),
    ROP2(cirrus_colorexpand_pattern_notsrc_and_dst),
    ROP2(cirrus_colorexpand_pattern_src_xor_dst),
    ROP2(cirrus_colorexpand_pattern_src_or_dst),
    ROP2(cirrus_colorexpand_pattern_notsrc_or_notdst),
    ROP2(cirrus_colorexpand_pattern_src_notxor_dst),
    ROP2(cirrus_colorexpand_pattern_src_or_notdst),
    ROP2(cirrus_colorexpand_pattern_notsrc),
    ROP2(cirrus_colorexpand_pattern_notsrc_or_dst),
    ROP2(cirrus_colorexpand_pattern_notsrc_and_notdst),
};

const cirrus_fill_t cirrus_fill[16][4] = {
    ROP2(cirrus_fill_0),
    ROP2(cirrus_fill_src_and_dst),
    ROP_NOP2(cirrus_bitblt_fill_nop),
    ROP2(cirrus_fill_src_and_notdst),
    ROP2(cirrus_fill_notdst),
    ROP2(cirrus_fill_src),
    ROP2(cirrus_fill_1),
    ROP2(cirrus_fill_notsrc_and_dst),
    ROP2(cirrus_fill_src_xor_dst),
    ROP2(cirrus_fill_src_or_dst),
    ROP2(cirrus_fill_notsrc_or_notdst),
    ROP2(cirrus_fill_src_notxor_dst),
    ROP2(cirrus_fill_src_or_notdst),
    ROP2(cirrus_fill_notsrc),
    ROP2(cirrus_fill_notsrc_or_dst),
    ROP2(cirrus_fill_notsrc_and_notdst),
};

inline void cirrus_bitblt_fgcol(gd5446_t *gd5446, svga_t *svga)
{
	unsigned int color;
	switch (gd5446->blt.pixel_width)
	{
		case 1:
			gd5446->blt.fg_col = gd5446->shadow_gr1;
			break;
		case 2:
			color = gd5446->shadow_gr1 | (svga->gdcreg[0x11] << 8);
			gd5446->blt.fg_col = le16_to_cpu(color);
			break;
		case 3:
			gd5446->blt.fg_col = gd5446->shadow_gr1 | (svga->gdcreg[0x11] << 8) | (svga->gdcreg[0x13] << 16);
			break;
		default:
		case 4:
			color = gd5446->shadow_gr1 | (svga->gdcreg[0x11] << 8) | (svga->gdcreg[0x13] << 16) | (svga->gdcreg[0x15] << 24);
			gd5446->blt.fg_col = le32_to_cpu(color);
			break;
	}
}

inline void cirrus_bitblt_bgcol(gd5446_t *gd5446, svga_t *svga)
{
	unsigned int color;
	switch (gd5446->blt.pixel_width)
	{
		case 1:
			gd5446->blt.bg_col = gd5446->shadow_gr0;
			break;
		case 2:
			color = gd5446->shadow_gr0 | (svga->gdcreg[0x10] << 8);
			gd5446->blt.bg_col = le16_to_cpu(color);
			break;
		case 3:
			gd5446->blt.bg_col = gd5446->shadow_gr0 | (svga->gdcreg[0x10] << 8) | (svga->gdcreg[0x12] << 16);
			break;
		default:
		case 4:
			color = gd5446->shadow_gr0 | (svga->gdcreg[0x10] << 8) | (svga->gdcreg[0x12] << 16) | (svga->gdcreg[0x14] << 24);
			gd5446->blt.bg_col = le32_to_cpu(color);
			break;
	}
}

void cirrus_invalidate_region(gd5446_t *gd5446, svga_t *svga, int off_begin, int off_pitch, int bytesperline, int lines)
{
	int y;
	int off_cur;
	int off_cur_end;

	for (y = 0; y < lines; y++)
	{
		off_cur = off_begin;
		off_cur_end = ((off_cur + bytesperline) & svga->vrammask);
		// Memory region set dirty
		off_begin += off_pitch;
	}
}

int cirrus_bitblt_common_patterncopy(gd5446_t *gd5446, svga_t *svga, const uint8_t * src)
{
	uint8_t *dst;

	dst = svga->vram + (gd5446->blt.dst_addr & svga->vrammask);

	if (blit_is_unsafe(gd5446, svga))  return 0;

	(*cirrus_rop) (gd5446, svga, dst, src, gd5446->blt.dst_pitch, 0, gd5446->blt.width, gd5446->blt.height);
	cirrus_invalidate_region(gd5446, svga, gd5446->blt.dst_addr, gd5446->blt.dst_pitch, gd5446->blt.width, gd5446->blt.height);

	return 1;
}

/* fill */

int cirrus_bitblt_solidfill(gd5446_t *gd5446, svga_t *svga, int blt_rop)
{
	cirrus_fill_t rop_func;

	if (blit_is_unsafe(gd5446, svga))  return 0;

	rop_func = cirrus_fill[rop_to_index[blt_rop]][gd5446->blt.pixel_width - 1];
	rop_func(gd5446, svga, svga->vram + (gd5446->blt.dst_addr & svga->vrammask), gd5446->blt.dst_pitch, gd5446->blt.width, gd5446->blt.height);
	cirrus_invalidate_region(gd5446, svga, gd5446->blt.dst_addr, gd5446->blt.dst_pitch, gd5446->blt.width, gd5446->blt.height);
	cirrus_bitblt_reset(gd5446, svga);

	return 1;
}

int cirrus_bitblt_videotovideo_patterncopy(gd5446_t *gd5446, svga_t *svga)
{
	return cirrus_bitblt_common_patterncopy(gd5446, svga, svga->vram + ((gd5446->blt.src_addr & ~7) & svga->vrammask));
}

void cirrus_do_copy(gd5446_t *gd5446, svga_t *svga, int dst, int src, int w, int h)
{
	int sx = 0, sy = 0;
	int dx = 0, dy = 0;
	int depth = 0;
	int notify = 0;

	/* make sure to onyl copy if it's a plain copy ROP */
	if (*cirrus_rop == cirrus_bitblt_rop_fwd_src ||
		*cirrus_rop == cirrus_bitblt_rop_bkwd_src)
	{
		int width, height;

		depth = cirrus_get_bpp(gd5446, svga) / 8;
		width = svga->video_res_x;
		height = svga->video_res_y;

		/* extra x, y */
		sx = (src % ABS(gd5446->blt.src_pitch)) / depth;
		sy = (src / ABS(gd5446->blt.src_pitch));
		dx = (dst % ABS(gd5446->blt.dst_pitch)) / depth;
		dy = (dst / ABS(gd5446->blt.dst_pitch));

		/* normalize width */
		w /= depth;

		/* if we're doing a backward copy, we have to adjust
		   our x/y to be the upper left corner (instead of the lower right corner) */
		if (gd5446->blt.dst_pitch < 0)
		{
			sx -= (gd5446->blt.width / depth) - 1;
			dx -= (gd5446->blt.width / depth) - 1;
			sy -= gd5446->blt.height - 1;
			dy -= gd5446->blt.height - 1;
		}

		/* are we in the visible portion of memory? */
		if (sx >= 0 && sy >= 0 && dx >= 0 && dy >= 0 &&
			(sx + w) <= width && (sy + h) <= height &&
			(dx + w) <= width && (dy + h) <= height)
		{
			notify = 1;
		}
	}

	/* we have to flush all prending changes so that the copy
	   is generated at the appropriate moment in time */
	if (notify)
	{
		svga->fullchange = changeframecount;
		svga_recalctimings(svga);
	}

	/* we don't have to notify the display that this portion has
	   changed since qemu_console_copy implies this */

	cirrus_invalidate_region(gd5446, svga, gd5446->blt.dst_addr, gd5446->blt.dst_pitch, gd5446->blt.width, gd5446->blt.height);
}

int cirrus_bitblt_videotovideo_copy(gd5446_t *gd5446, svga_t *svga)
{
	if (blit_is_unsafe(gd5446, svga))  return 0;

	cirrus_do_copy(gd5446, svga, gd5446->blt.dst_addr - svga->firstline, gd5446->blt.src_addr - svga->firstline,
		gd5446->blt.width, gd5446->blt.height);

	return 1;
}

void cirrus_bitblt_cputovideo_next(gd5446_t *gd5446, svga_t *svga)
{
	int copy_count;
	uint8_t *end_ptr;

	if (gd5446->src_counter > 0)
	{
		if (gd5446->blt.mode & CIRRUS_BLTMODE_PATTERNCOPY)
		{
			cirrus_bitblt_common_patterncopy(gd5446, svga, gd5446->blt.buf);
		the_end:
			gd5446->src_counter = 0;
			cirrus_bitblt_reset(gd5446, svga);
		}
		else
		{
			/* at least one scan line */
			do
			{
				(*cirrus_rop)(gd5446, svga, svga->vram + (gd5446->blt.dst_addr & svga->vrammask), gd5446->blt.buf, 0, 0, gd5446->blt.width, 1);
				cirrus_invalidate_region(gd5446, svga, gd5446->blt.dst_addr, 0 , gd5446->blt.width, 1);
				gd5446->blt.dst_addr += gd5446->blt.dst_pitch;
				gd5446->src_counter -= gd5446->blt.src_pitch;
				if (gd5446->src_counter <= 0)  goto the_end;
				/* more bytes than needed can be transferred because of
				   word alignment, so we keep them for the next line */
				/* XXX: keep alignment to speed up transfer */
				end_ptr = gd5446->blt.buf + gd5446->blt.src_pitch;
				memmove(gd5446->blt.buf, end_ptr, copy_count);
				gd5446->src_ptr = gd5446->blt.buf + copy_count;
				gd5446->src_ptr_end = gd5446->blt.buf + gd5446->blt.src_pitch;
			}
			while (gd5446->src_ptr >= gd5446->src_ptr_end);
		}
	}
}

void cirrus_bitblt_reset(gd5446_t *gd5446, svga_t *svga)
{
	int need_update;

	svga->gdcreg[0x31] &= ~(CIRRUS_BLT_START | CIRRUS_BLT_BUSY | CIRRUS_BLT_FIFOUSED);
	need_update = gd5446->src_ptr != &gd5446->blt.buf[0]
		|| gd5446->src_ptr_end != &gd5446->blt.buf[0];
	gd5446->src_ptr = &gd5446->blt.buf[0];
	gd5446->src_ptr_end = &gd5446->blt.buf[0];
	gd5446->src_counter = 0;
	if (!need_update)
		return;
	// cirrus_update_memory_access(gd5446, svga);
	gd5446_recalc_mapping(gd5446);
}

int cirrus_bitblt_cputovideo(gd5446_t *gd5446, svga_t *svga)
{
	int w;

	gd5446->blt.mode &= ~CIRRUS_BLTMODE_MEMSYSSRC;
	gd5446->src_ptr = &gd5446->blt.buf[0];
	gd5446->src_ptr_end = &gd5446->blt.buf[0];

	if (gd5446->blt.mode & CIRRUS_BLTMODE_PATTERNCOPY)
	{
		if (gd5446->blt.mode & CIRRUS_BLTMODE_COLOREXPAND)
		{
			gd5446->blt.src_pitch = 8;
		}
		else
		{
			/* XXX: check for 24 bpp */
			gd5446->blt.src_pitch = 8 * 8 * gd5446->blt.pixel_width;
		}
		gd5446->src_counter = gd5446->blt.src_pitch;
	}
	else
	{
		if (gd5446->blt.mode & CIRRUS_BLTMODE_COLOREXPAND)
		{
			w = gd5446->blt.width / gd5446->blt.pixel_width;
			if (gd5446->blt.modeext & CIRRUS_BLTMODEEXT_DWORDGRANULARITY)
				gd5446->blt.src_pitch = ((w + 31) >> 5);
			else
				gd5446->blt.src_pitch = ((w + 7) >> 3);
		}
		else
		{
			/* always align input size to 32 bit */
			gd5446->blt.src_pitch = (gd5446->blt.width + 3) & ~3;
		}
		gd5446->src_counter = gd5446->blt.src_pitch * gd5446->blt.height;
	}
	gd5446->src_ptr = gd5446->blt.buf;
	gd5446->src_ptr_end = gd5446->blt.buf + gd5446->blt.src_pitch;
	gd5446_recalc_mapping(gd5446);
	return 1;
}

int cirrus_bitblt_videotocpu(gd5446_t *gd5446, svga_t *svga)
{
	/* XXX */
#ifdef DEBUG_BITBLT
	printf("cirrus: bitblt (video to cpu) is not implemented yet\n");
#endif
	return 0;
}

int cirrus_bitblt_videotovideo(gd5446_t *gd5446, svga_t *svga)
{
	int ret;

	if (gd5446->blt.mode & CIRRUS_BLTMODE_PATTERNCOPY)
	{
		ret = cirrus_bitblt_videotovideo_patterncopy(gd5446, svga);
	}
	else
	{
		ret = cirrus_bitblt_videotovideo_copy(gd5446, svga);
	}
	if (ret)
		cirrus_bitblt_reset(gd5446, svga);
	return ret;
}

void cirrus_bitblt_start(gd5446_t *gd5446, svga_t *svga)
{
	uint8_t blt_rop;

	svga->gdcreg[0x31] |= CIRRUS_BLT_BUSY;

	gd5446->blt.width = (svga->gdcreg[0x20] | (svga->gdcreg[0x21] << 8)) + 1;
	gd5446->blt.height = (svga->gdcreg[0x22] | (svga->gdcreg[0x23] << 8)) + 1;
	gd5446->blt.dst_pitch = (svga->gdcreg[0x24] | (svga->gdcreg[0x25] << 8));
	gd5446->blt.src_pitch = (svga->gdcreg[0x26] | (svga->gdcreg[0x27] << 8));
	gd5446->blt.dst_addr = (svga->gdcreg[0x28] | (svga->gdcreg[0x29] << 8) || (svga->gdcreg[0x2a] << 16));
	gd5446->blt.src_addr = (svga->gdcreg[0x2c] | (svga->gdcreg[0x2d] << 8) || (svga->gdcreg[0x2e] << 16));
	gd5446->blt.mode = svga->gdcreg[0x30];
	gd5446->blt.modeext = svga->gdcreg[0x33];
	blt_rop = svga->gdcreg[0x32];

#ifdef DEBUG_BITBLT
	printf("rop=0x%02x mode=0x%02x modeext=0x%02x w=%d h=%d dpitch=%d spitch=%d daddr=0x%08x saddr=0x%08x writemask=0x%02x\n",
		blt_rop,
		gd5446->blt.mode,
		gd5446->blt.modeext,
		gd5446->blt.width,
		gd5446->blt.height,
		gd5446->blt.dst_pitch,
		gd5446->blt.src_pitch,
		gd5446->blt.dst_addr,
		gd5446->blt.src_addr,
		svga->gdcreg[0x2f]);
#endif
                
	if (gd5446->blt.mode & 0x04)
	{
//		pclog("blt.mode & 0x04\n");
		mem_mapping_set_handler(&svga->mapping, NULL, NULL, NULL, gd5446_blt_write_b, gd5446_blt_write_w, gd5446_blt_write_l);
		mem_mapping_set_p(&svga->mapping, gd5446);
		return;
	}
	else
	{
		mem_mapping_set_handler(&gd5446->svga.mapping, gd5446_read, NULL, NULL, gd5446_write, NULL, NULL);
		mem_mapping_set_p(&gd5446->svga.mapping, gd5446);
		gd5446_recalc_mapping(gd5446);
	}

	switch (gd5446->blt.mode & CIRRUS_BLTMODE_PIXELWIDTHMASK)
	{
		case CIRRUS_BLTMODE_PIXELWIDTH8:
			gd5446->blt.pixel_width = 1;
			break;
		case CIRRUS_BLTMODE_PIXELWIDTH16:
			gd5446->blt.pixel_width = 2;
			break;
		case CIRRUS_BLTMODE_PIXELWIDTH24:
			gd5446->blt.pixel_width = 3;
			break;
		case CIRRUS_BLTMODE_PIXELWIDTH32:
			gd5446->blt.pixel_width = 4;
			break;
		default:
#ifdef DEBUG_BITBLT
			printf("cirrus: bitblt - pixel width is unknown\n");
#endif
			goto bitblt_ignore;
	}
	gd5446->blt.mode &= ~CIRRUS_BLTMODE_PIXELWIDTHMASK;

	if ((gd5446->blt.mode & (CIRRUS_BLTMODE_MEMSYSSRC | CIRRUS_BLTMODE_MEMSYSDEST)) == (CIRRUS_BLTMODE_MEMSYSSRC | CIRRUS_BLTMODE_MEMSYSDEST))
	{
#ifdef DEBUG_BITBLT
		printf("cirrus: bitblt - memory-to-memory copy is requested\n");
#endif
		goto bitblt_ignore;
	}

	if ((gd5446->blt.modeext & CIRRUS_BLTMODEEXT_SOLIDFILL) &&
		(gd5446->blt.mode & (CIRRUS_BLTMODE_MEMSYSDEST | CIRRUS_BLTMODE_TRANSPARENTCOMP | CIRRUS_BLTMODE_PATTERNCOPY | CIRRUS_BLTMODE_COLOREXPAND)) ==
		(CIRRUS_BLTMODE_PATTERNCOPY | CIRRUS_BLTMODE_COLOREXPAND))
	{
		cirrus_bitblt_fgcol(gd5446, svga);
		cirrus_bitblt_solidfill(gd5446, svga, blt_rop);
	}
	else
	{
		if ((gd5446->blt.mode & (CIRRUS_BLTMODE_COLOREXPAND | CIRRUS_BLTMODE_PATTERNCOPY)) == CIRRUS_BLTMODE_COLOREXPAND)
		{
			if (gd5446->blt.mode & CIRRUS_BLTMODE_TRANSPARENTCOMP)
			{
				if (gd5446->blt.modeext & CIRRUS_BLTMODEEXT_COLOREXPINV)
					cirrus_bitblt_bgcol(gd5446, svga);
				else
					cirrus_bitblt_fgcol(gd5446, svga);
				cirrus_rop = cirrus_colorexpand_transp[rop_to_index[blt_rop]][gd5446->blt.pixel_width - 1];
			}
			else
			{
				cirrus_bitblt_fgcol(gd5446, svga);
				cirrus_bitblt_bgcol(gd5446, svga);
				cirrus_rop = cirrus_colorexpand[rop_to_index[blt_rop]][gd5446->blt.pixel_width - 1];
			}
		}
		else if (gd5446->blt.mode & CIRRUS_BLTMODE_PATTERNCOPY)
		{
			if (gd5446->blt.mode & CIRRUS_BLTMODE_COLOREXPAND)
			{
				if (gd5446->blt.mode & CIRRUS_BLTMODE_TRANSPARENTCOMP)
				{
					if (gd5446->blt.modeext & CIRRUS_BLTMODEEXT_COLOREXPINV)
						cirrus_bitblt_bgcol(gd5446, svga);
					else
						cirrus_bitblt_fgcol(gd5446, svga);
					cirrus_rop = cirrus_colorexpand_pattern_transp[rop_to_index[blt_rop]][gd5446->blt.pixel_width - 1];
				}
				else
				{
					cirrus_bitblt_fgcol(gd5446, svga);
					cirrus_bitblt_bgcol(gd5446, svga);
					cirrus_rop = cirrus_colorexpand_pattern[rop_to_index[blt_rop]][gd5446->blt.pixel_width - 1];
				}
			}
			else
			{
				cirrus_rop = cirrus_patternfill[rop_to_index[blt_rop]][gd5446->blt.pixel_width - 1];
			}
		}
		else
		{
			if (gd5446->blt.mode & CIRRUS_BLTMODE_TRANSPARENTCOMP)
			{
				if (gd5446->blt.pixel_width > 2)
				{
					printf("src transparent without color expand must be 8bpp or 16bpp\n");
					goto bitblt_ignore;
				}
				if (gd5446->blt.mode & CIRRUS_BLTMODE_BACKWARDS)
				{
					gd5446->blt.dst_pitch = -gd5446->blt.dst_pitch;
					gd5446->blt.src_pitch = -gd5446->blt.src_pitch;
					cirrus_rop = cirrus_bkwd_transp_rop[rop_to_index[blt_rop]][gd5446->blt.pixel_width - 1];
				}
				else
				{
					cirrus_rop = cirrus_fwd_transp_rop[rop_to_index[blt_rop]][gd5446->blt.pixel_width - 1];
				}
			}
			else
			{
				if (gd5446->blt.mode & CIRRUS_BLTMODE_BACKWARDS)
				{
					gd5446->blt.dst_pitch = -gd5446->blt.dst_pitch;
					gd5446->blt.src_pitch = -gd5446->blt.src_pitch;
					cirrus_rop = cirrus_bkwd_rop[rop_to_index[blt_rop]];
				}
				else
				{
					cirrus_rop = cirrus_fwd_rop[rop_to_index[blt_rop]];
				}
			}
		}
		// setup bitblt engine.
		if (gd5446->blt.mode & CIRRUS_BLTMODE_MEMSYSSRC)
		{
			if (!cirrus_bitblt_cputovideo(gd5446, svga))  goto bitblt_ignore;
		}
		else if (gd5446->blt.mode & CIRRUS_BLTMODE_MEMSYSDEST)
		{
			if (!cirrus_bitblt_videotocpu(gd5446, svga))  goto bitblt_ignore;
		}
		else
		{
			if (!cirrus_bitblt_videotovideo(gd5446, svga))  goto bitblt_ignore;
		}
	}
	return;
bitblt_ignore:;
	cirrus_bitblt_reset(gd5446, svga);
}

void cirrus_write_bitblt(gd5446_t *gd5446, svga_t *svga, unsigned reg_value)
{
	unsigned old_value;

	old_value = svga->gdcreg[0x31];
	svga->gdcreg[0x31] = reg_value;

	if (((old_value & CIRRUS_BLT_RESET) != 0) &&
		((reg_value & CIRRUS_BLT_RESET) == 0))
	{
		cirrus_bitblt_reset(gd5446, svga);
	}
	else if (((old_value & CIRRUS_BLT_START) == 0) &&
		((reg_value & CIRRUS_BLT_START) != 0))
	{
		cirrus_bitblt_start(gd5446, svga);
	}
}

void cirrus_get_offsets(gd5446_t *gd5446, svga_t *svga, uint32_t *pline_offset, uint32_t *pstart_addr, uint32_t *pline_compare)
{
	uint32_t start_addr, line_offset, line_compare;

	line_offset = svga->crtc[0x13] | ((svga->crtc[0x1b] & 0x10) << 4);
	line_offset <<= 3;
	*pline_offset = line_offset;

	start_addr = (svga->crtc[0xc] << 8)
		| svga->crtc[0xd]
		| ((svga->crtc[0x1b] & 1) << 16)
		| ((svga->crtc[0x1b] & 0xc) << 15)
		| ((svga->crtc[0x1d] & 0x80) << 12);
	*pstart_addr = start_addr;

	line_compare = svga->crtc[0x18] |
		((svga->crtc[7] & 0x10) << 4) |
		((svga->crtc[9] & 0x40) << 3);
	*pline_compare = line_compare;
}

uint32_t cirrus_get_bpp16_depth(gd5446_t *gd5446, svga_t *svga)
{
	uint32_t ret = 16;

	switch (svga->dac_mask & 0xf)
	{
		case 0:
			ret = 15;	/* Sierra HiColor */
			break;
		case 1:
			ret = 16;	/* XGA HiColor */
			break;
		default:
#ifdef DEBUG_CIRRUS
			printf("cirrus: invalid DAC value %x in 16bpp\n", (svga->dac_mask & 0xf));
#endif
			ret = 15;
			break;
	}
	return ret;
}

int cirrus_get_bpp(gd5446_t *gd5446, svga_t *svga)
{
	uint32_t ret = 8;

	if ((svga->seqregs[7] & 1) != 0)
	{
		/* Cirrus SVGA */
		switch (svga->seqregs[7] & CIRRUS_SR7_BPP_MASK)
		{
			case CIRRUS_SR7_BPP_8:
				ret = 8;
				break;
			case CIRRUS_SR7_BPP_16_DOUBLEVCLK:
				ret = cirrus_get_bpp16_depth(gd5446, svga);
				break;
			case CIRRUS_SR7_BPP_24:
				ret = 24;
				break;
			case CIRRUS_SR7_BPP_16:
				ret = cirrus_get_bpp16_depth(gd5446, svga);
				break;
			case CIRRUS_SR7_BPP_32:
				ret = 32;
				break;
			default:
#ifdef DEBUG_CIRRUS
				printf("cirrus: unknown bpp - sr7=%x\n", vga->seqregs[7]);
#endif
				ret = 8;
				break;
		}
	}
	else
	{
		/* VGA */
		ret = 0;
	}

	return ret;
}

void cirrus_get_resolution(gd5446_t *gd5446, svga_t *svga, int *pwidth, int *pheight)
{
	int width, height;

	width = (svga->crtc[1] + 1) * 8;
	height = svga->crtc[0x12] | ((svga->crtc[7] & 2) << 7) | ((svga->crtc[7] & 0x40) << 3);
	height = (height + 1);
	/* interlace support */
	if (svga->crtc[0x1a] & 1)  height *= 2;
	*pwidth = width;
	*pheight = height;
}

void cirrus_update_bank_ptr(gd5446_t *gd5446, svga_t *svga, unsigned bank_index)
{
	unsigned offset;
	unsigned limit;

	if ((svga->gdcreg[0xb] & 1) != 0)	/* dual bank */
		offset = svga->gdcreg[9 + bank_index];
	else					/* single bank */
		offset = svga->gdcreg[9];

	if ((svga->gdcreg[0xb] & 0x20) != 0)
		offset <<= 14;
	else
		offset <<= 12;

	if (gd5446_vram_size <= offset)
		limit = 0;
	else
		limit = gd5446_vram_size - offset;

	if (((svga->gdcreg[0xb] & 1) == 0) && (bank_index != 0))
	{
		if (limit > 0x8000)
		{
			offset += 0x8000;
			limit -= 0x8000;
		}
		else
		{
			limit = 0;
		}
	}

	if (limit > 0)
	{
		gd5446->bank_base[bank_index] = offset;
		gd5446->bank_limit[bank_index] = limit;
	}
	else
	{
		gd5446->bank_base[bank_index] = 0;
		gd5446->bank_limit[bank_index] = 0;
	}
}

void init_rops()
{
	int i = 0;

	for(i = 0;i < 256; i++)
		rop_to_index[i] = CIRRUS_ROP_NOP_INDEX; /* nop rop */
	rop_to_index[CIRRUS_ROP_0] = 0;
	rop_to_index[CIRRUS_ROP_SRC_AND_DST] = 1;
	rop_to_index[CIRRUS_ROP_NOP] = 2;
	rop_to_index[CIRRUS_ROP_SRC_AND_NOTDST] = 3;
	rop_to_index[CIRRUS_ROP_NOTDST] = 4;
	rop_to_index[CIRRUS_ROP_SRC] = 5;
	rop_to_index[CIRRUS_ROP_1] = 6;
	rop_to_index[CIRRUS_ROP_NOTSRC_AND_DST] = 7;
	rop_to_index[CIRRUS_ROP_SRC_XOR_DST] = 8;
	rop_to_index[CIRRUS_ROP_SRC_OR_DST] = 9;
	rop_to_index[CIRRUS_ROP_NOTSRC_OR_NOTDST] = 10;
	rop_to_index[CIRRUS_ROP_SRC_NOTXOR_DST] = 11;
	rop_to_index[CIRRUS_ROP_SRC_OR_NOTDST] = 12;
	rop_to_index[CIRRUS_ROP_NOTSRC] = 13;
	rop_to_index[CIRRUS_ROP_NOTSRC_OR_DST] = 14;
	rop_to_index[CIRRUS_ROP_NOTSRC_AND_NOTDST] = 15;
}