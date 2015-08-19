/*	Cirrus Logic CL-GD emulation
	Original QEMU code by: Fabrice Bellard
	Adapter for PCem by: OBattler
	Original PCem code by: Tom Walker
	GD6235 ID and ROM contributed by: kazblox */
#include <stdio.h>
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
#include "vid_cl_gd.h"
#include "vid_cl_gd_blit.h"

// void clgd_start_blit(uint32_t cpu_dat, int count, void *p);

// void clgd_mmio_write(uint32_t addr, uint8_t val, void *p);
// uint8_t clgd_mmio_read(uint32_t addr, void *p);

void clgd_recalc_banking(clgd_t *clgd);
// void clgd_recalc_mapping(clgd_t *clgd);

void clgd_recalctimings(svga_t *svga);

void svga_cirrus_recalcbanks(clgd_t *clgd, unsigned bank_index);
void svga_write_mode45_8bpp(clgd_t *clgd, unsigned mode, unsigned offset, uint32_t mem_value);
void svga_write_mode45_16bpp(clgd_t *clgd, unsigned mode, unsigned offset, uint32_t mem_value);
void svga_write_cirrus(uint32_t addr, uint8_t val, void *p);
void svga_write_cirrus_linear(uint32_t addr, uint8_t val, void *p);
void svga_write_cirrus_linear_bitblt(uint32_t addr, uint8_t val, void *p);
uint8_t svga_read_cirrus(uint32_t addr, void *p);
uint8_t svga_read_cirrus_linear(uint32_t addr, void *p);
uint8_t svga_read_cirrus_linear_bitblt(uint32_t addr, void *p);

void clgd_reset_blit(clgd_t *clgd, svga_t *svga)
{
	svga->gdcreg[0x31] &= ~(CIRRUS_BLT_START | CIRRUS_BLT_BUSY | CIRRUS_BLT_FIFOUSED);
	svga_cirrus_recalcbanks(clgd, 0);
	svga_cirrus_recalcbanks(clgd, 1);
}

void clgd_out(uint16_t addr, uint8_t val, void *p)
{
        clgd_t *clgd = (clgd_t *)p;
        svga_t *svga = &clgd->svga;
        uint8_t old;
	uint32_t i;
	FILE *f;
        
        if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1)) 
                addr ^= 0x60;

        // pclog("clgd out %04X %02X\n", addr, val);
                
        switch (addr)
        {
		case 0x200:
		// Debug VRAM dumping
		f = fopen("vramdump.dmp", "wb");		
		fwrite(svga->vram, (1 << 22), 1, f);
		fclose(f);
		return;
                case 0x3c4:
                svga->seqaddr = val;
                break;
                case 0x3c5:
                        switch (svga->seqaddr & 0x1f)
                        {
				case 0x00: case 0x01: case 0x02: case 0x03:
				case 0x04:
					svga_out(addr, val, svga);
					break;
				case 0x06:
					val &= 0x17;
					if (val == 0x12)
					{
						svga->seqregs[svga->seqaddr & 0x1f] = 0x12;
					}
					else
					{
						svga->seqregs[svga->seqaddr & 0x1f] = 0xf;
					}
					break;
                                case 0x10: case 0x30: case 0x50: case 0x70:
                                case 0x90: case 0xb0: case 0xd0: case 0xf0:
					svga->seqregs[0x10] = val;
        	                        svga->hwcursor.x = (val << 3) | (svga->seqaddr >> 5);
                	                // pclog("svga->hwcursor.x = %i\n", svga->hwcursor.x);
	                                break;
                                case 0x11: case 0x31: case 0x51: case 0x71:
                                case 0x91: case 0xb1: case 0xd1: case 0xf1:
					svga->seqregs[0x11] = val;
	                                svga->hwcursor.y = (val << 3) | (svga->seqaddr >> 5);
        	                        // pclog("svga->hwcursor.y = %i\n", svga->hwcursor.y);
                	                break;
				case 0x07:
					cirrus_update_memory_access(clgd);
					clgd_recalctimings(svga);
					if (old != val)  svga->bpp = cirrus_get_bpp(clgd, svga);
				case 0x08: case 0x09: case 0x0a: case 0x0b:
				case 0x0c: case 0x0d: case 0x0e: case 0x0f:
				case 0x14: case 0x15: case 0x16:
				case 0x18: case 0x19: case 0x1a: case 0x1b:
				case 0x1c: case 0x1d: case 0x1e: case 0x1f:
                        		svga->seqregs[svga->seqaddr & 0x1f] = val;
					break;
                                case 0x13:
                        		svga->seqregs[svga->seqaddr & 0x1f] = val;
                	                svga->hwcursor.addr = 0x1fc000 + ((val & 0x3f) * 256);
        	                        // pclog("svga->hwcursor.addr = %x\n", svga->hwcursor.addr);
	                                break;                                
                                case 0x12:
        	                	svga->seqregs[svga->seqaddr & 0x1f] = val;
                	                svga->hwcursor.ena = val & 1;
                        	        // pclog("svga->hwcursor.ena = %i\n", svga->hwcursor.ena);
                                	break;
                                case 0x17:
					old = svga->seqregs[svga->seqaddr & 0x1f];
					svga->seqregs[svga->seqaddr & 0x1f] = (svga->seqregs[svga->seqaddr & 0x1f] & 0x38) | (val & 0xc7);
					cirrus_update_memory_access(clgd);
					break;
                                break;
                        }
                        return;
	                break;

                case 0x3C6: case 0x3C7: case 0x3C8: case 0x3C9:
			// pclog("Write RAMDAC %04X %02X %04X:%04X\n", addr, val, CS, pc);
	                cl_ramdac_out(addr, val, &clgd->ramdac, (void *) clgd, svga);
        	        return;

                case 0x3cf:
			switch(svga->gdcaddr)
			{
				case 0x00:
					clgd->shadow_gr0 = val;
					svga_out(addr, val, svga);
					break;
				case 0x01:
					clgd->shadow_gr1 = val;
					svga_out(addr, val, svga);
					break;
				case 0x02: case 0x03: case 0x04: case 0x06:
				case 0x07: case 0x08:
					svga_out(addr, val, svga);
					break;
				case 0x05:
					svga_out(addr, val, svga);
					svga->gdcreg[5] = val & 0x7f;
					cirrus_update_memory_access(clgd);
	                	        break;
				case 0x09: case 0x0A: case 0x0B:
					svga->gdcreg[svga->gdcaddr & 0x3f] = val;
					svga_cirrus_recalcbanks(clgd, 0);
					svga_cirrus_recalcbanks(clgd, 1);
					cirrus_update_memory_access(clgd);
					break;
				case 0x21: case 0x23: case 0x25: case 0x27:
					svga->gdcreg[svga->gdcaddr & 0x3f] = val & 0x1f;
					break;
				case 0x2a:
					svga->gdcreg[svga->gdcaddr & 0x3f] = val & 0x3f;
					/* if auto start mode, starts bit blt now */
					if (svga->gdcreg[0x31] & CIRRUS_BLT_AUTOSTART)  cirrus_bitblt_start(clgd, svga);
					break;
				case 0x2e:
					svga->gdcreg[svga->gdcaddr & 0x3f] = val & 0x3f;
					break;
				case 0x31:
					svga->gdcreg[svga->gdcaddr & 0x3f] = val & 0x3f;

					if (((old & CIRRUS_BLT_RESET) != 0) && ((val & CIRRUS_BLT_RESET) == 0))
					{
						// TODO: Bitblt reset
						clgd_reset_blit(clgd, svga);
					}
					else if (((old & CIRRUS_BLT_START) == 0) && (val & CIRRUS_BLT_START != 0))
					{
						// clgd_start_blit(0, -1, gd5446);
						cirrus_bitblt_start(clgd, svga);
					}

					/* if (svga->gdcreg[0x31] & CIRRUS_BLT_AUTOSTART)
					{
						cirrus_bitblt_start(clgd, svga);
					} */
				break;
				default:
					svga->gdcreg[svga->gdcaddr & 0x3f] = val;
				break;
                        }                        
                        return;
	                break;
                
                case 0x3D4:
                svga->crtcreg = val & 0x3f;
                return;
                case 0x3D5:
		if (svga->crtcreg <= 0x18)
			val &= mask_crtc[svga->crtcreg];
                if ((svga->crtcreg < 7) && (svga->crtc[0x11] & 0x80))
                        return;
                if ((svga->crtcreg == 7) && (svga->crtc[0x11] & 0x80))
                        val = (svga->crtc[7] & ~0x10) | (val & 0x10);
                old = svga->crtc[svga->crtcreg];
                if ((svga->crtcreg != 0x22) && (svga->crtcreg != 0x24) && (svga->crtcreg != 0x26) && (svga->crtcreg != 0x27))  svga->crtc[svga->crtcreg] = val;
                if ((svga->crtcreg == 0x22) || (svga->crtcreg == 0x24) || (svga->crtcreg == 0x26) || (svga->crtcreg == 0x27))  return;

                if (old != val)
                {
			if (svga->crtcreg == 0x1b)
			{
				svga->vrammask = (val & 2) ? (clgd->vram_size - 1) : 0x3ffff;
				clgd->linear_mmio_mask = (val & 2) ? (clgd->vram_size - 256) : (0x40000 - 256);
			}
                        if (svga->crtcreg < 0xe || svga->crtcreg > 0x10)
                        {
                                svga->fullchange = changeframecount;
                                svga_recalctimings(svga);
                        }
                }
                break;
        }
	if (addr == 0x3C6)  old = svga->dac_mask;
        svga_out(addr, val, svga);
	if (addr == 0x3C6)  if (old != val)  svga->bpp = cirrus_get_bpp(clgd, svga);
}

uint8_t clgd_in(uint16_t addr, void *p)
{
        clgd_t *clgd = (clgd_t *)p;
        svga_t *svga = &clgd->svga;

        if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3d0) && !(svga->miscout & 1)) 
                addr ^= 0x60;
        
        // if (addr != 0x3da) pclog("IN clgd %04X\n", addr);
        
        switch (addr)
        {
                case 0x3c5:
                if (svga->seqaddr > 5)
                {
                        switch (svga->seqaddr)
                        {
				case 0x00: case 0x01: case 0x02: case 0x03:
				case 0x04:
					return svga->seqregs[svga->seqaddr];
				case 0x06:
					return svga->seqregs[svga->seqaddr];
				case 0x10: case 0x30: case 0x50: case 0x70:
				case 0x90: case 0xb0: case 0xd0: case 0xf0:
					return svga->seqregs[0x10];
				case 0x11: case 0x31: case 0x51: case 0x71:
				case 0x91: case 0xb1: case 0xd1: case 0xf1:
					return svga->seqregs[0x11];
				case 0x05: case 0x07: case 0x08: case 0x09:
				case 0x0a: case 0x0b: case 0x0c: case 0x0d:
				case 0x0e: case 0x0f: case 0x12: case 0x13:
				case 0x14: case 0x15: case 0x16: case 0x17:
				case 0x18: case 0x19: case 0x1a: case 0x1b:
				case 0x1c: case 0x1d: case 0x1e: case 0x1f:
#ifdef DEBUG_CIRRUS
					printf("cirrus: handled inport sr_index %02x\n", svga->seqaddr);
#endif
					// return svga->seqregs[svga->seqaddr];
					if (svga->seqaddr == 0x15)  return clgd->vram_code;
#ifdef DEBUG_CIRRUS
					// printf("cirrus: handled inport sr_index %02x\n", svga->seqaddr);
#endif
				default:
					return 0xff;
					break;
                        }
seq_read_common:
                        return svga->seqregs[svga->seqaddr & 0x3f];
                }
                break;

                case 0x3cf:
		if (svga->gdcaddr >= 0x3a)
		{
			return 0xff;
		}
                if (svga->gdcaddr > 8)
                {
                        return svga->gdcreg[svga->gdcaddr & 0x3f];
                }
                break;

                case 0x3c6: case 0x3c7: case 0x3c8: case 0x3c9:
//                pclog("Read RAMDAC %04X  %04X:%04X\n", addr, CS, pc);
                return cl_ramdac_in(addr, &clgd->ramdac, (void *) clgd, svga);

                case 0x3D4:
                return svga->crtcreg;
                case 0x3D5:
                switch (svga->crtcreg)
                {
			case 0x24: /*Attribute controller toggle readback (R)*/
			return svga->attrff << 7;
			case 0x26: /*Attribute controller index readback (R)*/
			return svga->attraddr & 0x3f;
                        // case 0x27: /*ID (R)*/
                        // return 0xb8; /*clgd*/
                }
                return svga->crtc[svga->crtcreg];
        }
        return svga_in(addr, svga);
}

void clgd_recalc_banking_old(clgd_t *clgd)
{
        svga_t *svga = &clgd->svga;
        
        if (svga->gdcreg[0xb] & 0x20)
                clgd->bank[0] = (svga->gdcreg[0x09] & 0x7f) << 14;
        else
                clgd->bank[0] = svga->gdcreg[0x09] << 12;
                                
        if (svga->gdcreg[0xb] & 0x01)
        {
                if (svga->gdcreg[0xb] & 0x20)
                        clgd->bank[1] = (svga->gdcreg[0x0a] & 0x7f) << 14;
                else
                        clgd->bank[1] = svga->gdcreg[0x0a] << 12;
        }
        else
                clgd->bank[1] = clgd->bank[0] + 0x8000;
}

void clgd_recalc_banking(clgd_t *clgd)
{
        svga_t *svga = &clgd->svga;

        if (svga->gdcreg[0xb] & 0x01)
        {
		clgd->bank[0] = svga->gdcreg[0x09];
		clgd->bank[1] = svga->gdcreg[0x0a];
        }
	else
	{
		clgd->bank[0] = svga->gdcreg[0x09];
		clgd->bank[1] = svga->gdcreg[0x09];
	}
        
        if (svga->gdcreg[0xb] & 0x20)
	{
                clgd->bank[0] <<= 14;
                clgd->bank[1] <<= 14;
	}
        else
	{
                clgd->bank[0] <<= 12;
                clgd->bank[1] <<= 12;
	}
                                
        if (!(svga->gdcreg[0xb] & 0x01))  clgd->bank[1] += 0x8000;
}

#ifdef LEGACY_CIRRUS_RECMAP
void clgd_recalc_mapping(clgd_t *clgd)
{
        svga_t *svga = &clgd->svga;
        
        pclog("Write mapping %02X %i\n", svga->gdcreg[6], svga->seqregs[0x17] & 0x04);
        switch (svga->gdcreg[6] & 0x0C)
        {
                case 0x0: /*128k at A0000*/
		// Why are we disabling MMIO in 128k mode?
                // mem_mapping_disable(&clgd->mmio_mapping);
                if (svga->seqregs[0x17] & 0x04)
		{
	                mem_mapping_enable(&clgd->mmio_mapping);
                        mem_mapping_set_addr(&clgd->mmio_mapping, 0xb8000, 0x00100);
		}
		else
		{
                	mem_mapping_disable(&clgd->mmio_mapping);
		}
                break;
                case 0x4: /*64k at A0000*/
                if (svga->seqregs[0x17] & 0x04)
		{
	                mem_mapping_enable(&clgd->mmio_mapping);
                        mem_mapping_set_addr(&clgd->mmio_mapping, 0xb8000, 0x00100);
		}
		else
		{
                	mem_mapping_disable(&clgd->mmio_mapping);
		}
                break;
                case 0x8: /*32k at B0000*/
                mem_mapping_disable(&clgd->mmio_mapping);
                break;
                case 0xC: /*32k at B8000*/
                mem_mapping_disable(&clgd->mmio_mapping);
                break;
        }
}
#endif
        
void clgd_recalctimings(svga_t *svga)
{
        clgd_t *clgd = (clgd_t *)svga->p;

        if (svga->seqregs[7] & 0x01)
        {
		if (cirrus_get_bpp(clgd, svga) == 8)  svga->render = svga_render_8bpp_highres;
		if (cirrus_get_bpp(clgd, svga) == 15)  svga->render = svga_render_15bpp_highres;
		if (cirrus_get_bpp(clgd, svga) == 16)  svga->render = svga_render_16bpp_highres;
		if (cirrus_get_bpp(clgd, svga) == 24)  svga->render = svga_render_24bpp_highres;
		if (cirrus_get_bpp(clgd, svga) == 32)  svga->render = svga_render_32bpp_highres;
	}
        
        svga->ma_latch |= ((svga->crtc[0x1b] & 0x01) << 16) | ((svga->crtc[0x1b] & 0xc) << 15);
        // pclog("MA now %05X %02X\n", svga->ma_latch, svga->crtc[0x1b]);
}

void clgd_hwcursor_draw(svga_t *svga, int displine)
{
        int x;
        uint8_t dat[2];
        int xx;
        int offset = svga->hwcursor_latch.x - svga->hwcursor_latch.xoff;
	int largecur = (svga->seqregs[0x12] & 4);
	int cursize = (largecur) ? 64 : 32;
        
        pclog("HWcursor %i %i  %i %i  %x %02X %02X\n", svga->hwcursor_latch.x, svga->hwcursor_latch.y,  offset, displine, svga->hwcursor_latch.addr, vram[svga->hwcursor_latch.addr], vram[svga->hwcursor_latch.addr + 0x80]);
        for (x = 0; x < cursize; x += 8)
        {
                dat[0] = svga->vram[svga->hwcursor_latch.addr];
                dat[1] = svga->vram[svga->hwcursor_latch.addr + 0x80];
                for (xx = 0; xx < 8; xx++)
                {
                        if (offset >= svga->hwcursor_latch.x)
                        {
                                if (dat[1] & 0x80)
                                        ((uint32_t *)buffer32->line[displine])[offset + cursize] = 0;
                                if (dat[0] & 0x80)
                                        ((uint32_t *)buffer32->line[displine])[offset + cursize] ^= 0xffffff;
                        }
                           
                        offset++;
                        dat[0] <<= 1;
                        dat[1] <<= 1;
                }
                svga->hwcursor_latch.addr++;
        }
}

#ifdef LEGACY_CIRRUS_BLIT
void clgd_start_blit(uint32_t cpu_dat, int count, void *p)
{
        clgd_t *clgd = (clgd_t *)p;
        svga_t *svga = &clgd->svga;

        pclog("clgd_start_blit %i\n", count);
        if (count == -1)
        {
                clgd->blt.dst_addr_backup = clgd->blt.dst_addr;
                clgd->blt.src_addr_backup = clgd->blt.src_addr;
                clgd->blt.width_backup    = clgd->blt.width;
                clgd->blt.height_internal = clgd->blt.height;
                clgd->blt.x_count         = clgd->blt.mask & 7;
                pclog("clgd_start_blit : size %i, %i\n", clgd->blt.width, clgd->blt.height);
                
                if (clgd->blt.mode & 0x04)
                {
//                        pclog("blt.mode & 0x04\n");
                        mem_mapping_set_handler(&svga->mapping, NULL, NULL, NULL, clgd_blt_write_b, clgd_blt_write_w, clgd_blt_write_l);
                        mem_mapping_set_p(&svga->mapping, clgd);
                        clgd_recalc_mapping(clgd);
                        return;
                }
                else
                {
			mem_mapping_set_handler(&svga->mapping, svga_read_cirrus, svga_readw, svga_readl, svga_write_cirrus, svga_writew, svga_writel);
			mem_mapping_set_p(&svga->mapping, svga);
                        clgd_recalc_mapping(clgd);
                        return;
                }                
        }
        
	// printf("Blit, CPU Data %08X, write mode %02X\n", cpu_dat, svga->writemode);
        while (count)
        {
                uint8_t src, dst;
                int mask;
                
                if (clgd->blt.mode & 0x04)
                {
                        if (clgd->blt.mode & 0x80)
                        {
                                src = (cpu_dat & 0x80) ? clgd->blt.fg_col : clgd->blt.bg_col;
                                mask = cpu_dat & 0x80;
                                cpu_dat <<= 1;
                                count--;
                        }
                        else
                        {
                                src = cpu_dat & 0xff;
                                cpu_dat >>= 8;
                                count -= 8;
                                mask = 1;
                        }
                }
                else
                {
                        switch (clgd->blt.mode & 0xc0)
                        {
                                case 0x00:
                                src = svga->vram[clgd->blt.src_addr & svga->vrammask];
                                clgd->blt.src_addr += ((clgd->blt.mode & 0x01) ? -1 : 1);
                                mask = 1;
                                break;
                                case 0x40:
                                src = svga->vram[(clgd->blt.src_addr & (svga->vrammask & ~7)) | (clgd->blt.dst_addr & 7)];
                                mask = 1;
                                break;
                                case 0x80:
                                mask = svga->vram[clgd->blt.src_addr & svga->vrammask] & (0x80 >> clgd->blt.x_count);
                                src = mask ? clgd->blt.fg_col : clgd->blt.bg_col;
                                clgd->blt.x_count++;
                                if (clgd->blt.x_count == 8)
                                {
                                        clgd->blt.x_count = 0;
                                        clgd->blt.src_addr++;
                                }
                                break;
                                case 0xc0:
                                mask = svga->vram[clgd->blt.src_addr & svga->vrammask] & (0x80 >> (clgd->blt.dst_addr & 7));
                                src = mask ? clgd->blt.fg_col : clgd->blt.bg_col;
                                break;
                        }
                        count--;                        
                }
                dst = svga->vram[clgd->blt.dst_addr & svga->vrammask];
                svga->changedvram[(clgd->blt.dst_addr & svga->vrammask) >> 12] = changeframecount;
               
                pclog("Blit %i,%i %06X %06X  %06X %02X %02X  %02X %02X ", clgd->blt.width, clgd->blt.height_internal, clgd->blt.src_addr, clgd->blt.dst_addr, clgd->blt.src_addr & svga->vrammask, svga->vram[clgd->blt.src_addr & svga->vrammask], 0x80 >> (clgd->blt.dst_addr & 7), src, dst);
                switch (clgd->blt.rop)
                {
                        case 0x00: dst = 0;             break;
                        case 0x05: dst =   src &  dst;  break;
                        case 0x06: dst =   dst;         break;
                        case 0x09: dst =   src & ~dst;  break;
                        case 0x0b: dst = ~ dst;         break;
                        case 0x0d: dst =   src;         break;
                        case 0x0e: dst = 0xff;          break;
                        case 0x50: dst =  ~src &  dst;  break;
                        case 0x59: dst =   src ^  dst;  break;
                        case 0x6d: dst =   src |  dst;  break;
                        case 0x90: dst =  ~src | ~dst;  break;
                        case 0x95: dst = ~(src ^  dst); break;
                        case 0xad: dst =   src | ~dst;  break;
                        case 0xd0: dst =  ~src;         break;
                        case 0xd6: dst =  ~src |  dst;  break;
                        // case 0xda: dst = ~(src &  dst); break;                       
                        case 0xda: dst =  ~src & ~dst;  break;                       
                }
                pclog("%02X  %02X\n", dst, mask);
                
                if ((clgd->blt.width_backup - clgd->blt.width) >= (clgd->blt.mask & 7) &&
                    !((clgd->blt.mode & 0x08) && !mask))
                        svga->vram[clgd->blt.dst_addr & svga->vrammask] = dst;
                
                clgd->blt.dst_addr += ((clgd->blt.mode & 0x01) ? -1 : 1);
                
                clgd->blt.width--;
                
                if (clgd->blt.width == 0xffff)
                {
                        clgd->blt.width = clgd->blt.width_backup;

                        clgd->blt.dst_addr = clgd->blt.dst_addr_backup = clgd->blt.dst_addr_backup + ((clgd->blt.mode & 0x01) ? -clgd->blt.dst_pitch : clgd->blt.dst_pitch);
                        
                        switch (clgd->blt.mode & 0xc0)
                        {
                                case 0x00:
                                clgd->blt.src_addr = clgd->blt.src_addr_backup = clgd->blt.src_addr_backup + ((clgd->blt.mode & 0x01) ? -clgd->blt.src_pitch : clgd->blt.src_pitch);
                                break;
                                case 0x40:
                                clgd->blt.src_addr = ((clgd->blt.src_addr + ((clgd->blt.mode & 0x01) ? -8 : 8)) & 0x38) | (clgd->blt.src_addr & ~0x38);
                                break;
                                case 0x80:
                                if (clgd->blt.x_count != 0)
                                {
                                        clgd->blt.x_count = 0;
                                        clgd->blt.src_addr++;
                                }
                                break;
                                case 0xc0:
                                clgd->blt.src_addr = ((clgd->blt.src_addr + ((clgd->blt.mode & 0x01) ? -1 : 1)) & 7) | (clgd->blt.src_addr & ~7);
                                break;
                        }
                        
                        clgd->blt.height_internal--;
                        if (clgd->blt.height_internal == 0xffff)
                        {
                                if (clgd->blt.mode & 0x04)
                                {
                                        mem_mapping_set_handler(&svga->mapping, svga_read_cirrus, svga_readw, svga_readl, svga_write_cirrus, svga_writew, svga_writel);
                                        mem_mapping_set_p(&svga->mapping, svga);
                                        clgd_recalc_mapping(clgd);
                                }
                                return;
                        }
                                
                        if (clgd->blt.mode & 0x04)
                                return;
                }                        
        }
}

void clgd_mmio_write(uint32_t addr, uint8_t val, void *p)
{
        clgd_t *clgd = (clgd_t *)p;
	svga_t *svga = &clgd->svga;

	svga->vram[clgd->vram_size - 256 + (addr & 0xff)] = val;

        pclog("MMIO write %08X %02X\n", addr, val);
        switch (addr & 0xff)
        {
                case 0x00:
                clgd->blt.bg_col = (clgd->blt.bg_col & 0xff00) | val;
                break;
                case 0x01:
                clgd->blt.bg_col = (clgd->blt.bg_col & 0x00ff) | (val << 8);
                break;

                case 0x04:
                clgd->blt.fg_col = (clgd->blt.fg_col & 0xff00) | val;
                break;
                case 0x05:
                clgd->blt.fg_col = (clgd->blt.fg_col & 0x00ff) | (val << 8);
                break;

                case 0x08:
                clgd->blt.width = (clgd->blt.width & 0xff00) | val;
                break;
                case 0x09:
                clgd->blt.width = (clgd->blt.width & 0x00ff) | (val << 8);
                break;
                case 0x0a:
                clgd->blt.height = (clgd->blt.height & 0xff00) | val;
                break;
                case 0x0b:
                clgd->blt.height = (clgd->blt.height & 0x00ff) | (val << 8);
                break;
                case 0x0c:
                clgd->blt.dst_pitch = (clgd->blt.dst_pitch & 0xff00) | val;
                break;
                case 0x0d:
                clgd->blt.dst_pitch = (clgd->blt.dst_pitch & 0x00ff) | (val << 8);
                break;
                case 0x0e:
                clgd->blt.src_pitch = (clgd->blt.src_pitch & 0xff00) | val;
                break;
                case 0x0f:
                clgd->blt.src_pitch = (clgd->blt.src_pitch & 0x00ff) | (val << 8);
                break;
                
                case 0x10:
                clgd->blt.dst_addr = (clgd->blt.dst_addr & 0xffffff00) | val;
                break;
                case 0x11:
                clgd->blt.dst_addr = (clgd->blt.dst_addr & 0xffff00ff) | (val << 8);
                break;
                case 0x12:
                clgd->blt.dst_addr = (clgd->blt.dst_addr & 0xff00ffff) | (val << 16);
                break;

                case 0x14:
                clgd->blt.src_addr = (clgd->blt.src_addr & 0xffffff00) | val;
                break;
                case 0x15:
                clgd->blt.src_addr = (clgd->blt.src_addr & 0xffff00ff) | (val << 8);
                break;
                case 0x16:
                clgd->blt.src_addr = (clgd->blt.src_addr & 0xff00ffff) | (val << 16);
                break;

                case 0x17:
                clgd->blt.mask = val;
                break;
                case 0x18:
                clgd->blt.mode = val;
                break;
                
                case 0x1a:
                clgd->blt.rop = val;
                break;

		case 0x1b:
		clgd->blt.modeext = val;
		break;

		case 0x1c:
		clgd->blt.blttc = (clgd->blt.blttc & 0xffffff00) | val;
		break;
		case 0x1d:
		clgd->blt.blttc = (clgd->blt.blttc & 0xffff00ff) | (val << 8);
		break;
		case 0x1e:
		clgd->blt.blttc = (clgd->blt.blttc & 0xff00ffff) | (val << 16);
		break;
		case 0x1f:
		clgd->blt.blttc = (clgd->blt.blttc & 0x00ffffff) | (val << 24);
		break;
                
		case 0x20:
		clgd->blt.blttcmask = (clgd->blt.blttcmask & 0xffffff00) | val;
		break;
		case 0x21:
		clgd->blt.blttcmask = (clgd->blt.blttcmask & 0xffff00ff) | (val << 8);
		break;
		case 0x22:
		clgd->blt.blttcmask = (clgd->blt.blttcmask & 0xff00ffff) | (val << 16);
		break;
		case 0x23:
		clgd->blt.blttcmask = (clgd->blt.blttcmask & 0x00ffffff) | (val << 24);
		break;
                
		case 0x24:
		clgd->blt.ld_start_x = (clgd->blt.ld_start_x & 0x00ff) | val;
		break;
		case 0x25:
		clgd->blt.ld_start_x = (clgd->blt.ld_start_x & 0xff00) | (val << 8);
		break;
                
		case 0x26:
		clgd->blt.ld_start_y = (clgd->blt.ld_start_y & 0x00ff) | val;
		break;
		case 0x27:
		clgd->blt.ld_start_y = (clgd->blt.ld_start_y & 0xff00) | (val << 8);
		break;
                
		case 0x28:
		clgd->blt.ld_end_x = (clgd->blt.ld_end_x & 0x00ff) | val;
		break;
		case 0x29:
		clgd->blt.ld_end_x = (clgd->blt.ld_end_x & 0xff00) | (val << 8);
		break;
                
		case 0x2a:
		clgd->blt.ld_end_y = (clgd->blt.ld_end_y & 0x00ff) | val;
		break;
		case 0x2b:
		clgd->blt.ld_end_y = (clgd->blt.ld_end_y & 0xff00) | (val << 8);
		break;

		case 0x2c:
		clgd->blt.ld_ls_inc = val;
                
		case 0x2d:
		clgd->blt.ld_ls_ro = val;
                
		case 0x2e:
		clgd->blt.ld_ls_mask = val;
                
		case 0x2f:
		clgd->blt.ld_ls_ac = val;
                
		case 0x30:
		clgd->blt.bres_k1 = (clgd->blt.bres_k1 & 0x00ff) | val;
		break;
		case 0x31:
		clgd->blt.bres_k1 = (clgd->blt.bres_k1 & 0xff00) | (val << 8);
		break;

		case 0x32:
		clgd->blt.bres_k3 = (clgd->blt.bres_k3 & 0x00ff) | val;
		break;
		case 0x33:
		clgd->blt.bres_k3 = (clgd->blt.bres_k3 & 0xff00) | (val << 8);
		break;

		case 0x34:
		clgd->blt.bres_err = (clgd->blt.bres_err & 0x00ff) | val;
		break;
		case 0x35:
		clgd->blt.bres_err = (clgd->blt.bres_err & 0xff00) | (val << 8);
		break;

		case 0x36:
		clgd->blt.bres_dm = (clgd->blt.bres_dm & 0x00ff) | val;
		break;
		case 0x37:
		clgd->blt.bres_dm = (clgd->blt.bres_dm & 0xff00) | (val << 8);
		break;

		case 0x38:
		clgd->blt.bres_dir = val;
                
		case 0x39:
		clgd->blt.ld_mode = val;
                
                case 0x40:
		clgd->blt.blt_status = val;
                if (val & 0x02)
                        cirrus_bitblt_start(clgd, svga);
                break;
        }
}

uint8_t clgd_mmio_read(uint32_t addr, void *p)
{
        clgd_t *clgd = (clgd_t *)p;
	svga_t *svga = &clgd->svga;

        pclog("MMIO read %08X\n", addr);
        switch (addr & 0xff)
        {
                case 0x40: /*BLT status*/
                return 0;
        }
	return svga->vram[clgd->vram_size - 256 + (addr & 0xff)];
        // return 0xff; /*All other registers read-only*/
}
#endif

#ifdef LEGACY_BLT_READWRITE
void clgd_blt_write_b(uint32_t addr, uint8_t val, void *p)
{
        pclog("clgd_blt_write_w %08X %08X\n", addr, val);
        cirrus_bitblt_start(clgd, svga);
}

void clgd_blt_write_w(uint32_t addr, uint16_t val, void *p)
{
        pclog("clgd_blt_write_w %08X %08X\n", addr, val);
        cirrus_bitblt_start(clgd, svga);
}

void clgd_blt_write_l(uint32_t addr, uint32_t val, void *p)
{
        clgd_t *clgd = (clgd_t *)p;
        
        pclog("clgd_blt_write_l %08X %08X  %04X %04X\n", addr, val,  ((val >> 8) & 0x00ff) | ((val << 8) & 0xff00), ((val >> 24) & 0x00ff) | ((val >> 8) & 0xff00));
        if ((clgd->blt.mode & 0x84) == 0x84)
        {
                clgd_start_blit( val        & 0xff, 8, p);
                clgd_start_blit((val >> 8)  & 0xff, 8, p);
                clgd_start_blit((val >> 16) & 0xff, 8, p);
                clgd_start_blit((val >> 24) & 0xff, 8, p);
        }
        else
                clgd_start_blit(val, 32, p);
}
#endif

static void map_linear_vram_bank(clgd_t *clgd, unsigned bank)
{
	svga_t *svga = &clgd->svga;
	int enabled = !(clgd->src_ptr != clgd->src_ptr_end) && !((svga->seqregs[0x07] & 0x01) == 0) && !((svga->gdcreg[0x0B] & 0x14) == 0x14) && !(svga->gdcreg[0x0B] & 0x02);

	if (enabled)
	{
		mem_mapping_disable(&svga->mapping);
		mem_mapping_enable(&clgd->mbank[bank]);
		clgd->pbank[bank] = svga->vram + clgd->bank_base[bank];
                mem_mapping_set_addr(&clgd->mbank[bank], clgd->bank_base[bank], 0x18200);
	}
	else
	{
		mem_mapping_disable(&clgd->mbank[bank]);
		mem_mapping_enable(&svga->mapping);
	}
}

static void map_linear_vram(clgd_t *clgd)
{
	svga_t *svga = &clgd->svga;
	mem_mapping_disable(&svga->mapping);
	map_linear_vram_bank(clgd, 0);
	map_linear_vram_bank(clgd, 1);
}

static void unmap_linear_vram(clgd_t *clgd)
{
	svga_t *svga = &clgd->svga;
	mem_mapping_disable(&clgd->mbank[0]);
	mem_mapping_disable(&clgd->mbank[1]);
	mem_mapping_enable(&svga->mapping);
}

void cirrus_update_memory_access(clgd_t *clgd)
{
	svga_t *svga = &clgd->svga;
	unsigned mode;

	if ((svga->seqregs[0x17] & 0x44) == 0x44)
	{
		goto generic_io;
	}
	else if (clgd->src_ptr != clgd->src_ptr_end)
	{
		goto generic_io;
	}
	else
	{
		if ((svga->gdcreg[0x0B] & 0x14) == 0x14)
		{
			goto generic_io;
		}
		else if (svga->gdcreg[0x0B] & 0x02)
		{
			goto generic_io;
		}

		mode = svga->gdcreg[0x05] & 7;
		if (mode < 4 || mode > 5 || ((svga->gdcreg[0x0B] & 0x4) == 0))
		{
			map_linear_vram(clgd);
		}
		else
		{
generic_io:
			unmap_linear_vram(clgd);
		}
	}
}

void svga_cirrus_recalcbanks(clgd_t *clgd, unsigned bank_index)
{
	svga_t *svga = &clgd->svga;
	unsigned offset;
	unsigned limit;

	if ((svga->gdcreg[0x0b] & 0x01) != 0)	/* dual bank */
		offset = svga->gdcreg[0x09 + bank_index];
	else
		offset = svga->gdcreg[0x09];

	if ((svga->gdcreg[0x0b] & 0x20) != 0)
		offset <<= 14;
	else
		offset <<= 12;

	if (svga->vram_limit <= offset)
		limit = 0;
	else
		limit = svga->vram_limit - offset;


	if (((svga->gdcreg[0x0b] & 0x01) == 0) && (bank_index != 0))
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
		clgd->bank_base[bank_index] = offset;
		clgd->bank_limit[bank_index] = limit;
	}
	else
	{
		clgd->bank_base[bank_index] = 0;
		clgd->bank_limit[bank_index] = 0;
	}
}

void svga_write_mode45_8bpp(clgd_t *clgd, unsigned mode, unsigned offset, uint32_t mem_value)
{
	int x;
	unsigned val = mem_value;
	uint8_t *dst;
	svga_t *svga = &clgd->svga;

	dst = svga->vram + (offset &= svga->vrammask);

	svga->changedvram[(offset &= svga->vrammask) >> 12] = changeframecount;

	for (x = 0; x < 8; x++)
	{
		if (val & 0x80)
		{
			*dst = clgd->shadow_gr1;
		}
		else
		{
			*dst = clgd->shadow_gr0;
		}
		val <<= 1;
		dst++;
	}
}

void svga_write_mode45_16bpp(clgd_t *clgd, unsigned mode, unsigned offset, uint32_t mem_value)
{
	int x;
	unsigned val = mem_value;
	uint8_t *dst;
	svga_t *svga = &clgd->svga;

	dst = svga->vram + (offset &= svga->vrammask);

	svga->changedvram[(offset &= svga->vrammask) >> 12] = changeframecount;

	for (x = 0; x < 8; x++)
	{
		if (val & 0x80)
		{
			*dst = clgd->shadow_gr1;
			*(dst + 1) = svga->gdcreg[0x11];
		}
		else
		{
			*dst = clgd->shadow_gr0;
			*(dst + 1) = svga->gdcreg[0x10];
		}
		val <<= 1;
		dst += 2;
	}
}

static uint8_t cirrus_mmio_blt_read(clgd_t *clgd, unsigned address)
{
	svga_t *svga = &clgd->svga;
	int value = 0xff;

	switch(address)
	{
		case (CIRRUS_MMIO_BLTBGCOLOR + 0):
			value = svga->gdcreg[0x00];
			break;
		case (CIRRUS_MMIO_BLTBGCOLOR + 1):
			value = svga->gdcreg[0x10];
			break;
		case (CIRRUS_MMIO_BLTBGCOLOR + 2):
			value = svga->gdcreg[0x12];
			break;
		case (CIRRUS_MMIO_BLTBGCOLOR + 3):
			value = svga->gdcreg[0x14];
			break;
		case (CIRRUS_MMIO_BLTFGCOLOR + 0):
			value = svga->gdcreg[0x01];
			break;
		case (CIRRUS_MMIO_BLTFGCOLOR + 1):
			value = svga->gdcreg[0x11];
			break;
		case (CIRRUS_MMIO_BLTFGCOLOR + 2):
			value = svga->gdcreg[0x13];
			break;
		case (CIRRUS_MMIO_BLTFGCOLOR + 3):
			value = svga->gdcreg[0x15];
			break;
		case (CIRRUS_MMIO_BLTWIDTH + 0):
			value = svga->gdcreg[0x20];
			break;
		case (CIRRUS_MMIO_BLTWIDTH + 1):
			value = svga->gdcreg[0x21];
			break;
		case (CIRRUS_MMIO_BLTHEIGHT + 0):
			value = svga->gdcreg[0x22];
			break;
		case (CIRRUS_MMIO_BLTHEIGHT + 1):
			value = svga->gdcreg[0x23];
			break;
		case (CIRRUS_MMIO_BLTDESTPITCH + 0):
			value = svga->gdcreg[0x24];
			break;
		case (CIRRUS_MMIO_BLTDESTPITCH + 1):
			value = svga->gdcreg[0x25];
			break;
		case (CIRRUS_MMIO_BLTSRCPITCH + 0):
			value = svga->gdcreg[0x26];
			break;
		case (CIRRUS_MMIO_BLTSRCPITCH + 1):
			value = svga->gdcreg[0x27];
			break;
		case (CIRRUS_MMIO_BLTDESTADDR + 0):
			value = svga->gdcreg[0x28];
			break;
		case (CIRRUS_MMIO_BLTDESTADDR + 1):
			value = svga->gdcreg[0x29];
			break;
		case (CIRRUS_MMIO_BLTDESTADDR + 2):
			value = svga->gdcreg[0x2a];
			break;
		case (CIRRUS_MMIO_BLTSRCADDR + 0):
			value = svga->gdcreg[0x2c];
			break;
		case (CIRRUS_MMIO_BLTSRCADDR + 1):
			value = svga->gdcreg[0x2d];
			break;
		case (CIRRUS_MMIO_BLTSRCADDR + 2):
			value = svga->gdcreg[0x2e];
			break;
		case (CIRRUS_MMIO_BLTWRITEMASK):
			value = svga->gdcreg[0x2f];
			break;
		case (CIRRUS_MMIO_BLTMODE):
			value = svga->gdcreg[0x30];
			break;
		case (CIRRUS_MMIO_BLTROP):
			value = svga->gdcreg[0x32];
			break;
		case (CIRRUS_MMIO_BLTMODEEXT):
			value = svga->gdcreg[0x33];
			break;
		case (CIRRUS_MMIO_BLTTRANSPARENTCOLOR + 0):
			value = svga->gdcreg[0x34];
			break;
		case (CIRRUS_MMIO_BLTTRANSPARENTCOLOR + 1):
			value = svga->gdcreg[0x35];
			break;
		case (CIRRUS_MMIO_BLTTRANSPARENTCOLORMASK + 0):
			value = svga->gdcreg[0x38];
			break;
		case (CIRRUS_MMIO_BLTTRANSPARENTCOLORMASK + 1):
			value = svga->gdcreg[0x39];
			break;
		default:
			break;
	}

	return (uint8_t) value;
}

static uint8_t cirrus_mmio_blt_write(clgd_t *clgd, unsigned address, uint8_t value)
{
	svga_t *svga = &clgd->svga;

	switch(address)
	{
		case (CIRRUS_MMIO_BLTBGCOLOR + 0):
			svga->gdcreg[0x00] = value;
			break;
		case (CIRRUS_MMIO_BLTBGCOLOR + 1):
			svga->gdcreg[0x10] = value;
			break;
		case (CIRRUS_MMIO_BLTBGCOLOR + 2):
			svga->gdcreg[0x12] = value;
			break;
		case (CIRRUS_MMIO_BLTBGCOLOR + 3):
			svga->gdcreg[0x14] = value;
			break;
		case (CIRRUS_MMIO_BLTFGCOLOR + 0):
			svga->gdcreg[0x01] = value;
			break;
		case (CIRRUS_MMIO_BLTFGCOLOR + 1):
			svga->gdcreg[0x11] = value;
			break;
		case (CIRRUS_MMIO_BLTFGCOLOR + 2):
			svga->gdcreg[0x13] = value;
			break;
		case (CIRRUS_MMIO_BLTFGCOLOR + 3):
			svga->gdcreg[0x15] = value;
			break;
		case (CIRRUS_MMIO_BLTWIDTH + 0):
			svga->gdcreg[0x20] = value;
			break;
		case (CIRRUS_MMIO_BLTWIDTH + 1):
			svga->gdcreg[0x21] = value;
			break;
		case (CIRRUS_MMIO_BLTHEIGHT + 0):
			svga->gdcreg[0x22] = value;
			break;
		case (CIRRUS_MMIO_BLTHEIGHT + 1):
			svga->gdcreg[0x23] = value;
			break;
		case (CIRRUS_MMIO_BLTDESTPITCH + 0):
			svga->gdcreg[0x24] = value;
			break;
		case (CIRRUS_MMIO_BLTDESTPITCH + 1):
			svga->gdcreg[0x25] = value;
			break;
		case (CIRRUS_MMIO_BLTSRCPITCH + 0):
			svga->gdcreg[0x26] = value;
			break;
		case (CIRRUS_MMIO_BLTSRCPITCH + 1):
			svga->gdcreg[0x27] = value;
			break;
		case (CIRRUS_MMIO_BLTDESTADDR + 0):
			svga->gdcreg[0x28] = value;
			break;
		case (CIRRUS_MMIO_BLTDESTADDR + 1):
			svga->gdcreg[0x29] = value;
			break;
		case (CIRRUS_MMIO_BLTDESTADDR + 2):
			svga->gdcreg[0x2a] = value;
			break;
		case (CIRRUS_MMIO_BLTSRCADDR + 0):
			svga->gdcreg[0x2c] = value;
			break;
		case (CIRRUS_MMIO_BLTSRCADDR + 1):
			svga->gdcreg[0x2d] = value;
			break;
		case (CIRRUS_MMIO_BLTSRCADDR + 2):
			svga->gdcreg[0x2e] = value;
			break;
		case (CIRRUS_MMIO_BLTWRITEMASK):
			svga->gdcreg[0x2f] = value;
			break;
		case (CIRRUS_MMIO_BLTMODE):
			svga->gdcreg[0x30] = value;
			break;
		case (CIRRUS_MMIO_BLTROP):
			svga->gdcreg[0x32] = value;
			break;
		case (CIRRUS_MMIO_BLTMODEEXT):
			svga->gdcreg[0x33] = value;
			break;
		case (CIRRUS_MMIO_BLTTRANSPARENTCOLOR + 0):
			svga->gdcreg[0x34] = value;
			break;
		case (CIRRUS_MMIO_BLTTRANSPARENTCOLOR + 1):
			svga->gdcreg[0x35] = value;
			break;
		case (CIRRUS_MMIO_BLTTRANSPARENTCOLORMASK + 0):
			svga->gdcreg[0x38] = value;
			break;
		case (CIRRUS_MMIO_BLTTRANSPARENTCOLORMASK + 1):
			svga->gdcreg[0x39] = value;
			break;
		default:
			break;
	}
}

void svga_write_cirrus(uint32_t addr, uint8_t val, void *p)
{
	clgd_t *clgd = (clgd_t *)p;
        svga_t *svga = &clgd->svga;
	// uint32_t bank_offset;
	unsigned bank_index, bank_offset, mode;

	if ((svga->seqregs[0x07] & 0x01) == 0)
	{
		svga_write_common(addr, val, svga, 0);
		return;
	}

	if (addr < 0x10000)
	{
		if (clgd->src_ptr != clgd->src_ptr_end)
		{
			/* bitblt */
			*clgd->src_ptr++ = (uint8_t) val;
			if (clgd->src_ptr >= clgd->src_ptr_end)
			{
				cirrus_bitblt_cputovideo_next(clgd, svga);
			}
		}
		else
		{
			/* video memory */
			bank_index = addr >> 15;
			bank_offset = addr & 0x7fff;
			if (bank_offset < clgd->bank_limit[bank_index])
			{
				bank_offset += clgd->bank_base[bank_index];
				if ((svga->gdcreg[0x0B] & 0x14) == 0x14)
				{
					bank_offset <<= 4;
				}
				else if (svga->gdcreg[0x0B] & 0x02)
				{
					bank_offset <<= 3;
				}
				bank_offset &= svga->vrammask;
				mode = svga->gdcreg[0x05] & 0x7;
				if (mode < 4 || mode > 5 || ((svga->gdcreg[0x0B] & 0x4) == 0))
				{
					*(svga->vram + bank_offset) = val;
					svga->changedvram[bank_offset >> 12] = changeframecount;
				}
				else
				{
					if ((svga->gdcreg[0x0B] & 0x14) != 0x14)
					{
						svga_write_mode45_8bpp(clgd, mode, bank_offset, val);
					}
					else
					{
						svga_write_mode45_16bpp(clgd, mode, bank_offset, val);
					}
				}
			}
		}
	}
	else if (addr >= 0x18000 && addr < 0x18100)
	{
		/* memory-mapped I/O */
		if ((svga->seqregs[0x17] & 0x44) == 0x04)
		{
			cirrus_mmio_blt_write(clgd, addr & 0xff, val);
		}
	}
}

void svga_write_cirrus_linear(uint32_t addr, uint8_t val, void *p)
{
	clgd_t *clgd = (clgd_t *)p;
        svga_t *svga = &clgd->svga;
	unsigned mode;

	addr &= svga->vrammask;

	if (((svga->seqregs[0x17] & 0x44) == 0x44) && ((addr & clgd->linear_mmio_mask) == clgd->linear_mmio_mask))
	{
		/* memory-mapped I/O */
		cirrus_mmio_blt_write(clgd, addr & 0xff, val);
	}
	else if (clgd->src_ptr != clgd->src_ptr_end)
	{
		/* bitblt */
		*clgd->src_ptr++ = (uint8_t) val;
		if (clgd->src_ptr >= clgd->src_ptr_end)
		{
			cirrus_bitblt_cputovideo_next(clgd, svga);
		}
	}
	else
	{
		/* video memory */
		if ((svga->gdcreg[0x0B] & 0x14) == 0x14)
		{
			addr <<= 4;
		}
		else if (svga->gdcreg[0x0B] & 0x02)
		{
			addr <<= 3;
		}
		addr &= svga->vrammask;

		mode = svga->gdcreg[0x05] & 0x7;
		if (mode < 4 || mode > 5 || ((svga->gdcreg[0x0B] & 0x4) == 0))
		{
			*(svga->vram + addr) = val;
			svga->changedvram[addr >> 12] = changeframecount;
		}
		else
		{
			if ((svga->gdcreg[0x0B] & 0x14) != 0x14)
			{
				svga_write_mode45_8bpp(clgd, mode, addr, val);
			}
			else
			{
				svga_write_mode45_16bpp(clgd, mode, addr, val);
			}
		}
	}
}

void svga_write_cirrus_linear_bitblt(uint32_t addr, uint8_t val, void *p)
{
	clgd_t *clgd = (clgd_t *)p;
        svga_t *svga = &clgd->svga;

	if (clgd->src_ptr != clgd->src_ptr_end)
	{
		/* bitblt */
		*clgd->src_ptr++ = (uint8_t) val;
		if (clgd->src_ptr >= clgd->src_ptr_end)
		{
			cirrus_bitblt_cputovideo_next(clgd, svga);
		}
	}
}

uint8_t svga_read_cirrus(uint32_t addr, void *p)
{
	clgd_t *clgd = (clgd_t *)p;
        svga_t *svga = &clgd->svga;
	unsigned bank_index;
	unsigned bank_offset;
	uint8_t val;

	if ((svga->seqregs[0x07] & 0x01) == 0)
	{
		return svga_read_common(addr, svga, 0);
	}

	if (addr < 0x10000)
	{
		/* XXX handle bitblt */
		/* video memory */
		bank_index = addr >> 15;
		bank_offset = addr & 0x7fff;
		if (bank_offset < clgd->bank_limit[bank_index])
		{
			bank_offset += clgd->bank_base[bank_index];
			if ((svga->gdcreg[0x0B] & 0x14) == 0x14)
			{
				bank_offset <<= 4;
			}
			else if (svga->gdcreg[0x0B] & 0x02)
			{
				bank_offset <<= 3;
			}
			bank_offset &= svga->vrammask;
			val = *(svga->vram + bank_offset);
		}
		else
			val = 0xff;
	}
	else if (addr >= 0x18000 && addr < 0x18100)
	{
		/* memory-mapped I/O */
		val = 0xff;
		if ((svga->seqregs[0x17] & 0x44) == 0x04)
		{
			val = cirrus_mmio_blt_read(clgd, addr & 0xff);
		}
	}
	else
	{
		val = 0xff;
	}
	return val;
}

uint8_t svga_read_cirrus_linear(uint32_t addr, void *p)
{
	clgd_t *clgd = (clgd_t *)p;
        svga_t *svga = &clgd->svga;
	unsigned bank_index;
	unsigned bank_offset;
	uint32_t ret;

	addr &= svga->vrammask;

	if (((svga->seqregs[0x17] & 0x44) == 0x44) && ((addr & clgd->linear_mmio_mask) == clgd->linear_mmio_mask))
	{
		/* memory-mapped I/O */
		ret = cirrus_mmio_blt_read(clgd, addr & 0xff);
	}
	else if (0)
	{
		/* XXX handle bitblt */
		ret = 0xff;
	}
	else
	{
		/* video memory */
		if ((svga->gdcreg[0x0B] & 0x14) == 0x14)
		{
			addr <<= 4;
		}
		else if (svga->gdcreg[0x0B] & 0x02)
		{
			addr <<= 3;
		}
		addr &= svga->vrammask;
		ret = *(svga->vram + addr);
	}
	return ret;
}

uint8_t svga_read_cirrus_linear_bitblt(uint32_t addr, void *p)
{
	/* XXX handle bitblt */
	return 0xff;
}

void *clgd_common_init(char *romfn, uint8_t id)
{
        // clgd_t *clgd = malloc(sizeof(clgd_t));
        clgd = malloc(sizeof(clgd_t));
        svga_t *svga = &clgd->svga;
        memset(clgd, 0, sizeof(clgd_t));

        rom_init(&clgd->bios_rom, romfn, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
        
        svga_init(&clgd->svga, clgd, 1 << 21, /*2mb*/
                   clgd_recalctimings,
                   clgd_in, clgd_out,
                   clgd_hwcursor_draw,
                   NULL);

        mem_mapping_set_handler(&svga->mapping, svga_read_cirrus, NULL, NULL, svga_write_cirrus, NULL, NULL);
        mem_mapping_set_p(&svga->mapping, clgd);

        // mem_mapping_add(&clgd->mmio_mapping, 0, 0, clgd_mmio_read, NULL, NULL, clgd_mmio_write, NULL, NULL,  NULL, 0, clgd);

        mem_mapping_add(&clgd->mbank[0], 0, 0, svga_read_cirrus_linear, NULL, NULL, svga_write_cirrus_linear, NULL, NULL,  NULL, 0, clgd);
        mem_mapping_add(&clgd->mbank[1], 0, 0, svga_read_cirrus_linear, NULL, NULL, svga_write_cirrus_linear, NULL, NULL,  NULL, 0, clgd);

        io_sethandler(0x03c0, 0x0020, clgd_in, NULL, NULL, clgd_out, NULL, NULL, clgd);

        svga->hwcursor.yoff = 32;
        svga->hwcursor.xoff = 0;

	if (id < CIRRUS_ID_CLGD5428)
	{
		/* 1 MB */
		clgd->vram_size = (1 << 20);
		clgd->vram_code = 2;

		svga->seqregs[0xf] = 0x18;
		svga->seqregs[0x1f] = 0x22;
	}
	else if ((id >= CIRRUS_ID_CLGD5428) && (id <= CIRRUS_ID_CLGD5436))
	{
		/* 2 MB */
		clgd->vram_size = (1 << 21);
		clgd->vram_code = 3;

		svga->seqregs[0xf] = 0x18;
		svga->seqregs[0x1f] = 0x22;
	}
	else
	{
		/* 4 MB */
		clgd->vram_size = (1 << 22);
		clgd->vram_code = 4;

		svga->seqregs[0xf] = 0x98;
		svga->seqregs[0x1f] = 0x2d;
		svga->seqregs[0x17] = 0x20;
		svga->gdcreg[0x18] = 0xf;
	}

	// Seems the 5436 and 5446 BIOS'es never turn on that bit until it's actually needed,
	// therefore they also don't turn it back off on 640x480x4bpp,
	// therefore, we need to make sure the VRAM mask is correct at start.
	svga->vrammask = (svga->crtc[0x1b] & 2) ? (clgd->vram_size - 1) : 0x3ffff;
	clgd->linear_mmio_mask = (svga->crtc[0x1b] & 2) ? (clgd->vram_size - 256) : (0x40000 - 256);

	svga->seqregs[0x15] = clgd->vram_code;
	if ((id >= CIRRUS_ID_CLGD5422) && (id <= CIRRUS_ID_CLGD5429))  svga->seqregs[0xa] = (clgd->vram_code << 3);

	svga->crtc[0x27] = id;

	svga->gdcreg[0x00] = clgd->shadow_gr0 & 0x0f;
	svga->gdcreg[0x01] = clgd->shadow_gr1 & 0x0f;

	// clgd_recalc_mapping(clgd);
	/* force refresh */
	// cirrus_update_bank_ptr(s, 0);
	// cirrus_update_bank_ptr(s, 1);

	init_rops();

        clgd->bank[1] = 0x8000;
        
        return clgd;
}

void *gd6235_init()
{
	return clgd_common_init("roms/vga6235.rom", CIRRUS_ID_CLGD6235);
}

void *gd5422_init()
{
	return clgd_common_init("roms/CL5422.ROM", CIRRUS_ID_CLGD5422);
}

void *gd5429_init()
{
	return clgd_common_init("roms/5429.vbi", CIRRUS_ID_CLGD5429);
}

void *gd5436_init()
{
	return clgd_common_init("roms/5436.VBI", CIRRUS_ID_CLGD5436);
}

void *gd5446_init()
{
	return clgd_common_init("roms/5446BV.VBI", CIRRUS_ID_CLGD5446);
}

static int gd5422_available()
{
        return rom_present("roms/CL5422.ROM");
}

static int gd5429_available()
{
        return rom_present("roms/5429.vbi");
}

static int gd5436_available()
{
        return rom_present("roms/5436.VBI");
}

static int gd5446_available()
{
        return rom_present("roms/5446BV.VBI");
}

static int gd6235_available()
{
        return rom_present("roms/vga6235.rom");
}

void clgd_close(void *p)
{
        clgd_t *clgd = (clgd_t *)p;

        svga_close(&clgd->svga);
        
        free(clgd);
}

void clgd_speed_changed(void *p)
{
        clgd_t *clgd = (clgd_t *)p;
        
        svga_recalctimings(&clgd->svga);
}

void clgd_force_redraw(void *p)
{
        clgd_t *clgd = (clgd_t *)p;

        clgd->svga.fullchange = changeframecount;
}

void clgd_add_status_info(char *s, int max_len, void *p)
{
        clgd_t *clgd = (clgd_t *)p;
        
        svga_add_status_info(s, max_len, &clgd->svga);
}

device_t gd5422_device =
{
        "Cirrus Logic GD5422",
        // DEVICE_NOT_WORKING,
	0,
        gd5422_init,
        clgd_close,
        gd5422_available,
        clgd_speed_changed,
        clgd_force_redraw,
        clgd_add_status_info
};

device_t gd5429_device =
{
        "Cirrus Logic GD5429",
        // DEVICE_NOT_WORKING,
	0,
        gd5429_init,
        clgd_close,
        gd5429_available,
        clgd_speed_changed,
        clgd_force_redraw,
        clgd_add_status_info
};

device_t gd5436_device =
{
        "Cirrus Logic GD5436",
        // DEVICE_NOT_WORKING,
	0,
        gd5436_init,
        clgd_close,
        gd5436_available,
        clgd_speed_changed,
        clgd_force_redraw,
        clgd_add_status_info
};

device_t gd5446_device =
{
        "Cirrus Logic GD5446",
        // DEVICE_NOT_WORKING,
	0,
        gd5446_init,
        clgd_close,
        gd5446_available,
        clgd_speed_changed,
        clgd_force_redraw,
        clgd_add_status_info
};

device_t gd6235_device =
{
        "Cirrus Logic GD6235",
        // DEVICE_NOT_WORKING,
	0,
        gd6235_init,
        clgd_close,
        gd6235_available,
        clgd_speed_changed,
        clgd_force_redraw,
        clgd_add_status_info
};