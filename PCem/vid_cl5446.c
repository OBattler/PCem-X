/*Cirrus Logic CL-GD5446 emulation*/
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
#include "vid_cl5446.h"
#include "vid_cl5446_blit.h"

void gd5446_start_blit(uint32_t cpu_dat, int count, void *p);

void gd5446_mmio_write(uint32_t addr, uint8_t val, void *p);
uint8_t gd5446_mmio_read(uint32_t addr, void *p);

void gd5446_recalc_banking(gd5446_t *gd5446);
void gd5446_recalc_mapping(gd5446_t *gd5446);

void gd5446_recalctimings(svga_t *svga);

void gd5446_reset_blit(gd5446_t *gd5446, svga_t *svga)
{
	svga->gdcreg[0x31] &= ~(CIRRUS_BLT_START | CIRRUS_BLT_BUSY | CIRRUS_BLT_FIFOUSED);
	gd5446_recalc_banking(gd5446);
}

void gd5446_out(uint16_t addr, uint8_t val, void *p)
{
        gd5446_t *gd5446 = (gd5446_t *)p;
        svga_t *svga = &gd5446->svga;
        uint8_t old;
	uint32_t i;
	FILE *f;
        
        if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1)) 
                addr ^= 0x60;

        pclog("gd5446 out %04X %02X\n", addr, val);
                
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
                if (svga->seqaddr > 5)
                {
                        // svga->seqregs[svga->seqaddr & 0x1f] = val;
                        switch (svga->seqaddr & 0x1f)
                        {
				case 6:
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
                                pclog("svga->hwcursor.x = %i\n", svga->hwcursor.x);
                                break;
                                case 0x11: case 0x31: case 0x51: case 0x71:
                                case 0x91: case 0xb1: case 0xd1: case 0xf1:
				svga->seqregs[0x11] = val;
                                svga->hwcursor.y = (val << 3) | (svga->seqaddr >> 5);
                                pclog("svga->hwcursor.y = %i\n", svga->hwcursor.y);
                                break;
                                case 0x12:
                        	svga->seqregs[svga->seqaddr & 0x1f] = val;
                                svga->hwcursor.ena = val & 1;
                                pclog("svga->hwcursor.ena = %i\n", svga->hwcursor.ena);
                                break;                               
                                case 0x13:
                        	svga->seqregs[svga->seqaddr & 0x1f] = val;
                                svga->hwcursor.addr = 0x1fc000 + ((val & 0x3f) * 256);
                                pclog("svga->hwcursor.addr = %x\n", svga->hwcursor.addr);
                                break;                                
				case 8: case 9: case 0xa: case 0xb: case 0xc: case 0xd: case 0xe: case 0xf:
				case 0x14: case 0x15: case 0x16: case 0x18: case 0x19: case 0x1a: case 0x1b:
				case 0x1c: case 0x1d: case 0x1e: case 0x1f:
                        	svga->seqregs[svga->seqaddr & 0x1f] = val;
				break;
                                
                                case 0x17:
				old = svga->seqregs[svga->seqaddr & 0x1f];
				svga->seqregs[svga->seqaddr & 0x1f] = (svga->seqregs[svga->seqaddr & 0x1f] & 0x38) | (val & 0xc7);
				case 0x7:
                                gd5446_recalc_mapping(gd5446);
				gd5446_recalctimings(svga);
				if (old != val)  svga->bpp = cirrus_get_bpp(gd5446, svga);
                                break;
                        }
                        return;
                }
                break;

                case 0x3C6: case 0x3C7: case 0x3C8: case 0x3C9:
//                pclog("Write RAMDAC %04X %02X %04X:%04X\n", addr, val, CS, pc);
                cl_ramdac_out(addr, val, &gd5446->ramdac, (void *) gd5446, svga);
                return;

                case 0x3cf:
		if (svga->gdcaddr == 0)  gd5446->shadow_gr0 = val;
		if (svga->gdcaddr == 1)  gd5446->shadow_gr1 = val;
                if (svga->gdcaddr == 5)
                {
                        svga->gdcreg[5] = val & 0x7f;
                        if (svga->gdcreg[0xb] & 0x04)
                                svga->writemode = svga->gdcreg[5] & 7;
                        else
                                svga->writemode = svga->gdcreg[5] & 3;
                        svga->readmode = val & 8;
//                        pclog("writemode = %i\n", svga->writemode);
			// gd5446_recalc_mapping(gd5446);
                        return;
                }
                if (svga->gdcaddr == 6)
                {
                        if ((svga->gdcreg[6] & 0xc) != (val & 0xc))
                        {
                                svga->gdcreg[6] = val;
				gd5446_recalc_mapping(gd5446);
                        }
                        svga->gdcreg[6] = val;
                        return;
                }
                if (svga->gdcaddr > 8)
                {
			old = svga->gdcreg[svga->gdcaddr & 0x3f];
                        svga->gdcreg[svga->gdcaddr & 0x3f] = val;
                        switch (svga->gdcaddr)
                        {
#ifdef OLD_CODE
                                case 0x09: case 0x0a: case 0x0b:
                                gd5446_recalc_banking(gd5446);
                                if (svga->gdcreg[0xb] & 0x04)
                                        svga->writemode = svga->gdcreg[5] & 7;
                                else
                                        svga->writemode = svga->gdcreg[5] & 3;
                                break;
				case 0x21: case 0x23: case 0x25: case 0x27:
				svga->gdcreg[svga->gdcaddr & 0x3f] = val & 0x1f;
				break;
				case 0x2a:
				svga->gdcreg[svga->gdcaddr & 0x3f] = val & 0x3f;
				if (svga->gdcreg[0x31] & 0x80)  gd5446_start_blit(0, -1, gd5446);
				break;
				case 0x31:
				gd5446_blt_write_b(addr, val, p);
				break;
#else
				case 0x9: case 0xa: case 0xb:
				svga->gdcreg[svga->gdcaddr & 0x3f] = val;
				// cirrus_update_bank_ptr(gd5446, svga, 0);
				// cirrus_update_bank_ptr(gd5446, svga, 1);
				gd5446_recalc_banking(gd5446);
                                if (svga->gdcreg[0xb] & 0x04)
                                        svga->writemode = svga->gdcreg[5] & 7;
                                else
                                        svga->writemode = svga->gdcreg[5] & 3;
				break;
				case 0x27:
				svga->gdcreg[svga->gdcaddr & 0x3f] = val & 0x1f;
				break;
				case 0x2a:
				svga->gdcreg[svga->gdcaddr & 0x3f] = val & 0x3f;
				/* if auto start mode, starts bit blt now */
				if (svga->gdcreg[0x31] & CIRRUS_BLT_AUTOSTART)  gd5446_start_blit(0, -1, gd5446);
				break;
				case 0x2e:
				svga->gdcreg[svga->gdcaddr & 0x3f] = val & 0x3f;
				break;
				case 0x31:
				// cirrus_write_bitblt(gd5446, svga, val);
				if (((old & CIRRUS_BLT_RESET) != 0) && ((val & CIRRUS_BLT_RESET) == 0))
				{
					// TODO: Bitblt reset
					gd5446_reset_blit(gd5446, svga);
				}
				else if (((old & CIRRUS_BLT_START) == 0) && (val & CIRRUS_BLT_START != 0))
				{
					gd5446_start_blit(0, -1, gd5446);
				}
				break;
				default:
				svga->gdcreg[svga->gdcaddr & 0x3f] = val;
				break;
#endif
                        }                        
                        return;
                }
                break;
                
                case 0x3D4:
                svga->crtcreg = val & 0x3f;
                return;
                case 0x3D5:
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
				svga->vrammask = (val & 2) ? (gd5446->vram_size - 1) : 0x3ffff;
				gd5446->linear_mmio_mask = (val & 2) ? (gd5446->vram_size - 256) : (0x40000 - 256);
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
	if (addr == 0x3C6)  if (old != val)  svga->bpp = cirrus_get_bpp(gd5446, svga);
}

uint8_t gd5446_in(uint16_t addr, void *p)
{
        gd5446_t *gd5446 = (gd5446_t *)p;
        svga_t *svga = &gd5446->svga;

        if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3d0) && !(svga->miscout & 1)) 
                addr ^= 0x60;
        
        if (addr != 0x3da) pclog("IN gd5446 %04X\n", addr);
        
        switch (addr)
        {
                case 0x3c5:
                if (svga->seqaddr > 5)
                {
                        switch (svga->seqaddr)
                        {
				case 0x10: case 0x30: case 0x50: case 0x70:
				case 0x90: case 0xb0: case 0xd0: case 0xf0:
				return svga->seqregs[0x10];
				case 0x11: case 0x31: case 0x51: case 0x71:
				case 0x91: case 0xb1: case 0xd1: case 0xf1:
				return svga->seqregs[0x11];
                                // case 6:
				// wtf!
                                // return ((svga->seqregs[6] & 0x17) == 0x12) ? 0x12 : 0x0f;
				case 5: case 7: case 8: case 9: case 0xa: case 0xb: case 0xc: case 0xd:
				case 0xe: case 0xf: case 0x12: case 0x13: case 0x14: case 0x16: case 0x15: case 0x17:
				case 0x18: case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e: case 0x1f:
#ifdef DEBUG_CIRRUS
				printf("cirrus: handled inport sr_index %02x\n", svga->seqaddr);
#endif
				if (svga->seqaddr == 0x15)  return gd5446->vram_code;
				// goto seq_read_common;
                                // case 0x15:
#ifdef DEBUG_CIRRUS
				// printf("cirrus: handled inport sr_index %02x\n", svga->seqaddr);
#endif
                                // return 4;
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
                return cl_ramdac_in(addr, &gd5446->ramdac, (void *) gd5446, svga);

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
                        // return 0xb8; /*GD5446*/
                }
                return svga->crtc[svga->crtcreg];
        }
        return svga_in(addr, svga);
}

void gd5446_recalc_banking_old(gd5446_t *gd5446)
{
        svga_t *svga = &gd5446->svga;
        
        if (svga->gdcreg[0xb] & 0x20)
                gd5446->bank[0] = (svga->gdcreg[0x09] & 0x7f) << 14;
        else
                gd5446->bank[0] = svga->gdcreg[0x09] << 12;
                                
        if (svga->gdcreg[0xb] & 0x01)
        {
                if (svga->gdcreg[0xb] & 0x20)
                        gd5446->bank[1] = (svga->gdcreg[0x0a] & 0x7f) << 14;
                else
                        gd5446->bank[1] = svga->gdcreg[0x0a] << 12;
        }
        else
                gd5446->bank[1] = gd5446->bank[0] + 0x8000;
}

void gd5446_recalc_banking(gd5446_t *gd5446)
{
        svga_t *svga = &gd5446->svga;

        if (svga->gdcreg[0xb] & 0x01)
        {
		gd5446->bank[0] = svga->gdcreg[0x09];
		gd5446->bank[1] = svga->gdcreg[0x0a];
        }
	else
	{
		gd5446->bank[0] = svga->gdcreg[0x09];
		gd5446->bank[1] = svga->gdcreg[0x09];
	}
        
        if (svga->gdcreg[0xb] & 0x20)
	{
                gd5446->bank[0] <<= 14;
                gd5446->bank[1] <<= 14;
	}
        else
	{
                gd5446->bank[0] <<= 12;
                gd5446->bank[1] <<= 12;
	}
                                
        if (!(svga->gdcreg[0xb] & 0x01))  gd5446->bank[1] += 0x8000;
}

void gd5446_recalc_mapping(gd5446_t *gd5446)
{
        svga_t *svga = &gd5446->svga;
        
        pclog("Write mapping %02X %i\n", svga->gdcreg[6], svga->seqregs[0x17] & 0x04);
        switch (svga->gdcreg[6] & 0x0C)
        {
                case 0x0: /*128k at A0000*/
                mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x20000);
		// Why are we disabling MMIO in 128k mode?
                // mem_mapping_disable(&gd5446->mmio_mapping);
                if (svga->seqregs[0x17] & 0x04)
		{
	                mem_mapping_enable(&gd5446->mmio_mapping);
                        mem_mapping_set_addr(&gd5446->mmio_mapping, 0xb8000, 0x00100);
		}
		else
		{
                	mem_mapping_disable(&gd5446->mmio_mapping);
		}
                svga->banked_mask = 0xffff;
                break;
                case 0x4: /*64k at A0000*/
                mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x10000);
                if (svga->seqregs[0x17] & 0x04)
		{
	                mem_mapping_enable(&gd5446->mmio_mapping);
                        mem_mapping_set_addr(&gd5446->mmio_mapping, 0xb8000, 0x00100);
		}
		else
		{
                	mem_mapping_disable(&gd5446->mmio_mapping);
		}
                svga->banked_mask = 0xffff;
                break;
                case 0x8: /*32k at B0000*/
                mem_mapping_set_addr(&svga->mapping, 0xb0000, 0x08000);
                mem_mapping_disable(&gd5446->mmio_mapping);
                svga->banked_mask = 0x7fff;
                break;
                case 0xC: /*32k at B8000*/
                mem_mapping_set_addr(&svga->mapping, 0xb8000, 0x08000);
                mem_mapping_disable(&gd5446->mmio_mapping);
                svga->banked_mask = 0x7fff;
                break;
        }
}
        
void gd5446_recalctimings(svga_t *svga)
{
        gd5446_t *gd5446 = (gd5446_t *)svga->p;

        if (svga->seqregs[7] & 0x01)
        {
		if (cirrus_get_bpp(gd5446, svga) == 8)  svga->render = svga_render_8bpp_highres;
		if (cirrus_get_bpp(gd5446, svga) == 15)  svga->render = svga_render_15bpp_highres;
		if (cirrus_get_bpp(gd5446, svga) == 16)  svga->render = svga_render_16bpp_highres;
		if (cirrus_get_bpp(gd5446, svga) == 24)  svga->render = svga_render_24bpp_highres;
		if (cirrus_get_bpp(gd5446, svga) == 32)  svga->render = svga_render_32bpp_highres;
	}
        
        svga->ma_latch |= ((svga->crtc[0x1b] & 0x01) << 16) | ((svga->crtc[0x1b] & 0xc) << 15);
        pclog("MA now %05X %02X\n", svga->ma_latch, svga->crtc[0x1b]);
}

void gd5446_hwcursor_draw(svga_t *svga, int displine)
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


void gd5446_write_linear(uint32_t addr, uint8_t val, void *p);

void gd5446_write(uint32_t addr, uint8_t val, void *p)
{
        gd5446_t *gd5446 = (gd5446_t *)p;
        svga_t *svga = &gd5446->svga;
	uint32_t bank_offset;
//        pclog("gd5446_write : %05X %02X  ", addr, val);
        addr &= svga->banked_mask;
	bank_offset = (addr & 0x7fff) >> 15;
	if ((svga->gdcreg[0xb] & 0x14) == 0x14)
	{
		bank_offset <<= 4;
	}
	else if (svga->gdcreg[0xb] & 2)
	{
		bank_offset <<= 3;
	}
	if (!(svga->gdcreg[6] & 1) || !(svga->seqregs[7] & 0x01))
	{
        	addr = (addr & 0x7fff) + gd5446->bank[(addr >> 15) & 1];
	}
	else
	{
        	addr = bank_offset + gd5446->bank[(addr >> 15)];
	}
//        pclog("%08X\n", addr);
        gd5446_write_linear(addr, val, p);
}

uint8_t gd5446_read(uint32_t addr, void *p)
{
        gd5446_t *gd5446 = (gd5446_t *)p;
        svga_t *svga = &gd5446->svga;
	uint32_t bank_offset;
        uint8_t ret;
//        pclog("gd5446_read : %05X ", addr);
        addr &= svga->banked_mask;
	bank_offset = (addr & 0x7fff) >> 15;
	if ((svga->gdcreg[0xb] & 0x14) == 0x14)
	{
		bank_offset <<= 4;
	}
	else if (svga->gdcreg[0xb] & 2)
	{
		bank_offset <<= 3;
	}
	if (!(svga->gdcreg[6] & 1) || !(svga->seqregs[7] & 0x01))
	{
        	addr = (addr & 0x7fff) + gd5446->bank[(addr >> 15) & 1];
	}
	else
	{
        	addr = bank_offset + gd5446->bank[(addr >> 15)];
	}
        ret = svga_read_linear(addr, &gd5446->svga);
//        pclog("%08X %02X\n", addr, ret);  
        return ret;      
}

void gd5446_write_linear(uint32_t addr, uint8_t val, void *p)
{
        gd5446_t *gd5446 = (gd5446_t *)p;
        svga_t *svga = &gd5446->svga;
        uint8_t vala, valb, valc, vald, wm = svga->writemask;
        int writemask2 = svga->writemask;

        cycles -= video_timing_b;
        cycles_lost += video_timing_b;

        egawrites++;

	svga->vram[0] = 0x55;
        
//        if (svga_output) pclog("Write LFB %08X %02X ", addr, val);
        if (!(svga->gdcreg[6] & 1))
                svga->fullchange = 2;
        if (svga->chain4 && (svga->writemode < 4))
        {
                writemask2 = 1 << (addr & 3);
                addr &= ~3;
        }
        else
        {
                addr <<= 2;
        }
        addr &= 0x7fffff;
        if (addr >= svga->vram_limit)
//                return;
		fatal("Address %08X is above the VRAM limit of %08X\n", addr, svga->vram_limit);
//	printf("Linear write at addr %08X, value %02X, write mode %02X\n", addr, val, svga->writemode);
//	pclog("wm2 %08X\n", writemask2);
//        if (svga_output) pclog("%08X\n", addr);
        svga->changedvram[addr >> 12] = changeframecount;

        switch (svga->writemode)
        {
                case 4:
		pclog("qd\n");
                pclog("Writemode 4 : %X ", addr);
                addr <<= 1;
                svga->changedvram[addr >> 12] = changeframecount;
                pclog("%X %X\n", addr, val);
		if (cirrus_get_bpp(gd5446, svga) == 8)
		{
                	if (val & 0x80)
                        	svga->vram[addr + 0] = svga->gdcreg[1];
                	if (val & 0x40)
                        	svga->vram[addr + 1] = svga->gdcreg[1];
                	if (val & 0x20)
                        	svga->vram[addr + 2] = svga->gdcreg[1];
                	if (val & 0x10)
                        	svga->vram[addr + 3] = svga->gdcreg[1];
                	if (val & 0x08)
                        	svga->vram[addr + 4] = svga->gdcreg[1];
                	if (val & 0x04)
                        	svga->vram[addr + 5] = svga->gdcreg[1];
                	if (val & 0x02)
                        	svga->vram[addr + 6] = svga->gdcreg[1];
                	if (val & 0x01)
                        	svga->vram[addr + 7] = svga->gdcreg[1];
		}
		else if (cirrus_get_bpp(gd5446, svga) == 16)
		{
                	if (val & 0x80)
			{
                        	svga->vram[addr + 0] = svga->gdcreg[1];
                        	svga->vram[addr + 1] = svga->gdcreg[0x11];
			}
                	if (val & 0x40)
			{
                        	svga->vram[addr + 2] = svga->gdcreg[1];
                        	svga->vram[addr + 3] = svga->gdcreg[0x11];
			}
                	if (val & 0x20)
			{
                        	svga->vram[addr + 4] = svga->gdcreg[1];
                        	svga->vram[addr + 5] = svga->gdcreg[0x11];
			}
                	if (val & 0x10)
			{
                        	svga->vram[addr + 6] = svga->gdcreg[1];
                        	svga->vram[addr + 7] = svga->gdcreg[0x11];
			}
                	if (val & 0x08)
			{
                        	svga->vram[addr + 8] = svga->gdcreg[1];
                        	svga->vram[addr + 9] = svga->gdcreg[0x11];
			}
                	if (val & 0x04)
			{
                        	svga->vram[addr + 0xa] = svga->gdcreg[1];
                        	svga->vram[addr + 0xb] = svga->gdcreg[0x11];
			}
                	if (val & 0x02)
			{
                        	svga->vram[addr + 0xc] = svga->gdcreg[1];
                        	svga->vram[addr + 0xd] = svga->gdcreg[0x11];
			}
                	if (val & 0x01)
			{
                        	svga->vram[addr + 0xe] = svga->gdcreg[1];
                        	svga->vram[addr + 0xf] = svga->gdcreg[0x11];
			}
		}
                break;
                        
                case 5:
		pclog("wt\n");
                pclog("Writemode 5 : %X ", addr);
                addr <<= 1;
                svga->changedvram[addr >> 12] = changeframecount;
                pclog("%X %X\n", addr, val);
		if (cirrus_get_bpp(gd5446, svga) == 8)
		{
                	svga->vram[addr + 0] = (val & 0x80) ? svga->gdcreg[1] : svga->gdcreg[0];
                	svga->vram[addr + 1] = (val & 0x40) ? svga->gdcreg[1] : svga->gdcreg[0];
                	svga->vram[addr + 2] = (val & 0x20) ? svga->gdcreg[1] : svga->gdcreg[0];
                	svga->vram[addr + 3] = (val & 0x10) ? svga->gdcreg[1] : svga->gdcreg[0];
                	svga->vram[addr + 4] = (val & 0x08) ? svga->gdcreg[1] : svga->gdcreg[0];
                	svga->vram[addr + 5] = (val & 0x04) ? svga->gdcreg[1] : svga->gdcreg[0];
                	svga->vram[addr + 6] = (val & 0x02) ? svga->gdcreg[1] : svga->gdcreg[0];
                	svga->vram[addr + 7] = (val & 0x01) ? svga->gdcreg[1] : svga->gdcreg[0];
		}
		else
		{
                	svga->vram[addr + 0] = (val & 0x80) ? svga->gdcreg[1] : svga->gdcreg[0];
                	svga->vram[addr + 1] = (val & 0x80) ? svga->gdcreg[0x11] : svga->gdcreg[0x10];
                	svga->vram[addr + 2] = (val & 0x40) ? svga->gdcreg[1] : svga->gdcreg[0];
                	svga->vram[addr + 3] = (val & 0x40) ? svga->gdcreg[0x11] : svga->gdcreg[0x10];
                	svga->vram[addr + 4] = (val & 0x20) ? svga->gdcreg[1] : svga->gdcreg[0];
                	svga->vram[addr + 5] = (val & 0x20) ? svga->gdcreg[0x11] : svga->gdcreg[0x10];
                	svga->vram[addr + 6] = (val & 0x10) ? svga->gdcreg[1] : svga->gdcreg[0];
                	svga->vram[addr + 7] = (val & 0x10) ? svga->gdcreg[0x11] : svga->gdcreg[0x10];
                	svga->vram[addr + 8] = (val & 0x08) ? svga->gdcreg[1] : svga->gdcreg[0];
                	svga->vram[addr + 9] = (val & 0x08) ? svga->gdcreg[0x11] : svga->gdcreg[0x10];
                	svga->vram[addr + 0xa] = (val & 0x04) ? svga->gdcreg[1] : svga->gdcreg[0];
                	svga->vram[addr + 0xb] = (val & 0x04) ? svga->gdcreg[0x11] : svga->gdcreg[0x10];
                	svga->vram[addr + 0xc] = (val & 0x02) ? svga->gdcreg[1] : svga->gdcreg[0];
                	svga->vram[addr + 0xd] = (val & 0x02) ? svga->gdcreg[0x11] : svga->gdcreg[0x10];
                	svga->vram[addr + 0xe] = (val & 0x01) ? svga->gdcreg[1] : svga->gdcreg[0];
                	svga->vram[addr + 0xf] = (val & 0x01) ? svga->gdcreg[0x11] : svga->gdcreg[0x10];
		}
                break;
                
                case 1:
		pclog("ug\n");
                if (writemask2 & 1) svga->vram[addr]       = svga->la;
                if (writemask2 & 2) svga->vram[addr | 0x1] = svga->lb;
                if (writemask2 & 4) svga->vram[addr | 0x2] = svga->lc;
                if (writemask2 & 8) svga->vram[addr | 0x3] = svga->ld;
                break;
                case 0:
		pclog("ov %02X\n", val);
                if (svga->gdcreg[3] & 7) 
                        val = svga_rotate[svga->gdcreg[3] & 7][val];
                if (svga->gdcreg[8] == 0xff && !(svga->gdcreg[3] & 0x18) && !svga->gdcreg[1])
                {
			pclog("zx %02X\n", val);
                        if (writemask2 & 1) svga->vram[addr]       = val;
                        if (writemask2 & 2) svga->vram[addr | 0x1] = val;
                        if (writemask2 & 4) svga->vram[addr | 0x2] = val;
                        if (writemask2 & 8) svga->vram[addr | 0x3] = val;
                }
                else
                {
			pclog("zy\n");
                        if (svga->gdcreg[1] & 1) vala = (svga->gdcreg[0] & 1) ? 0xff : 0;
                        else                     vala = val;
                        if (svga->gdcreg[1] & 2) valb = (svga->gdcreg[0] & 2) ? 0xff : 0;
                        else                     valb = val;
                        if (svga->gdcreg[1] & 4) valc = (svga->gdcreg[0] & 4) ? 0xff : 0;
                        else                     valc = val;
                        if (svga->gdcreg[1] & 8) vald = (svga->gdcreg[0] & 8) ? 0xff : 0;
                        else                     vald = val;

                        switch (svga->gdcreg[3] & 0x18)
                        {
                                case 0: /*Set*/
                                if (writemask2 & 1) svga->vram[addr]       = (vala & svga->gdcreg[8]) | (svga->la & ~svga->gdcreg[8]);
                                if (writemask2 & 2) svga->vram[addr | 0x1] = (valb & svga->gdcreg[8]) | (svga->lb & ~svga->gdcreg[8]);
                                if (writemask2 & 4) svga->vram[addr | 0x2] = (valc & svga->gdcreg[8]) | (svga->lc & ~svga->gdcreg[8]);
                                if (writemask2 & 8) svga->vram[addr | 0x3] = (vald & svga->gdcreg[8]) | (svga->ld & ~svga->gdcreg[8]);
                                break;
                                case 8: /*AND*/
                                if (writemask2 & 1) svga->vram[addr]       = (vala | ~svga->gdcreg[8]) & svga->la;
                                if (writemask2 & 2) svga->vram[addr | 0x1] = (valb | ~svga->gdcreg[8]) & svga->lb;
                                if (writemask2 & 4) svga->vram[addr | 0x2] = (valc | ~svga->gdcreg[8]) & svga->lc;
                                if (writemask2 & 8) svga->vram[addr | 0x3] = (vald | ~svga->gdcreg[8]) & svga->ld;
                                break;
                                case 0x10: /*OR*/
                                if (writemask2 & 1) svga->vram[addr]       = (vala & svga->gdcreg[8]) | svga->la;
                                if (writemask2 & 2) svga->vram[addr | 0x1] = (valb & svga->gdcreg[8]) | svga->lb;
                                if (writemask2 & 4) svga->vram[addr | 0x2] = (valc & svga->gdcreg[8]) | svga->lc;
                                if (writemask2 & 8) svga->vram[addr | 0x3] = (vald & svga->gdcreg[8]) | svga->ld;
                                break;
                                case 0x18: /*XOR*/
                                if (writemask2 & 1) svga->vram[addr]       = (vala & svga->gdcreg[8]) ^ svga->la;
                                if (writemask2 & 2) svga->vram[addr | 0x1] = (valb & svga->gdcreg[8]) ^ svga->lb;
                                if (writemask2 & 4) svga->vram[addr | 0x2] = (valc & svga->gdcreg[8]) ^ svga->lc;
                                if (writemask2 & 8) svga->vram[addr | 0x3] = (vald & svga->gdcreg[8]) ^ svga->ld;
                                break;
                        }
//                        pclog("- %02X %02X %02X %02X   %08X\n",vram[addr],vram[addr|0x1],vram[addr|0x2],vram[addr|0x3],addr);
                }
                break;
                case 2:
		pclog("yf\n");
                if (!(svga->gdcreg[3] & 0x18) && !svga->gdcreg[1])
                {
                        if (writemask2 & 1) svga->vram[addr]       = (((val & 1) ? 0xff : 0) & svga->gdcreg[8]) | (svga->la & ~svga->gdcreg[8]);
                        if (writemask2 & 2) svga->vram[addr | 0x1] = (((val & 2) ? 0xff : 0) & svga->gdcreg[8]) | (svga->lb & ~svga->gdcreg[8]);
                        if (writemask2 & 4) svga->vram[addr | 0x2] = (((val & 4) ? 0xff : 0) & svga->gdcreg[8]) | (svga->lc & ~svga->gdcreg[8]);
                        if (writemask2 & 8) svga->vram[addr | 0x3] = (((val & 8) ? 0xff : 0) & svga->gdcreg[8]) | (svga->ld & ~svga->gdcreg[8]);
                }
                else
                {
                        vala = ((val & 1) ? 0xff : 0);
                        valb = ((val & 2) ? 0xff : 0);
                        valc = ((val & 4) ? 0xff : 0);
                        vald = ((val & 8) ? 0xff : 0);
                        switch (svga->gdcreg[3] & 0x18)
                        {
                                case 0: /*Set*/
                                if (writemask2 & 1) svga->vram[addr]       = (vala & svga->gdcreg[8]) | (svga->la & ~svga->gdcreg[8]);
                                if (writemask2 & 2) svga->vram[addr | 0x1] = (valb & svga->gdcreg[8]) | (svga->lb & ~svga->gdcreg[8]);
                                if (writemask2 & 4) svga->vram[addr | 0x2] = (valc & svga->gdcreg[8]) | (svga->lc & ~svga->gdcreg[8]);
                                if (writemask2 & 8) svga->vram[addr | 0x3] = (vald & svga->gdcreg[8]) | (svga->ld & ~svga->gdcreg[8]);
                                break;
                                case 8: /*AND*/
                                if (writemask2 & 1) svga->vram[addr]       = (vala | ~svga->gdcreg[8]) & svga->la;
                                if (writemask2 & 2) svga->vram[addr | 0x1] = (valb | ~svga->gdcreg[8]) & svga->lb;
                                if (writemask2 & 4) svga->vram[addr | 0x2] = (valc | ~svga->gdcreg[8]) & svga->lc;
                                if (writemask2 & 8) svga->vram[addr | 0x3] = (vald | ~svga->gdcreg[8]) & svga->ld;
                                break;
                                case 0x10: /*OR*/
                                if (writemask2 & 1) svga->vram[addr]       = (vala & svga->gdcreg[8]) | svga->la;
                                if (writemask2 & 2) svga->vram[addr | 0x1] = (valb & svga->gdcreg[8]) | svga->lb;
                                if (writemask2 & 4) svga->vram[addr | 0x2] = (valc & svga->gdcreg[8]) | svga->lc;
                                if (writemask2 & 8) svga->vram[addr | 0x3] = (vald & svga->gdcreg[8]) | svga->ld;
                                break;
                                case 0x18: /*XOR*/
                                if (writemask2 & 1) svga->vram[addr]       = (vala & svga->gdcreg[8]) ^ svga->la;
                                if (writemask2 & 2) svga->vram[addr | 0x1] = (valb & svga->gdcreg[8]) ^ svga->lb;
                                if (writemask2 & 4) svga->vram[addr | 0x2] = (valc & svga->gdcreg[8]) ^ svga->lc;
                                if (writemask2 & 8) svga->vram[addr | 0x3] = (vald & svga->gdcreg[8]) ^ svga->ld;
                                break;
                        }
                }
                break;
                case 3:
		pclog("mp\n");
                if (svga->gdcreg[3] & 7) 
                        val = svga_rotate[svga->gdcreg[3] & 7][val];
                wm = svga->gdcreg[8];
                svga->gdcreg[8] &= val;

                vala = (svga->gdcreg[0] & 1) ? 0xff : 0;
                valb = (svga->gdcreg[0] & 2) ? 0xff : 0;
                valc = (svga->gdcreg[0] & 4) ? 0xff : 0;
                vald = (svga->gdcreg[0] & 8) ? 0xff : 0;
                switch (svga->gdcreg[3] & 0x18)
                {
                        case 0: /*Set*/
                        if (writemask2 & 1) svga->vram[addr]       = (vala & svga->gdcreg[8]) | (svga->la & ~svga->gdcreg[8]);
                        if (writemask2 & 2) svga->vram[addr | 0x1] = (valb & svga->gdcreg[8]) | (svga->lb & ~svga->gdcreg[8]);
                        if (writemask2 & 4) svga->vram[addr | 0x2] = (valc & svga->gdcreg[8]) | (svga->lc & ~svga->gdcreg[8]);
                        if (writemask2 & 8) svga->vram[addr | 0x3] = (vald & svga->gdcreg[8]) | (svga->ld & ~svga->gdcreg[8]);
                        break;
                        case 8: /*AND*/
                        if (writemask2 & 1) svga->vram[addr]       = (vala | ~svga->gdcreg[8]) & svga->la;
                        if (writemask2 & 2) svga->vram[addr | 0x1] = (valb | ~svga->gdcreg[8]) & svga->lb;
                        if (writemask2 & 4) svga->vram[addr | 0x2] = (valc | ~svga->gdcreg[8]) & svga->lc;
                        if (writemask2 & 8) svga->vram[addr | 0x3] = (vald | ~svga->gdcreg[8]) & svga->ld;
                        break;
                        case 0x10: /*OR*/
                        if (writemask2 & 1) svga->vram[addr]       = (vala & svga->gdcreg[8]) | svga->la;
                        if (writemask2 & 2) svga->vram[addr | 0x1] = (valb & svga->gdcreg[8]) | svga->lb;
                        if (writemask2 & 4) svga->vram[addr | 0x2] = (valc & svga->gdcreg[8]) | svga->lc;
                        if (writemask2 & 8) svga->vram[addr | 0x3] = (vald & svga->gdcreg[8]) | svga->ld;
                        break;
                        case 0x18: /*XOR*/
                        if (writemask2 & 1) svga->vram[addr]       = (vala & svga->gdcreg[8]) ^ svga->la;
                        if (writemask2 & 2) svga->vram[addr | 0x1] = (valb & svga->gdcreg[8]) ^ svga->lb;
                        if (writemask2 & 4) svga->vram[addr | 0x2] = (valc & svga->gdcreg[8]) ^ svga->lc;
                        if (writemask2 & 8) svga->vram[addr | 0x3] = (vald & svga->gdcreg[8]) ^ svga->ld;
                        break;
                }
                svga->gdcreg[8] = wm;
                break;
		default:
		fatal("Unknown write mode: %02X\n", svga->writemode);
        }
}

void gd5446_start_blit(uint32_t cpu_dat, int count, void *p)
{
        gd5446_t *gd5446 = (gd5446_t *)p;
        svga_t *svga = &gd5446->svga;

        pclog("gd5446_start_blit %i\n", count);
        if (count == -1)
        {
                gd5446->blt.dst_addr_backup = gd5446->blt.dst_addr;
                gd5446->blt.src_addr_backup = gd5446->blt.src_addr;
                gd5446->blt.width_backup    = gd5446->blt.width;
                gd5446->blt.height_internal = gd5446->blt.height;
                gd5446->blt.x_count         = gd5446->blt.mask & 7;
                pclog("gd5446_start_blit : size %i, %i\n", gd5446->blt.width, gd5446->blt.height);
                
                if (gd5446->blt.mode & 0x04)
                {
//                        pclog("blt.mode & 0x04\n");
                        mem_mapping_set_handler(&svga->mapping, NULL, NULL, NULL, gd5446_blt_write_b, gd5446_blt_write_w, gd5446_blt_write_l);
                        mem_mapping_set_p(&svga->mapping, gd5446);
                        gd5446_recalc_mapping(gd5446);
                        return;
                }
                else
                {
                        mem_mapping_set_handler(&gd5446->svga.mapping, gd5446_read, NULL, NULL, gd5446_write, NULL, NULL);
                        mem_mapping_set_p(&gd5446->svga.mapping, gd5446);
                        gd5446_recalc_mapping(gd5446);
                        return;
                }                
        }
        
	// printf("Blit, CPU Data %08X, write mode %02X\n", cpu_dat, svga->writemode);
        while (count)
        {
                uint8_t src, dst;
                int mask;
                
                if (gd5446->blt.mode & 0x04)
                {
                        if (gd5446->blt.mode & 0x80)
                        {
                                src = (cpu_dat & 0x80) ? gd5446->blt.fg_col : gd5446->blt.bg_col;
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
                        switch (gd5446->blt.mode & 0xc0)
                        {
                                case 0x00:
                                src = svga->vram[gd5446->blt.src_addr & svga->vrammask];
                                gd5446->blt.src_addr += ((gd5446->blt.mode & 0x01) ? -1 : 1);
                                mask = 1;
                                break;
                                case 0x40:
                                src = svga->vram[(gd5446->blt.src_addr & (svga->vrammask & ~7)) | (gd5446->blt.dst_addr & 7)];
                                mask = 1;
                                break;
                                case 0x80:
                                mask = svga->vram[gd5446->blt.src_addr & svga->vrammask] & (0x80 >> gd5446->blt.x_count);
                                src = mask ? gd5446->blt.fg_col : gd5446->blt.bg_col;
                                gd5446->blt.x_count++;
                                if (gd5446->blt.x_count == 8)
                                {
                                        gd5446->blt.x_count = 0;
                                        gd5446->blt.src_addr++;
                                }
                                break;
                                case 0xc0:
                                mask = svga->vram[gd5446->blt.src_addr & svga->vrammask] & (0x80 >> (gd5446->blt.dst_addr & 7));
                                src = mask ? gd5446->blt.fg_col : gd5446->blt.bg_col;
                                break;
                        }
                        count--;                        
                }
                dst = svga->vram[gd5446->blt.dst_addr & svga->vrammask];
                svga->changedvram[(gd5446->blt.dst_addr & svga->vrammask) >> 12] = changeframecount;
               
                pclog("Blit %i,%i %06X %06X  %06X %02X %02X  %02X %02X ", gd5446->blt.width, gd5446->blt.height_internal, gd5446->blt.src_addr, gd5446->blt.dst_addr, gd5446->blt.src_addr & svga->vrammask, svga->vram[gd5446->blt.src_addr & svga->vrammask], 0x80 >> (gd5446->blt.dst_addr & 7), src, dst);
                switch (gd5446->blt.rop)
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
                
                if ((gd5446->blt.width_backup - gd5446->blt.width) >= (gd5446->blt.mask & 7) &&
                    !((gd5446->blt.mode & 0x08) && !mask))
                        svga->vram[gd5446->blt.dst_addr & svga->vrammask] = dst;
                
                gd5446->blt.dst_addr += ((gd5446->blt.mode & 0x01) ? -1 : 1);
                
                gd5446->blt.width--;
                
                if (gd5446->blt.width == 0xffff)
                {
                        gd5446->blt.width = gd5446->blt.width_backup;

                        gd5446->blt.dst_addr = gd5446->blt.dst_addr_backup = gd5446->blt.dst_addr_backup + ((gd5446->blt.mode & 0x01) ? -gd5446->blt.dst_pitch : gd5446->blt.dst_pitch);
                        
                        switch (gd5446->blt.mode & 0xc0)
                        {
                                case 0x00:
                                gd5446->blt.src_addr = gd5446->blt.src_addr_backup = gd5446->blt.src_addr_backup + ((gd5446->blt.mode & 0x01) ? -gd5446->blt.src_pitch : gd5446->blt.src_pitch);
                                break;
                                case 0x40:
                                gd5446->blt.src_addr = ((gd5446->blt.src_addr + ((gd5446->blt.mode & 0x01) ? -8 : 8)) & 0x38) | (gd5446->blt.src_addr & ~0x38);
                                break;
                                case 0x80:
                                if (gd5446->blt.x_count != 0)
                                {
                                        gd5446->blt.x_count = 0;
                                        gd5446->blt.src_addr++;
                                }
                                break;
                                case 0xc0:
                                gd5446->blt.src_addr = ((gd5446->blt.src_addr + ((gd5446->blt.mode & 0x01) ? -1 : 1)) & 7) | (gd5446->blt.src_addr & ~7);
                                break;
                        }
                        
                        gd5446->blt.height_internal--;
                        if (gd5446->blt.height_internal == 0xffff)
                        {
                                if (gd5446->blt.mode & 0x04)
                                {
                                        mem_mapping_set_handler(&gd5446->svga.mapping, gd5446_read, NULL, NULL, gd5446_write, NULL, NULL);
                                        mem_mapping_set_p(&gd5446->svga.mapping, gd5446);
                                        gd5446_recalc_mapping(gd5446);
                                }
                                return;
                        }
                                
                        if (gd5446->blt.mode & 0x04)
                                return;
                }                        
        }
}

void gd5446_mmio_write(uint32_t addr, uint8_t val, void *p)
{
        gd5446_t *gd5446 = (gd5446_t *)p;
	svga_t *svga = &gd5446->svga;

	svga->vram[gd5446->vram_size - 256 + (addr & 0xff)] = val;

        pclog("MMIO write %08X %02X\n", addr, val);
        switch (addr & 0xff)
        {
                case 0x00:
                gd5446->blt.bg_col = (gd5446->blt.bg_col & 0xff00) | val;
                break;
                case 0x01:
                gd5446->blt.bg_col = (gd5446->blt.bg_col & 0x00ff) | (val << 8);
                break;

                case 0x04:
                gd5446->blt.fg_col = (gd5446->blt.fg_col & 0xff00) | val;
                break;
                case 0x05:
                gd5446->blt.fg_col = (gd5446->blt.fg_col & 0x00ff) | (val << 8);
                break;

                case 0x08:
                gd5446->blt.width = (gd5446->blt.width & 0xff00) | val;
                break;
                case 0x09:
                gd5446->blt.width = (gd5446->blt.width & 0x00ff) | (val << 8);
                break;
                case 0x0a:
                gd5446->blt.height = (gd5446->blt.height & 0xff00) | val;
                break;
                case 0x0b:
                gd5446->blt.height = (gd5446->blt.height & 0x00ff) | (val << 8);
                break;
                case 0x0c:
                gd5446->blt.dst_pitch = (gd5446->blt.dst_pitch & 0xff00) | val;
                break;
                case 0x0d:
                gd5446->blt.dst_pitch = (gd5446->blt.dst_pitch & 0x00ff) | (val << 8);
                break;
                case 0x0e:
                gd5446->blt.src_pitch = (gd5446->blt.src_pitch & 0xff00) | val;
                break;
                case 0x0f:
                gd5446->blt.src_pitch = (gd5446->blt.src_pitch & 0x00ff) | (val << 8);
                break;
                
                case 0x10:
                gd5446->blt.dst_addr = (gd5446->blt.dst_addr & 0xffffff00) | val;
                break;
                case 0x11:
                gd5446->blt.dst_addr = (gd5446->blt.dst_addr & 0xffff00ff) | (val << 8);
                break;
                case 0x12:
                gd5446->blt.dst_addr = (gd5446->blt.dst_addr & 0xff00ffff) | (val << 16);
                break;

                case 0x14:
                gd5446->blt.src_addr = (gd5446->blt.src_addr & 0xffffff00) | val;
                break;
                case 0x15:
                gd5446->blt.src_addr = (gd5446->blt.src_addr & 0xffff00ff) | (val << 8);
                break;
                case 0x16:
                gd5446->blt.src_addr = (gd5446->blt.src_addr & 0xff00ffff) | (val << 16);
                break;

                case 0x17:
                gd5446->blt.mask = val;
                break;
                case 0x18:
                gd5446->blt.mode = val;
                break;
                
                case 0x1a:
                gd5446->blt.rop = val;
                break;

		case 0x1b:
		gd5446->blt.modeext = val;
		break;

		case 0x1c:
		gd5446->blt.blttc = (gd5446->blt.blttc & 0xffffff00) | val;
		break;
		case 0x1d:
		gd5446->blt.blttc = (gd5446->blt.blttc & 0xffff00ff) | (val << 8);
		break;
		case 0x1e:
		gd5446->blt.blttc = (gd5446->blt.blttc & 0xff00ffff) | (val << 16);
		break;
		case 0x1f:
		gd5446->blt.blttc = (gd5446->blt.blttc & 0x00ffffff) | (val << 24);
		break;
                
		case 0x20:
		gd5446->blt.blttcmask = (gd5446->blt.blttcmask & 0xffffff00) | val;
		break;
		case 0x21:
		gd5446->blt.blttcmask = (gd5446->blt.blttcmask & 0xffff00ff) | (val << 8);
		break;
		case 0x22:
		gd5446->blt.blttcmask = (gd5446->blt.blttcmask & 0xff00ffff) | (val << 16);
		break;
		case 0x23:
		gd5446->blt.blttcmask = (gd5446->blt.blttcmask & 0x00ffffff) | (val << 24);
		break;
                
		case 0x24:
		gd5446->blt.ld_start_x = (gd5446->blt.ld_start_x & 0x00ff) | val;
		break;
		case 0x25:
		gd5446->blt.ld_start_x = (gd5446->blt.ld_start_x & 0xff00) | (val << 8);
		break;
                
		case 0x26:
		gd5446->blt.ld_start_y = (gd5446->blt.ld_start_y & 0x00ff) | val;
		break;
		case 0x27:
		gd5446->blt.ld_start_y = (gd5446->blt.ld_start_y & 0xff00) | (val << 8);
		break;
                
		case 0x28:
		gd5446->blt.ld_end_x = (gd5446->blt.ld_end_x & 0x00ff) | val;
		break;
		case 0x29:
		gd5446->blt.ld_end_x = (gd5446->blt.ld_end_x & 0xff00) | (val << 8);
		break;
                
		case 0x2a:
		gd5446->blt.ld_end_y = (gd5446->blt.ld_end_y & 0x00ff) | val;
		break;
		case 0x2b:
		gd5446->blt.ld_end_y = (gd5446->blt.ld_end_y & 0xff00) | (val << 8);
		break;

		case 0x2c:
		gd5446->blt.ld_ls_inc = val;
                
		case 0x2d:
		gd5446->blt.ld_ls_ro = val;
                
		case 0x2e:
		gd5446->blt.ld_ls_mask = val;
                
		case 0x2f:
		gd5446->blt.ld_ls_ac = val;
                
		case 0x30:
		gd5446->blt.bres_k1 = (gd5446->blt.bres_k1 & 0x00ff) | val;
		break;
		case 0x31:
		gd5446->blt.bres_k1 = (gd5446->blt.bres_k1 & 0xff00) | (val << 8);
		break;

		case 0x32:
		gd5446->blt.bres_k3 = (gd5446->blt.bres_k3 & 0x00ff) | val;
		break;
		case 0x33:
		gd5446->blt.bres_k3 = (gd5446->blt.bres_k3 & 0xff00) | (val << 8);
		break;

		case 0x34:
		gd5446->blt.bres_err = (gd5446->blt.bres_err & 0x00ff) | val;
		break;
		case 0x35:
		gd5446->blt.bres_err = (gd5446->blt.bres_err & 0xff00) | (val << 8);
		break;

		case 0x36:
		gd5446->blt.bres_dm = (gd5446->blt.bres_dm & 0x00ff) | val;
		break;
		case 0x37:
		gd5446->blt.bres_dm = (gd5446->blt.bres_dm & 0xff00) | (val << 8);
		break;

		case 0x38:
		gd5446->blt.bres_dir = val;
                
		case 0x39:
		gd5446->blt.ld_mode = val;
                
                case 0x40:
		gd5446->blt.blt_status = val;
                if (val & 0x02)
                        gd5446_start_blit(0, -1, gd5446);
                break;
        }
}

uint8_t gd5446_mmio_read(uint32_t addr, void *p)
{
        gd5446_t *gd5446 = (gd5446_t *)p;
	svga_t *svga = &gd5446->svga;

        pclog("MMIO read %08X\n", addr);
        switch (addr & 0xff)
        {
                case 0x40: /*BLT status*/
                return 0;
        }
	return svga->vram[gd5446->vram_size - 256 + (addr & 0xff)];
        // return 0xff; /*All other registers read-only*/
}

void gd5446_blt_write_b(uint32_t addr, uint8_t val, void *p)
{
        pclog("gd5446_blt_write_w %08X %08X\n", addr, val);
        gd5446_start_blit(val, 8, p);
}

void gd5446_blt_write_w(uint32_t addr, uint16_t val, void *p)
{
        pclog("gd5446_blt_write_w %08X %08X\n", addr, val);
        gd5446_start_blit(val, 16, p);
}

void gd5446_blt_write_l(uint32_t addr, uint32_t val, void *p)
{
        gd5446_t *gd5446 = (gd5446_t *)p;
        
        pclog("gd5446_blt_write_l %08X %08X  %04X %04X\n", addr, val,  ((val >> 8) & 0x00ff) | ((val << 8) & 0xff00), ((val >> 24) & 0x00ff) | ((val >> 8) & 0xff00));
        if ((gd5446->blt.mode & 0x84) == 0x84)
        {
                gd5446_start_blit( val        & 0xff, 8, p);
                gd5446_start_blit((val >> 8)  & 0xff, 8, p);
                gd5446_start_blit((val >> 16) & 0xff, 8, p);
                gd5446_start_blit((val >> 24) & 0xff, 8, p);
        }
        else
                gd5446_start_blit(val, 32, p);
}

void *gd5436_init()
{
        // gd5446_t *gd5446 = malloc(sizeof(gd5446_t));
        gd5446 = malloc(sizeof(gd5446_t));
        svga_t *svga = &gd5446->svga;
        memset(gd5446, 0, sizeof(gd5446_t));

        rom_init(&gd5446->bios_rom, "roms/5436.VBI", 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
        
        svga_init(&gd5446->svga, gd5446, 1 << 21, /*2mb*/
                   gd5446_recalctimings,
                   gd5446_in, gd5446_out,
                   gd5446_hwcursor_draw,
                   NULL);

        mem_mapping_set_handler(&gd5446->svga.mapping, gd5446_read, NULL, NULL, gd5446_write, NULL, NULL);
        mem_mapping_set_p(&gd5446->svga.mapping, gd5446);

        mem_mapping_add(&gd5446->mmio_mapping, 0, 0, gd5446_mmio_read, NULL, NULL, gd5446_mmio_write, NULL, NULL,  NULL, 0, gd5446);

        io_sethandler(0x03c0, 0x0020, gd5446_in, NULL, NULL, gd5446_out, NULL, NULL, gd5446);

        svga->hwcursor.yoff = 32;
        svga->hwcursor.xoff = 0;

	gd5446->vram_size = (1 << 21);
	gd5446->vram_code = 3;

	// Seems the 5436 and 5446 BIOS'es never turn on that bit until it's actually needed,
	// therefore they also don't turn it back off on 640x480x4bpp,
	// therefore, we need to make sure the VRAM mask is correct at start.
	svga->vrammask = (svga->crtc[0x1b] & 2) ? (gd5446->vram_size - 1) : 0x3ffff;
	gd5446->linear_mmio_mask = (svga->crtc[0x1b] & 2) ? (gd5446->vram_size - 256) : (0x40000 - 256);

	svga->seqregs[0x1f] = 0x2d;
	svga->gdcreg[0x18] = 0xf;
	svga->seqregs[0xf] = 0x98;
	svga->seqregs[0x17] = 0x20;
	svga->seqregs[0x15] = 3;

	svga->crtc[0x27] = CIRRUS_ID_CLGD5436;

	svga->gdcreg[0x00] = gd5446->shadow_gr0 & 0x0f;
	svga->gdcreg[0x01] = gd5446->shadow_gr1 & 0x0f;

	// gd5446_recalc_mapping(gd5446);
	/* force refresh */
	// cirrus_update_bank_ptr(s, 0);
	// cirrus_update_bank_ptr(s, 1);

	init_rops();

        gd5446->bank[1] = 0x8000;
        
        return gd5446;
}

void *gd5446_init()
{
        // gd5446_t *gd5446 = malloc(sizeof(gd5446_t));
        gd5446 = malloc(sizeof(gd5446_t));
        svga_t *svga = &gd5446->svga;
        memset(gd5446, 0, sizeof(gd5446_t));

        rom_init(&gd5446->bios_rom, "roms/5446BV.VBI", 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
        
        svga_init(&gd5446->svga, gd5446, 1 << 22, /*4mb*/
                   gd5446_recalctimings,
                   gd5446_in, gd5446_out,
                   gd5446_hwcursor_draw,
                   NULL);

        mem_mapping_set_handler(&gd5446->svga.mapping, gd5446_read, NULL, NULL, gd5446_write, NULL, NULL);
        mem_mapping_set_p(&gd5446->svga.mapping, gd5446);

        mem_mapping_add(&gd5446->mmio_mapping, 0xb8000, 0x100, gd5446_mmio_read, NULL, NULL, gd5446_mmio_write, NULL, NULL,  NULL, 0, gd5446);

        io_sethandler(0x03c0, 0x0020, gd5446_in, NULL, NULL, gd5446_out, NULL, NULL, gd5446);

        svga->hwcursor.yoff = 32;
        svga->hwcursor.xoff = 0;

	gd5446->vram_size = (1 << 22);
	gd5446->vram_code = 4;

	// Seems the 5436 and 5446 BIOS'es never turn on that bit until it's actually needed,
	// therefore they also don't turn it back off on 640x480x4bpp,
	// therefore, we need to make sure the VRAM mask is correct at start.
	svga->vrammask = (svga->crtc[0x1b] & 2) ? (gd5446->vram_size - 1) : 0x3ffff;
	gd5446->linear_mmio_mask = (svga->crtc[0x1b] & 2) ? (gd5446->vram_size - 256) : (0x40000 - 256);

	svga->seqregs[0x1f] = 0x2d;
	svga->gdcreg[0x18] = 0xf;
	svga->seqregs[0xf] = 0x98;
	svga->seqregs[0x17] = 0x20;
	svga->seqregs[0x15] = 4;

	svga->crtc[0x27] = CIRRUS_ID_CLGD5446;

	svga->gdcreg[0x00] = gd5446->shadow_gr0 & 0x0f;
	svga->gdcreg[0x01] = gd5446->shadow_gr1 & 0x0f;

	// gd5446_recalc_mapping(gd5446);
	/* force refresh */
	// cirrus_update_bank_ptr(s, 0);
	// cirrus_update_bank_ptr(s, 1);

	init_rops();

        gd5446->bank[1] = 0x8000;
        
        return gd5446;
}

static int gd5436_available()
{
        return rom_present("roms/5436.VBI");
}

static int gd5446_available()
{
        return rom_present("roms/5446BV.VBI");
}

void gd5436_close(void *p)
{
        gd5446_t *gd5446 = (gd5446_t *)p;

        svga_close(&gd5446->svga);
        
        free(gd5446);
}

void gd5446_close(void *p)
{
        gd5446_t *gd5446 = (gd5446_t *)p;

        svga_close(&gd5446->svga);
        
        free(gd5446);
}

void gd5436_speed_changed(void *p)
{
        gd5446_t *gd5446 = (gd5446_t *)p;
        
        svga_recalctimings(&gd5446->svga);
}

void gd5446_speed_changed(void *p)
{
        gd5446_t *gd5446 = (gd5446_t *)p;
        
        svga_recalctimings(&gd5446->svga);
}

void gd5436_force_redraw(void *p)
{
        gd5446_t *gd5446 = (gd5446_t *)p;

        gd5446->svga.fullchange = changeframecount;
}

void gd5446_force_redraw(void *p)
{
        gd5446_t *gd5446 = (gd5446_t *)p;

        gd5446->svga.fullchange = changeframecount;
}

void gd5436_add_status_info(char *s, int max_len, void *p)
{
        gd5446_t *gd5446 = (gd5446_t *)p;
        
        svga_add_status_info(s, max_len, &gd5446->svga);
}

void gd5446_add_status_info(char *s, int max_len, void *p)
{
        gd5446_t *gd5446 = (gd5446_t *)p;
        
        svga_add_status_info(s, max_len, &gd5446->svga);
}

device_t gd5436_device =
{
        "Cirrus Logic GD5436",
        // DEVICE_NOT_WORKING,
	0,
        gd5436_init,
        gd5436_close,
        gd5436_available,
        gd5436_speed_changed,
        gd5436_force_redraw,
        gd5436_add_status_info
};

device_t gd5446_device =
{
        "Cirrus Logic GD5446",
        // DEVICE_NOT_WORKING,
	0,
        gd5446_init,
        gd5446_close,
        gd5446_available,
        gd5446_speed_changed,
        gd5446_force_redraw,
        gd5446_add_status_info
};
