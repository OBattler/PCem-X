/* Cirrus Logic CL-GD6235 laptop chipset driver                           */
/* Emulation code by Kazblox/Mixmaster for PCem-X.                        */
/* based on CL-GD5429 emulation by OBattler                               */
/*                                                                        */
/* =================                                                      */
/* Version History                                                        */
/* =================                                                      */
/* -7/6/2015:driver is currently using GD5429 emulation, is enough to get */
/*  all internal graphics modes + some VESA/SVGA 8bpp modes working       */
/* -added monochrome handlers                                             */
/* =================                                                      */
/* Notes                                                                  */
/* =================                                                      */
/* CL-GD6235 datasheet can be found free at                               */
/* http://www.datasheet-pdf.com/datasheetdownload.php?id=684330           */
/* =================                                                      */
/* To-do                                                                  */
/* =================                                                      */
/* -emulate proper ramdac! maybe that will get 4bpp modes working         */
/* -actually implement everything from the gd6235                         */

#include <stdlib.h>
#include "ibm.h"
#include "device.h"
#include "io.h"
#include "mem.h"
#include "rom.h"
#include "video.h"
#include "vid_cl6235.h"
#include "vid_svga.h"
#include "vid_svga_render.h"
#include "vid_unk_ramdac.h"

typedef struct gd6235_t
{
        mem_mapping_t mmio_mapping;
        
        svga_t svga;
        
        rom_t bios_rom;
        
        uint32_t bank[2];
        uint32_t mask;

        struct
        {
                uint16_t bg_col, fg_col;                
                uint16_t width, height;
                uint16_t dst_pitch, src_pitch;               
                uint32_t dst_addr, src_addr;
                uint8_t mask, mode, rop;
                
                uint32_t dst_addr_backup, src_addr_backup;
                uint16_t width_backup, height_internal;
                int x_count;
        } blt;

} gd6235_t;

void gd6235_write(uint32_t addr, uint8_t val, void *p);
uint8_t gd6235_read(uint32_t addr, void *p);

void gd6235_mmio_write(uint32_t addr, uint8_t val, void *p);
uint8_t gd6235_mmio_read(uint32_t addr, void *p);

void gd6235_blt_write_w(uint32_t addr, uint16_t val, void *p);
void gd6235_blt_write_l(uint32_t addr, uint32_t val, void *p);

void gd6235_recalc_banking(gd6235_t *gd6235);
void gd6235_recalc_mapping(gd6235_t *gd6235);

void gd6235_out(uint16_t addr, uint8_t val, void *p)
{
        gd6235_t *gd6235 = (gd6235_t *)p;
        svga_t *svga = &gd6235->svga;
        uint8_t old;
        
        if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1)) 
                addr ^= 0x60;

//        pclog("gd6235 out %04X %02X\n", addr, val);

// SEQUENCER        
        switch (addr)
        {
                case 0x3c4:
                svga->seqaddr = val;
                break;
                case 0x3c5:
                if (svga->seqaddr > 5)
                {
                        svga->seqregs[svga->seqaddr & 0x1f] = val;
                        switch (svga->seqaddr & 0x1f)
                        {
                                case 0x10: case 0x30: case 0x50: case 0x70:
                                case 0x90: case 0xb0: case 0xd0: case 0xf0:
                                svga->hwcursor.x = (val << 3) | ((svga->seqaddr >> 5) & 7);
                                // pclog("svga->hwcursor.x = %i\n", svga->hwcursor.x);
                                break;
                                case 0x11: case 0x31: case 0x51: case 0x71:
                                case 0x91: case 0xb1: case 0xd1: case 0xf1:
                                svga->hwcursor.y = (val << 3) | ((svga->seqaddr >> 5) & 7);
                                // pclog("svga->hwcursor.y = %i\n", svga->hwcursor.y);
                                break;
                                case 0x12:
                                svga->hwcursor.ena = val & 1;
                                // pclog("svga->hwcursor.ena = %i\n", svga->hwcursor.ena);
                                break;                               
                                case 0x13:
                                svga->hwcursor.addr = 0x1fc000 + ((val & 0x3f) * 256);
                                // pclog("svga->hwcursor.addr = %x\n", svga->hwcursor.addr);
                                break;                                
                                
                                case 0x17:
                                gd6235_recalc_mapping(gd6235);
                                break;
                        }
                        return;
                }
                break;
// GRAPHICS CONTROLLER
                case 0x3ce:
                svga->gdcaddr = val;
                case 0x3cf:
                if (svga->gdcaddr == 5)
                {
                        svga->gdcreg[5] = val;
                        if (svga->gdcreg[0xb] & 0x04)
                                svga->writemode = svga->gdcreg[5] & 7;
                        else
                                svga->writemode = svga->gdcreg[5] & 3;
                        svga->readmode = val & 8;
//                        pclog("writemode = %i\n", svga->writemode);
                        return;
                }
                if (svga->gdcaddr == 6)
                {
                        if ((svga->gdcreg[6] & 0xc) != (val & 0xc))
                        {
                                svga->gdcreg[6] = val;
                                gd6235_recalc_mapping(gd6235);
                        }
                        svga->gdcreg[6] = val;
                        return;
                }
                if (svga->gdcaddr > 8)
                {
                        svga->gdcreg[svga->gdcaddr & 0x3f] = val;
                        switch (svga->gdcaddr)
                        {
                                case 0x09: case 0x0a: case 0x0b:
                                gd6235_recalc_banking(gd6235);
                                if (svga->gdcreg[0xb] & 0x04)
                                        svga->writemode = svga->gdcreg[5] & 7;
                                else
                                        svga->writemode = svga->gdcreg[5] & 3;
                                break;
                        }                        
                        return;
                }
                break;
// CRTC

// MONOCHROME
                case 0x3b4:
                svga->crtcreg = val & 0x3f;
                return;
                case 0x3b5:
                if ((svga->crtcreg < 7) && (svga->crtc[0x11] & 0x80))
                        return;
                if ((svga->crtcreg == 7) && (svga->crtc[0x11] & 0x80))
                        val = (svga->crtc[7] & ~0x10) | (val & 0x10);
                old = svga->crtc[svga->crtcreg];
                svga->crtc[svga->crtcreg] = val;

                if (old != val)
                {
			if (svga->crtcreg == 0x1b)
			{
				svga->vrammask = (val & 2) ? ((1 << 21) - 1) : 0x3ffff;
			}
                        if (svga->crtcreg < 0xe || svga->crtcreg > 0x10)
                        {
                                svga->fullchange = changeframecount;
                                svga_recalctimings(svga);
                        }
                }
                break;
// COLOR                
                case 0x3d4:
                svga->crtcreg = val & 0x3f;
                return;
                case 0x3d5:
                if ((svga->crtcreg < 7) && (svga->crtc[0x11] & 0x80))
                        return;
                if ((svga->crtcreg == 7) && (svga->crtc[0x11] & 0x80))
                        val = (svga->crtc[7] & ~0x10) | (val & 0x10);
                old = svga->crtc[svga->crtcreg];
                svga->crtc[svga->crtcreg] = val;

                if (old != val)
                {
			if (svga->crtcreg == 0x1b)
			{
				svga->vrammask = (val & 2) ? ((1 << 21) - 1) : 0x3ffff;
			}
                        if (svga->crtcreg < 0xe || svga->crtcreg > 0x10)
                        {
                                svga->fullchange = changeframecount;
                                svga_recalctimings(svga);
                        }
                }
                break;
        }
        svga_out(addr, val, svga);
}

uint8_t gd6235_in(uint16_t addr, void *p)
{
        gd6235_t *gd6235 = (gd6235_t *)p;
        svga_t *svga = &gd6235->svga;

        if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3d0) && !(svga->miscout & 1)) 
                addr ^= 0x60;
        
//        if (addr != 0x3da) pclog("IN gd6235 %04X\n", addr);
//        if (addr != 0x3ba) pclog("IN gd6235 %04X\n", addr);
        
        switch (addr)
        {
                case 0x3c5:
                if (svga->seqaddr > 5)
                {
                        switch (svga->seqaddr)
                        {
                                case 6:
                                return ((svga->seqregs[6] & 0x17) == 0x12) ? 0x12 : 0x0f;
                        }
                        return svga->seqregs[svga->seqaddr & 0x3f];
                }
                break;

                case 0x3cf:
                if (svga->gdcaddr > 8)
                {
                        return svga->gdcreg[svga->gdcaddr & 0x3f];
                }
                break;
// MONOCHROME
                case 0x3b4:
                return svga->crtcreg;
                case 0x3b5:
                switch (svga->crtcreg)
                {
                        case 0x27: /*ID*/
                        return 0x91; /*gd6235*/
                }
                return svga->crtc[svga->crtcreg];
// COLOR
                case 0x3d4:
                return svga->crtcreg;
                case 0x3d5:
                // color handler
                switch (svga->crtcreg)
                {
                        case 0x27: /*ID*/
                        return 0x91; /*gd6235*/
                }
                return svga->crtc[svga->crtcreg];
        }
        return svga_in(addr, svga);
}

void gd6235_recalc_banking(gd6235_t *gd6235)
{
        svga_t *svga = &gd6235->svga;
        
        if (svga->gdcreg[0xb] & 0x20)
                gd6235->bank[0] = (svga->gdcreg[0x09] & 0x7f) << 14;
        else
                gd6235->bank[0] = svga->gdcreg[0x09] << 12;
                                
        if (svga->gdcreg[0xb] & 0x01)
        {
                if (svga->gdcreg[0xb] & 0x20)
                        gd6235->bank[1] = (svga->gdcreg[0x0a] & 0x7f) << 14;
                else
                        gd6235->bank[1] = svga->gdcreg[0x0a] << 12;
        }
        else
                gd6235->bank[1] = gd6235->bank[0] + 0x8000;
}

void gd6235_recalc_mapping(gd6235_t *gd6235)
{
        svga_t *svga = &gd6235->svga;
        
        // pclog("Write mapping %02X %i\n", svga->gdcreg[6], svga->seqregs[0x17] & 0x04);
        switch (svga->gdcreg[6] & 0x0C)
        {
                case 0x0: /*128k at A0000*/
                mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x10000);
                mem_mapping_disable(&gd6235->mmio_mapping);
                svga->banked_mask = 0xffff;
                break;
                case 0x4: /*64k at A0000*/
                mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x10000);
                if (svga->seqregs[0x17] & 0x04)
                        mem_mapping_set_addr(&gd6235->mmio_mapping, 0xb8000, 0x00100);
                svga->banked_mask = 0xffff;
                break;
                case 0x8: /*32k at B0000*/
                mem_mapping_set_addr(&svga->mapping, 0xb0000, 0x08000);
                mem_mapping_disable(&gd6235->mmio_mapping);
                svga->banked_mask = 0x7fff;
                break;
                case 0xC: /*32k at B8000*/
                mem_mapping_set_addr(&svga->mapping, 0xb8000, 0x08000);
                mem_mapping_disable(&gd6235->mmio_mapping);
                svga->banked_mask = 0x7fff;
                break;
        }
}
        
void gd6235_recalctimings(svga_t *svga)
{
        gd6235_t *gd6235 = (gd6235_t *)svga->p;
        
        if (svga->seqregs[7] & 0x01)
        {
                svga->render = svga_render_8bpp_highres;
        }
        
        svga->ma_latch |= ((svga->crtc[0x1b] & 0x01) << 16) | ((svga->crtc[0x1b] & 0xc) << 15);
        // pclog("MA now %05X %02X\n", svga->ma_latch, svga->crtc[0x1b]);
}

void gd6235_hwcursor_draw(svga_t *svga, int displine)
{
        int x;
        uint8_t dat[2];
        int xx;
        int offset = svga->hwcursor_latch.x - svga->hwcursor_latch.xoff;
        
        // pclog("HWcursor %i %i  %i %i  %x %02X %02X\n", svga->hwcursor_latch.x, svga->hwcursor_latch.y,  offset, displine, svga->hwcursor_latch.addr, vram[svga->hwcursor_latch.addr], vram[svga->hwcursor_latch.addr + 0x80]);
        for (x = 0; x < 32; x += 8)
        {
                dat[0] = svga->vram[svga->hwcursor_latch.addr];
                dat[1] = svga->vram[svga->hwcursor_latch.addr + 0x80];
                for (xx = 0; xx < 8; xx++)
                {
                        if (offset >= svga->hwcursor_latch.x)
                        {
                                if (dat[1] & 0x80)
                                        ((uint32_t *)buffer32->line[displine])[offset + 32] = 0;
                                if (dat[0] & 0x80)
                                        ((uint32_t *)buffer32->line[displine])[offset + 32] ^= 0xffffff;
                        }
                           
                        offset++;
                        dat[0] <<= 1;
                        dat[1] <<= 1;
                }
                svga->hwcursor_latch.addr++;
        }
}


void gd6235_write_linear(uint32_t addr, uint8_t val, void *p);

void gd6235_write(uint32_t addr, uint8_t val, void *p)
{
        gd6235_t *gd6235 = (gd6235_t *)p;
        svga_t *svga = &gd6235->svga;
//        pclog("gd6235_write : %05X %02X  ", addr, val);
        addr &= svga->banked_mask;
        addr = (addr & 0x7fff) + gd6235->bank[(addr >> 15) & 1];
//        pclog("%08X\n", addr);
        gd6235_write_linear(addr, val, p);
}

uint8_t gd6235_read(uint32_t addr, void *p)
{
        gd6235_t *gd6235 = (gd6235_t *)p;
        svga_t *svga = &gd6235->svga;
        uint8_t ret;
//        pclog("gd6235_read : %05X ", addr);
        addr &= svga->banked_mask;
        addr = (addr & 0x7fff) + gd6235->bank[(addr >> 15) & 1];
        ret = svga_read_linear(addr, &gd6235->svga);
//        pclog("%08X %02X\n", addr, ret);  
        return ret;      
}

void gd6235_write_linear(uint32_t addr, uint8_t val, void *p)
{
        gd6235_t *gd6235 = (gd6235_t *)p;
        svga_t *svga = &gd6235->svga;
        uint8_t vala, valb, valc, vald, wm = svga->writemask;
        int writemask2 = svga->writemask;

        cycles -= video_timing_b;
        cycles_lost += video_timing_b;

        egawrites++;
        
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
                return;
//        if (svga_output) pclog("%08X\n", addr);
        svga->changedvram[addr >> 12] = changeframecount;
        
        switch (svga->writemode)
        {
                case 4:
                // pclog("Writemode 4 : %X ", addr);
                addr <<= 1;
                svga->changedvram[addr >> 12] = changeframecount;
                // pclog("%X %X\n", addr, val);
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
                break;
                        
                case 5:
                // pclog("Writemode 5 : %X ", addr);
                addr <<= 1;
                svga->changedvram[addr >> 12] = changeframecount;
                // pclog("%X %X\n", addr, val);
                svga->vram[addr + 0] = (val & 0x80) ? svga->gdcreg[1] : svga->gdcreg[0];
                svga->vram[addr + 1] = (val & 0x40) ? svga->gdcreg[1] : svga->gdcreg[0];
                svga->vram[addr + 2] = (val & 0x20) ? svga->gdcreg[1] : svga->gdcreg[0];
                svga->vram[addr + 3] = (val & 0x10) ? svga->gdcreg[1] : svga->gdcreg[0];
                svga->vram[addr + 4] = (val & 0x08) ? svga->gdcreg[1] : svga->gdcreg[0];
                svga->vram[addr + 5] = (val & 0x04) ? svga->gdcreg[1] : svga->gdcreg[0];
                svga->vram[addr + 6] = (val & 0x02) ? svga->gdcreg[1] : svga->gdcreg[0];
                svga->vram[addr + 7] = (val & 0x01) ? svga->gdcreg[1] : svga->gdcreg[0];
                break;
                
                case 1:
                if (writemask2 & 1) svga->vram[addr]       = svga->la;
                if (writemask2 & 2) svga->vram[addr | 0x1] = svga->lb;
                if (writemask2 & 4) svga->vram[addr | 0x2] = svga->lc;
                if (writemask2 & 8) svga->vram[addr | 0x3] = svga->ld;
                break;
                case 0:
                if (svga->gdcreg[3] & 7) 
                        val = svga_rotate[svga->gdcreg[3] & 7][val];
                if (svga->gdcreg[8] == 0xff && !(svga->gdcreg[3] & 0x18) && !svga->gdcreg[1])
                {
                        if (writemask2 & 1) svga->vram[addr]       = val;
                        if (writemask2 & 2) svga->vram[addr | 0x1] = val;
                        if (writemask2 & 4) svga->vram[addr | 0x2] = val;
                        if (writemask2 & 8) svga->vram[addr | 0x3] = val;
                }
                else
                {
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
        }
}

void gd6235_start_blit(uint32_t cpu_dat, int count, void *p)
{
        gd6235_t *gd6235 = (gd6235_t *)p;
        svga_t *svga = &gd6235->svga;

        // pclog("gd6235_start_blit %i\n", count);
        if (count == -1)
        {
                gd6235->blt.dst_addr_backup = gd6235->blt.dst_addr;
                gd6235->blt.src_addr_backup = gd6235->blt.src_addr;
                gd6235->blt.width_backup    = gd6235->blt.width;
                gd6235->blt.height_internal = gd6235->blt.height;
                gd6235->blt.x_count         = gd6235->blt.mask & 7;
                // pclog("gd6235_start_blit : size %i, %i\n", gd6235->blt.width, gd6235->blt.height);
                
                if (gd6235->blt.mode & 0x04)
                {
//                        pclog("blt.mode & 0x04\n");
                        mem_mapping_set_handler(&svga->mapping, NULL, NULL, NULL, NULL, gd6235_blt_write_w, gd6235_blt_write_l);
                        mem_mapping_set_p(&svga->mapping, gd6235);
                        return;
                }
                else
                {
                        mem_mapping_set_handler(&gd6235->svga.mapping, gd6235_read, NULL, NULL, gd6235_write, NULL, NULL);
                        mem_mapping_set_p(&gd6235->svga.mapping, gd6235);
                        gd6235_recalc_mapping(gd6235);
                }                
        }
        
        while (count)
        {
                uint8_t src, dst;
                int mask;
                
                if (gd6235->blt.mode & 0x04)
                {
                        if (gd6235->blt.mode & 0x80)
                        {
                                src = (cpu_dat & 0x80) ? gd6235->blt.fg_col : gd6235->blt.bg_col;
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
                        switch (gd6235->blt.mode & 0xc0)
                        {
                                case 0x00:
                                src = svga->vram[gd6235->blt.src_addr & svga->vrammask];
                                gd6235->blt.src_addr += ((gd6235->blt.mode & 0x01) ? -1 : 1);
                                mask = 1;
                                break;
                                case 0x40:
                                src = svga->vram[(gd6235->blt.src_addr & (svga->vrammask & ~7)) | (gd6235->blt.dst_addr & 7)];
                                mask = 1;
                                break;
                                case 0x80:
                                mask = svga->vram[gd6235->blt.src_addr & svga->vrammask] & (0x80 >> gd6235->blt.x_count);
                                src = mask ? gd6235->blt.fg_col : gd6235->blt.bg_col;
                                gd6235->blt.x_count++;
                                if (gd6235->blt.x_count == 8)
                                {
                                        gd6235->blt.x_count = 0;
                                        gd6235->blt.src_addr++;
                                }
                                break;
                                case 0xc0:
                                mask = svga->vram[gd6235->blt.src_addr & svga->vrammask] & (0x80 >> (gd6235->blt.dst_addr & 7));
                                src = mask ? gd6235->blt.fg_col : gd6235->blt.bg_col;
                                break;
                        }
                        count--;                        
                }
                dst = svga->vram[gd6235->blt.dst_addr & svga->vrammask];
                svga->changedvram[(gd6235->blt.dst_addr & svga->vrammask) >> 12] = changeframecount;
               
                // pclog("Blit %i,%i %06X %06X  %06X %02X %02X  %02X %02X ", gd6235->blt.width, gd6235->blt.height_internal, gd6235->blt.src_addr, gd6235->blt.dst_addr, gd6235->blt.src_addr & svga->vrammask, svga->vram[gd6235->blt.src_addr & svga->vrammask], 0x80 >> (gd6235->blt.dst_addr & 7), src, dst);
                switch (gd6235->blt.rop)
                {
                        case 0x00: dst = 0;             break;
                        case 0x05: dst =   src &  dst;  break;
                        case 0x06: dst =   dst;         break;
                        case 0x09: dst =   src & ~dst;  break;
                        case 0x0b: dst = ~ dst;         break;
                        case 0x0d: dst =   src;         break;
                        case 0x0e: dst = 0xff;          break;
                        case 0x50: dst = ~ src &  dst;  break;
                        case 0x59: dst =   src ^  dst;  break;
                        case 0x6d: dst =   src |  dst;  break;
                        case 0x90: dst = ~(src |  dst); break;
                        case 0x95: dst = ~(src ^  dst); break;
                        case 0xad: dst =   src | ~dst;  break;
                        case 0xd0: dst =  ~src;         break;
                        case 0xd6: dst =  ~src |  dst;  break;
                        case 0xda: dst = ~(src &  dst); break;                       
                }
                // pclog("%02X  %02X\n", dst, mask);
                
                if ((gd6235->blt.width_backup - gd6235->blt.width) >= (gd6235->blt.mask & 7) &&
                    !((gd6235->blt.mode & 0x08) && !mask))
                        svga->vram[gd6235->blt.dst_addr & svga->vrammask] = dst;
                
                gd6235->blt.dst_addr += ((gd6235->blt.mode & 0x01) ? -1 : 1);
                
                gd6235->blt.width--;
                
                if (gd6235->blt.width == 0xffff)
                {
                        gd6235->blt.width = gd6235->blt.width_backup;

                        gd6235->blt.dst_addr = gd6235->blt.dst_addr_backup = gd6235->blt.dst_addr_backup + ((gd6235->blt.mode & 0x01) ? -gd6235->blt.dst_pitch : gd6235->blt.dst_pitch);
                        
                        switch (gd6235->blt.mode & 0xc0)
                        {
                                case 0x00:
                                gd6235->blt.src_addr = gd6235->blt.src_addr_backup = gd6235->blt.src_addr_backup + ((gd6235->blt.mode & 0x01) ? -gd6235->blt.src_pitch : gd6235->blt.src_pitch);
                                break;
                                case 0x40:
                                gd6235->blt.src_addr = ((gd6235->blt.src_addr + ((gd6235->blt.mode & 0x01) ? -8 : 8)) & 0x38) | (gd6235->blt.src_addr & ~0x38);
                                break;
                                case 0x80:
                                if (gd6235->blt.x_count != 0)
                                {
                                        gd6235->blt.x_count = 0;
                                        gd6235->blt.src_addr++;
                                }
                                break;
                                case 0xc0:
                                gd6235->blt.src_addr = ((gd6235->blt.src_addr + ((gd6235->blt.mode & 0x01) ? -1 : 1)) & 7) | (gd6235->blt.src_addr & ~7);
                                break;
                        }
                        
                        gd6235->blt.height_internal--;
                        if (gd6235->blt.height_internal == 0xffff)
                        {
                                if (gd6235->blt.mode & 0x04)
                                {
                                        mem_mapping_set_handler(&gd6235->svga.mapping, gd6235_read, NULL, NULL, gd6235_write, NULL, NULL);
                                        mem_mapping_set_p(&gd6235->svga.mapping, gd6235);
                                        gd6235_recalc_mapping(gd6235);
                                }
                                return;
                        }
                                
                        if (gd6235->blt.mode & 0x04)
                                return;
                }                        
        }
}

void gd6235_mmio_write(uint32_t addr, uint8_t val, void *p)
{
        gd6235_t *gd6235 = (gd6235_t *)p;

        // pclog("MMIO write %08X %02X\n", addr, val);
        switch (addr & 0xff)
        {
                case 0x00:
                gd6235->blt.bg_col = (gd6235->blt.bg_col & 0xff00) | val;
                break;
                case 0x01:
                gd6235->blt.bg_col = (gd6235->blt.bg_col & 0x00ff) | (val << 8);
                break;

                case 0x04:
                gd6235->blt.fg_col = (gd6235->blt.fg_col & 0xff00) | val;
                break;
                case 0x05:
                gd6235->blt.fg_col = (gd6235->blt.fg_col & 0x00ff) | (val << 8);
                break;

                case 0x08:
                gd6235->blt.width = (gd6235->blt.width & 0xff00) | val;
                break;
                case 0x09:
                gd6235->blt.width = (gd6235->blt.width & 0x00ff) | (val << 8);
                break;
                case 0x0a:
                gd6235->blt.height = (gd6235->blt.height & 0xff00) | val;
                break;
                case 0x0b:
                gd6235->blt.height = (gd6235->blt.height & 0x00ff) | (val << 8);
                break;
                case 0x0c:
                gd6235->blt.dst_pitch = (gd6235->blt.dst_pitch & 0xff00) | val;
                break;
                case 0x0d:
                gd6235->blt.dst_pitch = (gd6235->blt.dst_pitch & 0x00ff) | (val << 8);
                break;
                case 0x0e:
                gd6235->blt.src_pitch = (gd6235->blt.src_pitch & 0xff00) | val;
                break;
                case 0x0f:
                gd6235->blt.src_pitch = (gd6235->blt.src_pitch & 0x00ff) | (val << 8);
                break;
                
                case 0x10:
                gd6235->blt.dst_addr = (gd6235->blt.dst_addr & 0xffff00) | val;
                break;
                case 0x11:
                gd6235->blt.dst_addr = (gd6235->blt.dst_addr & 0xff00ff) | (val << 8);
                break;
                case 0x12:
                gd6235->blt.dst_addr = (gd6235->blt.dst_addr & 0x00ffff) | (val << 16);
                break;

                case 0x14:
                gd6235->blt.src_addr = (gd6235->blt.src_addr & 0xffff00) | val;
                break;
                case 0x15:
                gd6235->blt.src_addr = (gd6235->blt.src_addr & 0xff00ff) | (val << 8);
                break;
                case 0x16:
                gd6235->blt.src_addr = (gd6235->blt.src_addr & 0x00ffff) | (val << 16);
                break;

                case 0x17:
                gd6235->blt.mask = val;
                break;
                case 0x18:
                gd6235->blt.mode = val;
                break;
                
                case 0x1a:
                gd6235->blt.rop = val;
                break;
                
                case 0x40:
                if (val & 0x02)
                        gd6235_start_blit(0, -1, gd6235);
                break;
        }
}

uint8_t gd6235_mmio_read(uint32_t addr, void *p)
{
        gd6235_t *gd6235 = (gd6235_t *)p;

        // pclog("MMIO read %08X\n", addr);
        switch (addr & 0xff)
        {
                case 0x40: /*BLT status*/
                return 0;
        }
        return 0xff; /*All other registers read-only*/
}

void gd6235_blt_write_w(uint32_t addr, uint16_t val, void *p)
{
        // pclog("gd6235_blt_write_w %08X %08X\n", addr, val);
        gd6235_start_blit(val, 16, p);
}

void gd6235_blt_write_l(uint32_t addr, uint32_t val, void *p)
{
        gd6235_t *gd6235 = (gd6235_t *)p;
        
        // pclog("gd6235_blt_write_l %08X %08X  %04X %04X\n", addr, val,  ((val >> 8) & 0x00ff) | ((val << 8) & 0xff00), ((val >> 24) & 0x00ff) | ((val >> 8) & 0xff00));
        if ((gd6235->blt.mode & 0x84) == 0x84)
        {
                gd6235_start_blit( val        & 0xff, 8, p);
                gd6235_start_blit((val >> 8)  & 0xff, 8, p);
                gd6235_start_blit((val >> 16) & 0xff, 8, p);
                gd6235_start_blit((val >> 24) & 0xff, 8, p);
        }
        else
                gd6235_start_blit(val, 32, p);
}

void *gd6235_init()
{
        gd6235_t *gd6235 = malloc(sizeof(gd6235_t));
        svga_t *svga = &gd6235->svga;
        memset(gd6235, 0, sizeof(gd6235_t));
        rom_init(&gd6235->bios_rom, "roms/vga6235.rom", 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
        svga_init(&gd6235->svga, gd6235, 1 << 19, /*512k*/
                   gd6235_recalctimings,
                   gd6235_in, gd6235_out,
                   gd6235_hwcursor_draw,
                   NULL);

        mem_mapping_set_handler(&gd6235->svga.mapping, gd6235_read, NULL, NULL, gd6235_write, NULL, NULL);
        mem_mapping_set_p(&gd6235->svga.mapping, gd6235);

        mem_mapping_add(&gd6235->mmio_mapping, 0, 0, gd6235_mmio_read, NULL, NULL, gd6235_mmio_write, NULL, NULL,  NULL, 0, gd6235);

        io_sethandler(0x03c0, 0x0020, gd6235_in, NULL, NULL, gd6235_out, NULL, NULL, gd6235);

        svga->hwcursor.yoff = 32;
        svga->hwcursor.xoff = 0;

        gd6235->bank[1] = 0x8000;

	svga->seqregs[0x1f] = 0x22;
	svga->seqregs[0xf] = 0x18;
	svga->seqregs[0x15] = 3;
        
        return gd6235;
}

static int gd6235_available()
{
        return rom_present("roms/vga6235.rom");
}

void gd6235_close(void *p)
{
        gd6235_t *gd6235 = (gd6235_t *)p;

        svga_close(&gd6235->svga);
        
        free(gd6235);
}

void gd6235_speed_changed(void *p)
{
        gd6235_t *gd6235 = (gd6235_t *)p;
        
        svga_recalctimings(&gd6235->svga);
}

void gd6235_force_redraw(void *p)
{
        gd6235_t *gd6235 = (gd6235_t *)p;

        gd6235->svga.fullchange = changeframecount;
}

void gd6235_add_status_info(char *s, int max_len, void *p)
{
        gd6235_t *gd6235 = (gd6235_t *)p;
        
        svga_add_status_info(s, max_len, &gd6235->svga);
}

device_t gd6235_device =
{
        "Cirrus Logic GD6235",
	0,
        gd6235_init,
        gd6235_close,
        gd6235_available,
        gd6235_speed_changed,
        gd6235_force_redraw,
        gd6235_add_status_info
};
