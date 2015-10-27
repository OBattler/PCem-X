/*Generic SVGA handling*/
/*This is intended to be used by another SVGA driver, and not as a card in it's own right*/
#include <stdlib.h>
#include "ibm.h"
#include "mem.h"
#include "video.h"
#include "vid_svga.h"
#include "vid_svga_render.h"
#include "io.h"
#include "timer.h"

#define svga_output 0

extern uint8_t edatlookup[4][4];

uint8_t svga_rotate[8][256];

static uint8_t mask_gdc[9] = {0x0F, 0x0F, 0x0F, 0x1F, 0x03, 0x7B, 0x0F, 0x0F, 0xFF};
uint8_t mask_crtc[0x19] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0xFF, 0x3F, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xCF, 0xFF, 0xFF, 0x7F, 0xFF, 0x7F, 0xEF, 0xFF};
static uint8_t mask_seq[5] = {0x03, 0x3D, 0x0F, 0x3F, 0x0E};

/*Primary SVGA device. As multiple video cards are not yet supported this is the
  only SVGA device.*/
static svga_t *svga_pri;

static int old_overscan_color = 0;

svga_t *svga_get_pri()
{
        return svga_pri;
}
void svga_set_override(svga_t *svga, int val)
{
        if (svga->override && !val)
                svga->fullchange = changeframecount;
        svga->override = val;
}

void svga_out(uint16_t addr, uint8_t val, void *p)
{
        svga_t *svga = (svga_t *)p;
        int c;
        uint8_t o;
        // printf("OUT SVGA %03X %02X %04X:%04X\n",addr,val,CS,pc);
        switch (addr)
        {
                case 0x3C0:
                if (!svga->attrff)
                {
                        svga->attraddr = val & 31;
                        if ((val & 0x20) != svga->attr_palette_enable)
                        {
                                svga->fullchange = 3;
                                svga->attr_palette_enable = val & 0x20;
                                svga_recalctimings(svga);
                        }
                }
                else
                {
			o = svga->attrregs[svga->attraddr & 31];
                        svga->attrregs[svga->attraddr & 31] = val;
                        if (svga->attraddr < 16) 
                                svga->fullchange = changeframecount;
                        if (svga->attraddr == 0x10 || svga->attraddr == 0x14 || svga->attraddr < 0x10)
                        {
                                for (c = 0; c < 16; c++)
                                {
                                        /* if (svga->attrregs[0x10] & 0x80) svga->egapal[c] = (svga->attrregs[c] &  0xf) | ((svga->attrregs[0x14] & 0xf) << 4);
                                        else                             svga->egapal[c] = (svga->attrregs[c] & 0x3f) | ((svga->attrregs[0x14] & 0xc) << 4); */

                                        if (svga->attrregs[0x10] & 0x80) svga->egapal[c] = (svga->attrregs[c] &  0xf) | ((svga->attrregs[0x14] & 3) << 4);
                                        else                             svga->egapal[c] = (svga->attrregs[c] & 0x3f);

					if (svga->attrregs[0x10] & 0x40) svga->egapal[c] |= ((svga->attrregs[0x14] & 0x0c) << 4);
                                }
                        }
                        if (svga->attraddr == 0x11)
			{
#ifndef RELEASE_BUILD
				pclog("Border color now %02X\n", svga->attrregs[svga->attraddr]);
#endif
			}
                        if ((svga->attraddr == 0x10) && (svga->attraddr == 0x11))
			{
                                if (o != val)  svga_recalctimings(svga);
			}
                        if (svga->attraddr == 0x12)
                        {
                                if ((val & 0xf) != svga->plane_mask)
                                        svga->fullchange = changeframecount;
                                svga->plane_mask = val & 0xf;
                        }
                }
                svga->attrff ^= 1;
                break;
                case 0x3C2:
                svga->miscout = val;
		svga->enablevram = (val & 2) ? 1 : 0;
		svga->oddeven_page = (val & 0x20) ? 0 : 1;
                svga->vidclock = val & 4;// printf("3C2 write %02X\n",val);
                if (val & 1)
                {
//                        pclog("Remove mono handler\n");
                        io_removehandler(0x03a0, 0x0020, svga->video_in, NULL, NULL, svga->video_out, NULL, NULL, svga->p);
                }
                else
                {
//                        pclog("Set mono handler\n");
                        io_sethandler(0x03a0, 0x0020, svga->video_in, NULL, NULL, svga->video_out, NULL, NULL, svga->p);
                }
                svga_recalctimings(svga);
                break;
                case 0x3C4:
                svga->seqaddr = val;
                break;
                case 0x3C5:
                if (svga->seqaddr > 0xf) return;
                o = svga->seqregs[svga->seqaddr & 0xf];
		/* Sanitize value for the first 5 sequencer registers. */
		if ((svga->seqaddr & 0xf) <= 4)
			val &= mask_seq[svga->seqaddr & 0xf];
               	svga->seqregs[svga->seqaddr & 0xf] = val;
                if (o != val && (svga->seqaddr & 0xf) == 1)
                        svga_recalctimings(svga);
                switch (svga->seqaddr & 0xf)
                {
			case 0:
			// pclog("Sequencer register 0 being set to %02X, old %02X\n", val, o);
			break;
                        case 1:
                        if (svga->scrblank && !(val & 0x20))
                                svga->fullchange = 3;
                        svga->scrblank = (svga->scrblank & ~0x20) | (val & 0x20);
                        svga_recalctimings(svga);
                        break;
                        case 2:
                        svga->writemask = val & 0xf;
                        break;
                        case 3:
                        svga->charsetb = (((val >> 2) & 3) * 0x10000) + 2;
                        svga->charseta = ((val & 3)  * 0x10000) + 2;
                        if (val & 0x10)
                                svga->charseta += 0x8000;
                        if (val & 0x20)
                                svga->charsetb += 0x8000;
                        break;
                        case 4:
                        svga->chain4 = val & 8;
                        svga->fast = (svga->gdcreg[8] == 0xff && !(svga->gdcreg[3] & 0x18) && !svga->gdcreg[1]) && svga->chain4;
			svga->extvram = (val & 2) ? 1 : 0;
                        break;
                }
                break;
                case 0x3c6:
                svga->dac_mask = val;
                break;
                case 0x3C7:
                svga->dac_read = val;
                svga->dac_pos = 0;
                break;
                case 0x3C8:
                svga->dac_write = val;
                svga->dac_pos = 0;
                break;
                case 0x3C9:
                svga->dac_status = 0;
                svga->fullchange = changeframecount;
                switch (svga->dac_pos)
                {
                        case 0:
			svga->dac_r = val & 63;
                        svga->dac_pos++;
                        break;
                        case 1:
			svga->dac_g = val & 63;
                        svga->dac_pos++;
                        break;
                        case 2:
			svga->vgapal[svga->dac_write].r = svga->dac_r;
			svga->vgapal[svga->dac_write].g = svga->dac_g;
                        svga->vgapal[svga->dac_write].b = val & 63;
                        svga->pallook[svga->dac_write] = makecol32(svga->vgapal[svga->dac_write].r * (svga->dac8bit ? 1 : 4), svga->vgapal[svga->dac_write].g * (svga->dac8bit ? 1 : 4), svga->vgapal[svga->dac_write].b * (svga->dac8bit ? 1 : 4));
                        svga->dac_pos = 0;
                        svga->dac_write = (svga->dac_write + 1) & 255;
                        break;
                }
                break;
                case 0x3CE:
                svga->gdcaddr = val;
                break;
                case 0x3CF:
		/* Sanitize the first 9 GDC registers. */
		if ((svga->gdcaddr & 15) <= 8)
			val &= mask_gdc[svga->gdcaddr & 15];
                o = svga->gdcreg[svga->gdcaddr & 15];
                switch (svga->gdcaddr & 15)
                {
                        case 2: svga->colourcompare=val; break;
                        case 4:
				svga->readplane=val&3;
				break;
                        case 5: svga->writemode=val&3; svga->readmode=val&8;
				break;
                        case 6:
//                                pclog("svga_out recalcmapping %p\n", svga);
			svga->chain2 = val & 2;
                        if ((svga->gdcreg[6] & 0xc) != (val & 0xc))
                        {
                                // pclog("Write mapping %02X\n", val);
                                switch (val&0xC)
                                {
                                        case 0x0: /*128k at A0000*/
                                        mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x20000);
                                        // svga->banked_mask = 0xffff;
                                        svga->banked_mask = 0x1ffff;
                                        break;
                                        case 0x4: /*64k at A0000*/
                                        mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x10000);
                                        svga->banked_mask = 0xffff;
                                        break;
                                        case 0x8: /*32k at B0000*/
                                        mem_mapping_set_addr(&svga->mapping, 0xb0000, 0x08000);
                                        svga->banked_mask = 0x7fff;
                                        break;
                                        case 0xC: /*32k at B8000*/
                                        mem_mapping_set_addr(&svga->mapping, 0xb8000, 0x08000);
                                        svga->banked_mask = 0x7fff;
                                        break;
                                }
                        }
                        break;
                        case 7: svga->colournocare=val; break;
                }
		svga->gdcreg[svga->gdcaddr & 15] = val;
                	
                svga->fast = (svga->gdcreg[8] == 0xff && !(svga->gdcreg[3] & 0x18) && !svga->gdcreg[1]) && svga->chain4;
                if (((svga->gdcaddr & 15) == 5  && (val ^ o) & 0x70) || ((svga->gdcaddr & 15) == 6 && (val ^ o) & 1))
                        svga_recalctimings(svga);
                break;
        }
}

uint8_t svga_in(uint16_t addr, void *p)
{
        svga_t *svga = (svga_t *)p;
        uint8_t temp;
//        if (addr!=0x3da) pclog("Read port %04X\n",addr);
        switch (addr)
        {
                case 0x3C0:
                return svga->attraddr | svga->attr_palette_enable;
                case 0x3C1:
                return svga->attrregs[svga->attraddr];
                case 0x3c2:
                if ((svga->vgapal[0].r + svga->vgapal[0].g + svga->vgapal[0].b) >= 0x4e)
                        temp = 0;
                else
                        temp = 0x10;
                return temp;
                case 0x3C4:
                return svga->seqaddr;
                case 0x3C5:
                return svga->seqregs[svga->seqaddr & 0xF];
                case 0x3c6: return svga->dac_mask;
                case 0x3c7: return svga->dac_status;
                case 0x3c8: return svga->dac_write;
                case 0x3c9:
                svga->dac_status = 3;
                switch (svga->dac_pos)
                {
                        case 0:
                        svga->dac_pos++;
                        return svga->vgapal[svga->dac_read].r;
                        case 1:
                        svga->dac_pos++;
                        return svga->vgapal[svga->dac_read].g;
                        case 2:
                        svga->dac_pos=0;
                        svga->dac_read = (svga->dac_read + 1) & 255;
                        return svga->vgapal[(svga->dac_read - 1) & 255].b;
                }
                break;
                case 0x3CC:
                return svga->miscout;
                case 0x3CE:
                return svga->gdcaddr;
                case 0x3CF:
		if (svga->gdcaddr == 0xF8)  return (svga->latch & 0xFF);
		if (svga->gdcaddr == 0xF9)  return ((svga->latch & 0xFF00) >> 8);
		if (svga->gdcaddr == 0xFA)  return ((svga->latch & 0xFF0000) >> 16);
		if (svga->gdcaddr == 0xFB)  return ((svga->latch & 0xFF000000) >> 24);
                return svga->gdcreg[svga->gdcaddr & 0xf];
		case 0x3CA:
#ifndef RELEASE_BUILD
		pclog("3CA read\n");
#endif
                case 0x3DA:
                svga->attrff = 0;
                if (svga->cgastat & 0x01)
                        svga->cgastat &= ~0x30;
                else
                        svga->cgastat ^= 0x30;
                return svga->cgastat;
        }
//        printf("Bad EGA read %04X %04X:%04X\n",addr,cs>>4,pc);
        return 0xFF;
}

void svga_recalctimings(svga_t *svga)
{
        double crtcconst;
        double _dispontime, _dispofftime, disptime;
        int hdisp_old;

        svga->vtotal = svga->crtc[6];
        svga->dispend = svga->crtc[0x12];
        svga->vsyncstart = svga->crtc[0x10];
        svga->split = svga->crtc[0x18];
        svga->vblankstart = svga->crtc[0x15];

        if (svga->crtc[7] & 1)  svga->vtotal |= 0x100;
        if (svga->crtc[7] & 32) svga->vtotal |= 0x200;
        svga->vtotal += 2;

        if (svga->crtc[7] & 2)  svga->dispend |= 0x100;
        if (svga->crtc[7] & 64) svga->dispend |= 0x200;
        svga->dispend++;

        if (svga->crtc[7] & 4)   svga->vsyncstart |= 0x100;
        if (svga->crtc[7] & 128) svga->vsyncstart |= 0x200;
        svga->vsyncstart++;

        if (svga->crtc[7] & 0x10) svga->split|=0x100;
        if (svga->crtc[9] & 0x40) svga->split|=0x200;
        svga->split++;

        if (svga->crtc[7] & 0x08) svga->vblankstart |= 0x100;
        if (svga->crtc[9] & 0x20) svga->vblankstart |= 0x200;
        svga->vblankstart++;

        svga->hdisp = svga->crtc[1];
        svga->hdisp++;

        svga->htotal = svga->crtc[0];
        svga->htotal += 6; /*+6 is required for Tyrian*/

        svga->rowoffset = svga->crtc[0x13];

        svga->clock = (svga->vidclock) ? VGACONST2 : VGACONST1;

        svga->lowres = svga->attrregs[0x10] & 0x40;

        svga->interlace = 0;

        svga->ma_latch = (svga->crtc[0xc] << 8) | svga->crtc[0xd];

        svga->hdisp_time = svga->hdisp;
        svga->render = svga_render_blank;
        if (!svga->scrblank && svga->attr_palette_enable)
        {
                if (!(svga->gdcreg[6] & 1)) /*Text mode*/
                {
                        if (svga->seqregs[1] & 8) /*40 column*/
                        {
                                svga->render = svga_render_text_40;
                                svga->hdisp *= (svga->seqregs[1] & 1) ? 16 : 18;
                        }
                        else
                        {
                                svga->render = svga_render_text_80;
                                svga->hdisp *= (svga->seqregs[1] & 1) ? 8 : 9;
                        }
                        svga->hdisp_old = svga->hdisp;
                }
                else
                {
                        svga->hdisp *= (svga->seqregs[1] & 8) ? 16 : 8;
                        svga->hdisp_old = svga->hdisp;

                        switch (svga->gdcreg[5] & 0x60)
                        {
                                case 0x00: /*16 colours*/
                                if (svga->seqregs[1] & 8) /*Low res (320)*/
                                        svga->render = svga_render_4bpp_lowres;
                                else
                                        svga->render = svga_render_4bpp_highres;
                                break;
                                case 0x20: /*4 colours*/
                                if (svga->seqregs[1] & 8) /*Low res (320)*/
                                        svga->render = svga_render_2bpp_lowres;
                                else
                                        svga->render = svga_render_2bpp_highres;
                                break;
                                case 0x40: case 0x60: /*256+ colours*/
                                switch (svga->bpp)
                                {
                                        case 8:
                                        if (svga->lowres)
                                                svga->render = svga_render_8bpp_lowres;
                                        else
                                                svga->render = svga_render_8bpp_highres;
                                        break;
                                        case 15:
                                        if (svga->lowres)
                                                svga->render = svga_render_15bpp_lowres;
                                        else
                                                svga->render = svga_render_15bpp_highres;
                                        break;
                                        case 16:
                                        if (svga->lowres)
                                                svga->render = svga_render_16bpp_lowres;
                                        else
                                                svga->render = svga_render_16bpp_highres;
                                        break;
                                        case 24:
                                        if (svga->lowres)
                                                svga->render = svga_render_24bpp_lowres;
                                        else
                                                svga->render = svga_render_24bpp_highres;
                                        break;
                                        case 32:
                                        if (svga->lowres)
                                                svga->render = svga_render_32bpp_lowres;
                                        else
                                                svga->render = svga_render_32bpp_highres;
                                        break;
                                }
                                break;
                        }
                }
        }

//        pclog("svga_render %08X : %08X %08X %08X %08X %08X  %i %i %02X %i %i\n", svga_render, svga_render_text_40, svga_render_text_80, svga_render_8bpp_lowres, svga_render_8bpp_highres, svga_render_blank, scrblank,gdcreg[6]&1,gdcreg[5]&0x60,bpp,seqregs[1]&8);

        svga->linedbl = svga->crtc[9] & 0x80;
        svga->rowcount = svga->crtc[9] & 31;
        if (svga->recalctimings_ex)
                svga->recalctimings_ex(svga);

        if (svga->vblankstart < svga->dispend)
                svga->dispend = svga->vblankstart;

        crtcconst = (svga->seqregs[1] & 1) ? (svga->clock * 8.0) : (svga->clock * 9.0);

        disptime  = svga->htotal;
        _dispontime = svga->hdisp_time;

//        printf("Disptime %f dispontime %f hdisp %i\n",disptime,dispontime,crtc[1]*8);
        if (svga->seqregs[1] & 8) { disptime *= 2; _dispontime *= 2; }
        _dispofftime = disptime - _dispontime;
        _dispontime *= crtcconst;
        _dispofftime *= crtcconst;

	svga->dispontime = (int)(_dispontime * (1 << TIMER_SHIFT) * 3.0d);
	svga->dispofftime = (int)(_dispofftime * (1 << TIMER_SHIFT) * 3.0d);
/*        printf("SVGA horiz total %i display end %i vidclock %f\n",svga->crtc[0],svga->crtc[1],svga->clock);
        printf("SVGA vert total %i display end %i max row %i vsync %i\n",svga->vtotal,svga->dispend,(svga->crtc[9]&31)+1,svga->vsyncstart);
        printf("total %f on %i cycles off %i cycles frame %i sec %i %02X\n",disptime*crtcconst,svga->dispontime,svga->dispofftime,(svga->dispontime+svga->dispofftime)*svga->vtotal,(svga->dispontime+svga->dispofftime)*svga->vtotal*70,svga->seqregs[1]);

        pclog("svga->render %08X\n", svga->render);*/
}

extern int cyc_total;
void svga_poll(void *p)
{
        svga_t *svga = (svga_t *)p;
        int x;

        if (!svga->linepos)
        {
//                if (!(vc & 15)) pclog("VC %i %i\n", vc, GetTickCount());
                if (svga->displine == svga->hwcursor_latch.y && svga->hwcursor_latch.ena)
                {
                        svga->hwcursor_on = 64 - svga->hwcursor_latch.yoff;
                        svga->hwcursor_oddeven = 0;
                }

                if (svga->displine == svga->hwcursor_latch.y+1 && svga->hwcursor_latch.ena && svga->interlace)
                {
                        svga->hwcursor_on = 64 - svga->hwcursor_latch.yoff;
                        svga->hwcursor_oddeven = 1;
                }

                if (svga->displine == svga->overlay_latch.y && svga->overlay_latch.ena)
                {
                        svga->overlay_on = svga->overlay_latch.ysize - svga->overlay_latch.yoff;
                        svga->overlay_oddeven = 0;
                }
                if (svga->displine == svga->overlay_latch.y+1 && svga->overlay_latch.ena && svga->interlace)
                {
                        svga->overlay_on = svga->overlay_latch.ysize - svga->overlay_latch.yoff;
                        svga->overlay_oddeven = 1;
                }

                svga->vidtime += svga->dispofftime;
//                if (output) printf("Display off %f\n",vidtime);
                svga->cgastat |= 1;
                svga->linepos = 1;

                if (svga->dispon)
                {
                        svga->hdisp_on=1;

                        svga->ma &= svga->vrammask;
                        if (svga->firstline == 2000)
                                svga->firstline = svga->displine;

                        if (svga->hwcursor_on || svga->overlay_on)
                                svga->changedvram[svga->ma >> 12] = svga->changedvram[(svga->ma >> 12) + 1] = 2;

                        if (!svga->override)
                                svga->render(svga);

                        if (svga->overlay_on)
                        {
                                if (!svga->override)
                                        svga->overlay_draw(svga, svga->displine);
                                svga->overlay_on--;
                                if (svga->overlay_on && svga->interlace)
                                        svga->overlay_on--;
                        }

                        if (svga->hwcursor_on)
                        {
                                if (!svga->override)
                                        svga->hwcursor_draw(svga, svga->displine);
                                svga->hwcursor_on--;
                                if (svga->hwcursor_on && svga->interlace)
                                        svga->hwcursor_on--;
                        }

                        if (svga->lastline < svga->displine)
                                svga->lastline = svga->displine;
                }

//                pclog("%03i %06X %06X\n",displine,ma,vrammask);
                svga->displine++;
                if (svga->interlace)
                        svga->displine++;
                if ((svga->cgastat & 8) && ((svga->displine & 15) == (svga->crtc[0x11] & 15)) && svga->vslines)
                {
//                        printf("Vsync off at line %i\n",displine);
                        svga->cgastat &= ~8;
                }
                svga->vslines++;
                if (svga->displine > 1500)
                        svga->displine = 0;
//                pclog("Col is %08X %08X %08X   %i %i  %08X\n",((uint32_t *)buffer32->line[displine])[320],((uint32_t *)buffer32->line[displine])[321],((uint32_t *)buffer32->line[displine])[322],
//                                                                displine, vc, ma);
        }
        else
        {
//                pclog("VC %i ma %05X\n", svga->vc, svga->ma);
                svga->vidtime += svga->dispontime;

//                if (output) printf("Display on %f\n",vidtime);
                if (svga->dispon)
                        svga->cgastat &= ~1;
                svga->hdisp_on = 0;

                svga->linepos = 0;
                if (svga->sc == (svga->crtc[11] & 31))
                        svga->con = 0;
                if (svga->dispon)
                {
                        if (svga->linedbl && !svga->linecountff)
                        {
                                svga->linecountff = 1;
                                svga->ma = svga->maback;
                        }
                        else if (svga->sc == svga->rowcount)
                        {
                                svga->linecountff = 0;
                                svga->sc = 0;

                                svga->maback += (svga->rowoffset << 3);
                                if (svga->interlace)
                                        svga->maback += (svga->rowoffset << 3);
                                svga->maback &= svga->vrammask;
                                svga->ma = svga->maback;
                        }
                        else
                        {
                                svga->linecountff = 0;
                                svga->sc++;
                                svga->sc &= 31;
                                svga->ma = svga->maback;
                        }
                }
                svga->vc++;
                svga->vc &= 2047;

                if (svga->vc == svga->split)
                {
//                        pclog("VC split\n");
                        svga->ma = svga->maback = 0;
                        if (svga->attrregs[0x10] & 0x20)
                                svga->scrollcache = 0;
                }
                if (svga->vc == svga->dispend)
                {
//                        pclog("VC dispend\n");
                        svga->dispon=0;
                        if (svga->crtc[10] & 0x20) svga->cursoron = 0;
                        else                       svga->cursoron = svga->blink & 16;
                        if (!(svga->gdcreg[6] & 1) && !(svga->blink & 15))
                                svga->fullchange = 2;
                        svga->blink++;

                        for (x = 0; x < (svga->vram_limit >> 12); x++)
                        {
                                if (svga->changedvram[x])
                                        svga->changedvram[x]--;
                        }
//                        memset(changedvram,0,2048);
                        if (svga->fullchange)
                                svga->fullchange--;
		}

                if (svga->vc == svga->vsyncstart)
                {
                        int wx, wy;
//                        pclog("VC vsync  %i %i\n", svga->firstline_draw, svga->lastline_draw);
                        svga->dispon=0;
                        svga->cgastat |= 8;
                        x = svga->hdisp;

                        if (svga->interlace && !svga->oddeven) svga->lastline++;
                        if (svga->interlace &&  svga->oddeven) svga->firstline--;

                        wx = x;
                        wy = svga->lastline - svga->firstline;

                        if (!svga->override)
                                svga_doblit(svga->firstline_draw, svga->lastline_draw + 1, wx, wy, svga);

                        readflash = 0;

                        svga->firstline = 2000;
                        svga->lastline = 0;

                        svga->firstline_draw = 2000;
                        svga->lastline_draw = 0;

                        svga->oddeven ^= 1;

                        changeframecount = svga->interlace ? 3 : 2;
                        svga->vslines = 0;

                        if (svga->interlace && svga->oddeven) svga->ma = svga->maback = svga->ma_latch + (svga->rowoffset << 1);
                        else                                  svga->ma = svga->maback = svga->ma_latch;
                        svga->ca = (svga->crtc[0xe] << 8) | svga->crtc[0xf];

                        svga->ma <<= 2;
                        svga->maback <<= 2;
                        svga->ca <<= 2;

                        svga->video_res_x = wx;
                        svga->video_res_y = wy + 1;
//                        pclog("%i %i %i\n", svga->video_res_x, svga->video_res_y, svga->lowres);
                        if (!(svga->gdcreg[6] & 1)) /*Text mode*/
                        {
                                svga->video_res_x /= (svga->seqregs[1] & 1) ? 8 : 9;
                                svga->video_res_y /= (svga->crtc[9] & 31) + 1;
                                svga->video_bpp = 0;
                        }
                        else
                        {
                                if (svga->crtc[9] & 0x80)
                                   svga->video_res_y /= 2;
                                if (!(svga->crtc[0x17] & 1))
                                   svga->video_res_y *= 2;
                                svga->video_res_y /= (svga->crtc[9] & 31) + 1;
                                if (svga->lowres)
                                   svga->video_res_x /= 2;

                                switch (svga->gdcreg[5] & 0x60)
                                {
                                        case 0x00:            svga->video_bpp = 4;   break;
                                        case 0x20:            svga->video_bpp = 2;   break;
                                        case 0x40: case 0x60: svga->video_bpp = svga->bpp; break;
                                }
                        }
//                        if (svga_interlace && oddeven) ma=maback=ma+(svga_rowoffset<<2);

//                        pclog("Addr %08X vson %03X vsoff %01X  %02X %02X %02X %i %i\n",ma,svga_vsyncstart,crtc[0x11]&0xF,crtc[0xD],crtc[0xC],crtc[0x33], svga_interlace, oddeven);
                }
                if (svga->vc == svga->vtotal)
                {
//                        pclog("VC vtotal\n");


//                        printf("Frame over at line %i %i  %i %i\n",displine,vc,svga_vsyncstart,svga_dispend);
                        svga->vc = 0;
                        svga->sc = 0;
                        svga->dispon = 1;
                        svga->displine = (svga->interlace && svga->oddeven) ? 1 : 0;
                        svga->scrollcache = svga->attrregs[0x13] & 7;
                        svga->linecountff = 0;

                        svga->hwcursor_on = 0;
                        svga->hwcursor_latch = svga->hwcursor;

                        svga->overlay_on = 0;
                        svga->overlay_latch = svga->overlay;
//                        pclog("Latch HWcursor addr %08X\n", svga_hwcursor_latch.addr);

//                        pclog("ADDR %08X\n",hwcursor_addr);
                }
                if (svga->sc == (svga->crtc[10] & 31))
                        svga->con = 1;
        }
//        printf("2 %i\n",svga_vsyncstart);
//pclog("svga_poll %i %i %i %i %i  %i %i\n", ins, svga->dispofftime, svga->dispontime, svga->vidtime, cyc_total, svga->linepos, svga->vc);
}

int svga_init(svga_t *svga, void *p, int memsize,
               void (*recalctimings_ex)(struct svga_t *svga),
               uint8_t (*video_in) (uint16_t addr, void *p),
               void    (*video_out)(uint16_t addr, uint8_t val, void *p),
               void (*hwcursor_draw)(struct svga_t *svga, int displine),
               void (*overlay_draw)(struct svga_t *svga, int displine))
{
        int c, d, e;

        svga->p = p;

        for (c = 0; c < 256; c++)
        {
                e = c;
                for (d = 0; d < 8; d++)
                {
                        svga_rotate[d][c] = e;
                        e = (e >> 1) | ((e & 1) ? 0x80 : 0);
                }
        }
        svga->readmode = 0;

	svga->attrregs[0x11] = 0;
	old_overscan_color = 0;

	overscan_x = 16;
	overscan_y = 32;

        svga->crtc[0] = 63;
        svga->crtc[6] = 255;
        svga->dispontime = 1000 * (1 << TIMER_SHIFT) * 3.0d;
        svga->dispofftime = 1000 * (1 << TIMER_SHIFT) * 3.0d;
        svga->bpp = 8;
        svga->dac8bit = 0;
        svga->vram = malloc(memsize);
        svga->vram_limit = memsize;
        svga->vrammask = memsize - 1;
        svga->changedvram = malloc(/*(memsize >> 12) << 1*/0x800000 >> 12);
        svga->recalctimings_ex = recalctimings_ex;
        svga->video_in  = video_in;
        svga->video_out = video_out;
        svga->hwcursor_draw = hwcursor_draw;
        svga->overlay_draw = overlay_draw;
//        _svga_recalctimings(svga);

	svga->miscout |= 0x22;
	svga->enablevram = 1;
	svga->oddeven_page = 0;

        mem_mapping_add(&svga->mapping, 0xa0000, 0x20000, svga_read, svga_readw, svga_readl, svga_write, svga_writew, svga_writel, NULL, 0, svga);

        timer_add(svga_poll, &svga->vidtime, TIMER_ALWAYS_ENABLED, svga);
        vramp = svga->vram;

        svga_pri = svga;

	svga->seqregs[4] |= 2;
	svga->extvram = 1;

        return 0;
}

void svga_close(svga_t *svga)
{
        free(svga->changedvram);
        free(svga->vram);

        svga_pri = NULL;
}

#define egacycles 1
#define egacycles2 1

#define GET_PLANE(data, p) (((data) >> ((p) * 8)) & 0xff)

static const uint32_t mask16[16] = {
	0x00000000,

	0x000000ff,

	0x0000ff00,

	0x0000ffff,

	0x00ff0000,

	0x00ff00ff,

	0x00ffff00,

	0x00ffffff,

	0xff000000,

	0xff0000ff,

	0xff00ff00,

	0xff00ffff,

	0xffff0000,

	0xffff00ff,

	0xffffff00,

	0xffffffff
};

void svga_write_common(uint32_t addr, uint8_t val, void *p, uint8_t linear)
{
        svga_t *svga = (svga_t *)p;
        uint8_t vala, valb, valc, vald, wm = svga->writemask;
        int writemask2 = svga->writemask;
	uint32_t raddr = addr;
	uint32_t ex = (1 << 13);
	uint32_t val32 = (uint32_t) val;

	int memory_map_mode, plane, write_mode, b, func_select, mask;
	uint32_t write_mask, bit_mask, set_mask, real_addr, real_mask;

	if (!svga->enablevram)  return;
	// if (!(svga->seqregs[0] & 2))  return;

	if (linear)
	{
	        cycles -= video_timing_b;
        	cycles_lost += video_timing_b;

	        egawrites++;
	}
	else
	{
	        egawrites++;

	        cycles -= video_timing_b;
        	cycles_lost += video_timing_b;
	}

        if (!(svga->gdcreg[6] & 1)) svga->fullchange=2;

	/* from here on, code is ported by OBattler from QEMU */
	memory_map_mode = (svga->gdcreg[6] >> 2) & 3;

	if (!linear)
	{
		addr &= 0x1ffff;

		switch (memory_map_mode)
		{
			case 0:
				break;
			case 1:
				if (addr >= 0x10000)
					return;
				addr += svga->write_bank;
				break;
			case 2:
				addr -= 0x10000;
				if (addr >= 0x8000)
					return;
				break;
			default:
			case 3:
				addr -= 0x18000;
				if (addr >= 0x8000)
					return;
				break;
		}
	}

	// if ((!svga->extvram) && (addr >= 0x10000))  return;
	/* Horrible hack, I know, but it's the only way to fix the 440FX BIOS filling the VRAM with garbage until Tom fixes the memory emulation. */
	if ((cs == 0xE0000) && (pc == 0xBF2F) && (romset == ROM_440FX))  return;
	if ((cs == 0xE0000) && (pc == 0xBF77) && (romset == ROM_440FX))  return;

	if (svga->chain4 || svga->fb_only)
	{
		/* chain 4 mode: simplest access */
		real_addr = addr;
		if (linear)  real_addr &= 0x7fffff;
		if ((!svga->extvram) && (real_addr >= 0x10000))  return;
                if (real_addr >= svga->vram_limit)  return;
		plane = real_addr & 3;
		mask = (1 << plane);
		if (svga->seqregs[2] & mask)
		{
			svga->vram[real_addr] = val32;

		        svga->changedvram[real_addr >> 12] = changeframecount;
		}
	}
	else if (!(svga->seqregs[4] & 4))
	{
		/* odd/even mode (aka text mode mapping) */
		plane = (svga->readplane & 2) | (addr & 1);
		mask = (1 << plane);
		if (svga->seqregs[2] & mask)
		{
			addr = ((addr & ~1) << 2) | plane | (svga->oddeven_page ? 0x10000 : 0);
			real_addr = addr;
			if (linear)  real_addr &= 0x7fffff;
			if ((!svga->extvram) && (real_addr >= 0x10000))  return;
	                if (real_addr >= svga->vram_limit)  return;
			if ((raddr <= 0xA0000) && (raddr >= 0xBFFFF))  return;
			// pclog("Write %08X %08X %02X [%02X %02X %02X %02X %02X %02X %02X %02X %02X] [%02X %02X %02X %02X %02X %02X] [%02X] [%04X %08X]\n", addr, raddr, val, svga->gdcreg[0], svga->gdcreg[1], svga->gdcreg[2], svga->gdcreg[3], svga->gdcreg[4], svga->gdcreg[5], svga->gdcreg[6], svga->gdcreg[7], svga->gdcreg[8], svga->seqregs[0], svga->seqregs[1], svga->seqregs[2], svga->seqregs[3], svga->seqregs[4], svga->seqregs[7], svga->miscout, cs, pc);
			svga->vram[real_addr] = val32;

		        svga->changedvram[real_addr >> 12] = changeframecount;
		}
	}
	else
	{
		/* standard VGA latched access */
		func_select = (svga->gdcreg[3] >> 3) & 3;

		write_mode = svga->gdcreg[5] & 3;

		switch (write_mode)
		{
			default:
			case 0:
				/* rotate */
		                if (svga->gdcreg[3] & 7)
		                        val32 = svga_rotate[svga->gdcreg[3] & 7][val32];

				/* apply set/reset mask */
				bit_mask = svga->gdcreg[8];

				val32 |= (val32 << 8);
				val32 |= (val32 << 16);

				if (svga->gdcreg[8] == 0xff && !(svga->gdcreg[3] & 0x18) && !svga->gdcreg[1])  goto do_write;

				set_mask = mask16[svga->gdcreg[1]];
				val32 = (val32 & ~set_mask) | (mask16[svga->gdcreg[0]] & set_mask);
				break;
			case 1:
				val32 = svga->latch;
				goto do_write;
			case 2:
				val32 = mask16[val32 & 0x0f];
				bit_mask = svga->gdcreg[8];
				if (!(svga->gdcreg[3] & 0x18) && !svga->gdcreg[1])  func_select = 0;
				break;
			case 3:
				/* rotate */
		                if (svga->gdcreg[3] & 7)
		                        val32 = svga_rotate[svga->gdcreg[3] & 7][val32];

				bit_mask = svga->gdcreg[8] & val32;
				val32 = mask16[svga->gdcreg[0]];
				break;
		}

		/* apply bit mask */
		bit_mask |= bit_mask << 8;
		bit_mask |= bit_mask << 16;

		/* apply logical operation */
		switch(func_select)
		{
			case 0:
			default:
				/* set */
				val32 &= bit_mask;
				val32 |= (svga->latch & ~bit_mask);
				break;
			case 1:
				/* and */
				val32 |= ~bit_mask;
				val32 &= svga->latch;
				break;
			case 2:
				/* or */
				val32 &= bit_mask;
				val32 |= svga->latch;
				break;
			case 3:
				/* xor */
				val32 &= bit_mask;
				val32 ^= svga->latch;
				break;
		}

do_write:
		/* mask data according to sr[2] */
		mask = svga->seqregs[2];
		write_mask = mask16[mask];
		real_addr = addr;
		if (linear)  real_addr &= 0x1fffff;
		if ((!svga->extvram) && (real_addr >= 0x4000))  return;
                if ((real_addr << 2) >= svga->vram_limit)  return;
		((uint32_t *)(svga->vram))[real_addr] = (((uint32_t *)(svga->vram))[real_addr] & ~write_mask) | (val32 & write_mask);

		real_addr <<= 2;
	        svga->changedvram[real_addr >> 12] = changeframecount;
		real_addr >>= 2;
	}
}

uint8_t svga_read_common(uint32_t addr, void *p, uint8_t linear)
{
        svga_t *svga = (svga_t *)p;
        uint8_t temp, temp2, temp3, temp4;
        uint32_t latch_addr;
	int readplane = svga->readplane;
	uint32_t raddr = addr;
	uint32_t ex = (1 << 13);
	int memory_map_mode, plane;
	uint32_t ret, real_addr;

	if (!svga->enablevram)  return 0xFF;
	// if (!(svga->seqregs[0] & 2))  return 0xFF;

        egareads++;
        cycles -= video_timing_b;
        cycles_lost += video_timing_b;

	/* from here on, code is ported by OBattler from QEMU */
	memory_map_mode = (svga->gdcreg[6] >> 2) & 3;

	if (!linear)
	{
		addr &= 0x1ffff;

		switch(memory_map_mode)
		{
			case 0:
				break;
			case 1:
				if (addr >= 0x10000)
					return 0xff;
				addr += svga->read_bank;
				break;
			case 2:
				addr -= 0x10000;
				if (addr >= 0x8000)
					return 0xff;
				break;
			default:
			case 3:
				addr -= 0x18000;
				if (addr >= 0x8000)
					return 0xff;
				break;
		}
	}

	// if ((!svga->extvram) && (addr >= 0x10000))  return 0xff;

	if (svga->chain4 || svga->fb_only)
	{
		/* chain 4 mode : simplest access */
		real_addr = addr;
		if (linear)  real_addr &= 0x7fffff;
	        latch_addr = (real_addr << 2) & 0x7fffff;
		if ((!svga->extvram) && (real_addr >= 0x10000))  return 0xff;
                if (real_addr >= svga->vram_limit)  return 0xff;
		ret = svga->vram[real_addr];
	}
	else if (svga->gdcreg[5] & 0x10)
	{
		/* odd/even mode (aka text mode mapping) */
		plane = (svga->readplane & 2) | (addr & 1);
		addr = ((addr & ~1) << 2) | plane | (svga->oddeven_page ? 0x10000 : 0);
		real_addr = addr;
		if (linear)  real_addr &= 0x7fffff;
	        latch_addr = (real_addr << 2) & 0x7fffff;
		if ((!svga->extvram) && (real_addr >= 0x10000))  return 0xff;
                if (real_addr >= svga->vram_limit)  return 0xff;
		ret = svga->vram[real_addr];
	}
	else
	{
		/* standard VGA latched access */
		real_addr = addr;
		if (linear)  real_addr &= 0x1fffff;
		if ((!svga->extvram) && (real_addr >= 0x4000))  return 0xff;
		if ((real_addr << 2) > (svga->vram_limit))
			svga->latch = 0xffffffff;
		else
			svga->latch = ((uint32_t *)(svga->vram))[real_addr];
                if ((real_addr << 2) >= svga->vram_limit)  return 0xff;

		if (!(svga->gdcreg[5] & 8))
		{
			/* read mode 0 */
			plane = svga->gdcreg[4];
			ret = GET_PLANE(svga->latch, plane);
		}
		else
		{
			/* read mode 1 */
			ret = (svga->latch ^ mask16[svga->gdcreg[2]]) & mask16[svga->gdcreg[7]];
			ret |= ret >> 16;
			ret |= ret >> 8;
			ret = (~ret) & 0xff;
		}
	}

	return ret;
}

void svga_write(uint32_t addr, uint8_t val, void *p)
{
	svga_write_common(addr, val, p, 0);
}

uint8_t svga_read(uint32_t addr, void *p)
{
	return svga_read_common(addr, p, 0);
}

void svga_write_linear(uint32_t addr, uint8_t val, void *p)
{
	svga_write_common(addr, val, p, 1);
}

uint8_t svga_read_linear(uint32_t addr, void *p)
{
	return svga_read_common(addr, p, 1);
}

void svga_doblit(int y1, int y2, int wx, int wy, svga_t *svga)
{
	int y_add = (enable_overscan) ? 32 : 0;
	int x_add = (enable_overscan) ? 16 : 0;
	uint32_t *p, *q, i, j;

//        pclog("svga_doblit start\n");
        svga->frames++;
//        pclog("doblit %i %i\n", y1, y2);
//        pclog("svga_doblit %i %i\n", wx, svga->hdisp);


        if (y1 > y2)
        {
                video_blit_memtoscreen(32, 0, 0, 0, xsize + x_add, ysize + y_add);
                return;
        }

        if ((wx!=xsize || wy!=ysize) && !vid_resize)
        {
                xsize=wx;
                ysize=wy+1;
                if (xsize<64) xsize=640;
                if (ysize<32) ysize=200;

                updatewindowsize(xsize + x_add,ysize + y_add);
        }
        if (vid_resize)
        {
                xsize = wx;
                ysize = wy + 1;
        }

#if 0
	if (old_overscan_color != svga->attrregs[0x11])
	{
		old_overscan_color = svga->attrregs[0x11];
                video_blit_memtoscreen(32, 0, 0, 0, xsize + x_add, ysize + y_add);
       	        return;
	}
#endif

	if (enable_overscan)
	{
		if ((wx < 160) && ((wy + 1) < 120))  goto svga_ignore_overscan;

		for (i  = 0; i < 16; i++)
		{
			p = &((uint32_t *)buffer32->line[i])[32];
			q = &((uint32_t *)buffer32->line[ysize + y_add - 1 - i])[32];

			for (j = 0; j < (xsize + x_add); j++)
			{
				p[j] = svga->pallook[svga->attrregs[0x11]];
				q[j] = svga->pallook[svga->attrregs[0x11]];
			}
		}

		for (i = 16; i < (ysize + 16); i ++)
		{
			p = &((uint32_t *)buffer32->line[i])[32];

			for (j = 0; j < 8; j++)
			{
				p[j] = svga->pallook[svga->attrregs[0x11]];
				p[xsize + x_add - 1 - j] = svga->pallook[svga->attrregs[0x11]];
			}
		}
	}

svga_ignore_overscan:
	// pclog("blit: 32 0 %i %i %i %i\n", y1, y2 + y_add, xsize + x_add, ysize + y_add);
        video_blit_memtoscreen(32, 0, y1, y2 + y_add, xsize + x_add, ysize + y_add);
//        pclog("svga_doblit end\n");
}

void svga_writew(uint32_t addr, uint16_t val, void *p)
{
        svga_t *svga = (svga_t *)p;
        if (!svga->fast)
        {
                svga_write(addr, val, p);
                svga_write(addr + 1, val >> 8, p);
                return;
        }
        
        egawrites += 2;

        cycles -= video_timing_w;
        cycles_lost += video_timing_w;

#ifndef RELEASE_BUILD
        if (svga_output) pclog("svga_writew: %05X ", addr);
#endif
        addr = (addr & svga->banked_mask) + svga->write_bank;
        addr &= 0x7FFFFF;        
        if (addr >= svga->vram_limit)
                return;
	if (!svga->enablevram)  return;
	if ((addr > 0xFFFF) && !svga->extvram)  return;
#ifndef RELEASE_BUILD
        if (svga_output) pclog("%08X (%i, %i) %04X\n", addr, addr & 1023, addr >> 10, val);
#endif
        *(uint16_t *)&svga->vram[addr] = val;
        svga->changedvram[addr >> 12] = changeframecount;
}

void svga_writel(uint32_t addr, uint32_t val, void *p)
{
        svga_t *svga = (svga_t *)p;
        
        if (!svga->fast)
        {
                svga_write(addr, val, p);
                svga_write(addr + 1, val >> 8, p);
                svga_write(addr + 2, val >> 16, p);
                svga_write(addr + 3, val >> 24, p);
                return;
        }
        
        egawrites += 4;

        cycles -= video_timing_l;
        cycles_lost += video_timing_l;

#ifndef RELEASE_BUILD
        if (svga_output) pclog("svga_writel: %05X ", addr);
#endif
        addr = (addr & svga->banked_mask) + svga->write_bank;
        addr &= 0x7FFFFF;
        if (addr >= svga->vram_limit)
                return;
	if (!svga->enablevram)  return;
	if ((addr > 0xFFFF) && !svga->extvram)  return;
#ifndef RELEASE_BUILD
        if (svga_output) pclog("%08X (%i, %i) %08X\n", addr, addr & 1023, addr >> 10, val);
#endif
        
        *(uint32_t *)&svga->vram[addr] = val;
        svga->changedvram[addr >> 12] = changeframecount;
}

uint16_t svga_readw(uint32_t addr, void *p)
{
        svga_t *svga = (svga_t *)p;
        
        if (!svga->fast)
           return svga_read(addr, p) | (svga_read(addr + 1, p) << 8);
        
        egareads += 2;

        cycles -= video_timing_w;
        cycles_lost += video_timing_w;

//        pclog("Readw %05X ", addr);
        addr = (addr & svga->banked_mask) + svga->read_bank;
        addr &= 0x7FFFFF;
//        pclog("%08X %04X\n", addr, *(uint16_t *)&vram[addr]);
        if (addr >= svga->vram_limit) return 0xffff;
	if (!svga->enablevram)  return 0xffff;
	if ((addr > 0xFFFF) && !svga->extvram)  return 0xffff;
        
        return *(uint16_t *)&svga->vram[addr];
}

uint32_t svga_readl(uint32_t addr, void *p)
{
        svga_t *svga = (svga_t *)p;
        
        if (!svga->fast)
           return svga_read(addr, p) | (svga_read(addr + 1, p) << 8) | (svga_read(addr + 2, p) << 16) | (svga_read(addr + 3, p) << 24);
        
        egareads += 4;

        cycles -= video_timing_l;
        cycles_lost += video_timing_l;

//        pclog("Readl %05X ", addr);
        addr = (addr & svga->banked_mask) + svga->read_bank;
        addr &= 0x7FFFFF;
//        pclog("%08X %08X\n", addr, *(uint32_t *)&vram[addr]);
        if (addr >= svga->vram_limit) return 0xffffffff;
	if (!svga->enablevram)  return 0xffffffff;
	if ((addr > 0xFFFF) && !svga->extvram)  return 0xffffffff;
        
        return *(uint32_t *)&svga->vram[addr];
}

void svga_writew_linear(uint32_t addr, uint16_t val, void *p)
{
        svga_t *svga = (svga_t *)p;
        
        if (!svga->fast)
        {
                svga_write_linear(addr, val, p);
                svga_write_linear(addr + 1, val >> 8, p);
                return;
        }
        
        egawrites += 2;

        cycles -= video_timing_w;
        cycles_lost += video_timing_w;

#ifndef RELEASE_BUILD
	if (svga_output) pclog("Write LFBw %08X %04X\n", addr, val);
#endif
        addr &= 0x7FFFFF;
        if (addr >= svga->vram_limit)
                return;
	if (!svga->enablevram)  return;
	if ((addr > 0xFFFF) && !svga->extvram)  return;
        *(uint16_t *)&svga->vram[addr] = val;
        svga->changedvram[addr >> 12] = changeframecount;
}

void svga_writel_linear(uint32_t addr, uint32_t val, void *p)
{
        svga_t *svga = (svga_t *)p;
        
        if (!svga->fast)
        {
                svga_write_linear(addr, val, p);
                svga_write_linear(addr + 1, val >> 8, p);
                svga_write_linear(addr + 2, val >> 16, p);
                svga_write_linear(addr + 3, val >> 24, p);
                return;
        }
        
        egawrites += 4;

        cycles -= video_timing_l;
        cycles_lost += video_timing_l;

#ifndef RELEASE_BUILD
	if (svga_output) pclog("Write LFBl %08X %08X\n", addr, val);
#endif
        addr &= 0x7fffff;
        if (addr >= svga->vram_limit)
                return;
	if (!svga->enablevram)  return;
	if ((addr > 0xFFFF) && !svga->extvram)  return;
        *(uint32_t *)&svga->vram[addr] = val;
        svga->changedvram[addr >> 12] = changeframecount;
}

uint16_t svga_readw_linear(uint32_t addr, void *p)
{
        svga_t *svga = (svga_t *)p;
        
        if (!svga->fast)
           return svga_read_linear(addr, p) | (svga_read_linear(addr + 1, p) << 8);
        
        egareads += 2;

        cycles -= video_timing_w;
        cycles_lost += video_timing_w;

        addr &= 0x7FFFFF;
        if (addr >= svga->vram_limit) return 0xffff;
	if (!svga->enablevram)  return 0xffff;
	if ((addr > 0xFFFF) && !svga->extvram)  return 0xffff;
        
        return *(uint16_t *)&svga->vram[addr];
}

uint32_t svga_readl_linear(uint32_t addr, void *p)
{
        svga_t *svga = (svga_t *)p;
        
        if (!svga->fast)
           return svga_read_linear(addr, p) | (svga_read_linear(addr + 1, p) << 8) | (svga_read_linear(addr + 2, p) << 16) | (svga_read_linear(addr + 3, p) << 24);
        
        egareads += 4;

        cycles -= video_timing_l;
        cycles_lost += video_timing_l;

        addr &= 0x7FFFFF;
        if (addr >= svga->vram_limit) return 0xffffffff;
	if (!svga->enablevram)  return 0xffffffff;
	if ((addr > 0xFFFF) && !svga->extvram)  return 0xffffffff;

        return *(uint32_t *)&svga->vram[addr];
}


void svga_add_status_info(char *s, int max_len, void *p)
{
        svga_t *svga = (svga_t *)p;
        char temps[128];

        if (svga->chain4) strcpy(temps, "SVGA chained (possibly mode 13h)\n");
        else              strcpy(temps, "SVGA unchained (possibly mode-X)\n");
        strncat(s, temps, max_len);

        if (!svga->video_bpp) strcpy(temps, "SVGA in text mode\n");
        else                  sprintf(temps, "SVGA colour depth : %i bpp\n", svga->video_bpp);
        strncat(s, temps, max_len);

        sprintf(temps, "SVGA resolution : %i x %i\n", svga->video_res_x, svga->video_res_y);
        strncat(s, temps, max_len);

        sprintf(temps, "SVGA refresh rate : %i Hz\n\n", svga->frames);
        svga->frames = 0;
        strncat(s, temps, max_len);
}
