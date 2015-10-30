/*cpqvdu emulation*/
#include <stdlib.h>
#include <math.h>
#include "ibm.h"
#include "device.h"
#include "io.h"
#include "mem.h"
#include "timer.h"
#include "video.h"
#include "vid_cpqvdu.h"

static int i_filt[8],q_filt[8];

static uint8_t crtcmask[32] = 
{
        0xff, 0xff, 0xff, 0xff, 0x7f, 0x1f, 0x7f, 0x7f, 0xf3, 0x1f, 0x7f, 0x1f, 0x3f, 0xff, 0x3f, 0xff,
        0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static int mdacols[256][2][2];

void cpqvdu_recalctimings(cpqvdu_t *cpqvdu);

void cpqvdu_out(uint16_t addr, uint8_t val, void *p)
{
        cpqvdu_t *cpqvdu = (cpqvdu_t *)p;
        uint8_t old;
//        pclog("cpqvdu_OUT %04X %02X\n", addr, val);
        switch (addr)
        {
                case 0x3D4:
                cpqvdu->crtcreg = val & 31;
                return;
                case 0x3D5:
                old = cpqvdu->crtc[cpqvdu->crtcreg];
                cpqvdu->crtc[cpqvdu->crtcreg] = val & crtcmask[cpqvdu->crtcreg];
                if (old != val)
                {
                        if (cpqvdu->crtcreg < 0xe || cpqvdu->crtcreg > 0x10)
                        {
                                fullchange = changeframecount;
                                cpqvdu_recalctimings(cpqvdu);
                        }
                }
                return;
                case 0x3D8:
                cpqvdu->cpqvdumode = val;
                return;
                case 0x3D9:
                cpqvdu->cpqvducol = val;
                return;
        }

	// pclog("What's a bitplane?\n");
}

uint8_t cpqvdu_in(uint16_t addr, void *p)
{
        cpqvdu_t *cpqvdu = (cpqvdu_t *)p;
//        pclog("cpqvdu_IN %04X\n", addr);
        switch (addr)
        {
                case 0x3D4:
                return cpqvdu->crtcreg;
                case 0x3D5:
                return cpqvdu->crtc[cpqvdu->crtcreg];
                case 0x3DA:
                return cpqvdu->cpqvdustat;
        }
        return 0xFF;
}

void cpqvdu_write(uint32_t addr, uint8_t val, void *p)
{
        cpqvdu_t *cpqvdu = (cpqvdu_t *)p;
//        pclog("CGA_WRITE %04X %02X\n", addr, val);
	if ((cs == 0xE0000) && (pc == 0xBF2F) && (romset == ROM_440FX))  return;
	if ((cs == 0xE0000) && (pc == 0xBF77) && (romset == ROM_440FX))  return;
        cpqvdu->vram[addr & 0x3fff] = val;
        cpqvdu->charbuffer[ ((int)(((cpqvdu->dispontime - cpqvdu->vidtime) * 2) / CGACONST)) & 0xfc] = val;
        cpqvdu->charbuffer[(((int)(((cpqvdu->dispontime - cpqvdu->vidtime) * 2) / CGACONST)) & 0xfc) | 1] = val;
        egawrites++;
        cycles -= 4;
	// pclog("Dots are my favours, except when they're saviour's...\n");
}

uint8_t cpqvdu_read(uint32_t addr, void *p)
{
        cpqvdu_t *cpqvdu = (cpqvdu_t *)p;
        cycles -= 4;        
	if ((cs == 0xE0000) && (pc == 0xBF2F) && (romset == ROM_440FX))  return 0xff;
	if ((cs == 0xE0000) && (pc == 0xBF77) && (romset == ROM_440FX))  return 0xff;
        cpqvdu->charbuffer[ ((int)(((cpqvdu->dispontime - cpqvdu->vidtime) * 2) / CGACONST)) & 0xfc] = cpqvdu->vram[addr & 0x3fff];
        cpqvdu->charbuffer[(((int)(((cpqvdu->dispontime - cpqvdu->vidtime) * 2) / CGACONST)) & 0xfc) | 1] = cpqvdu->vram[addr & 0x3fff];
        egareads++;
//        pclog("CGA_READ %04X\n", addr);
        return cpqvdu->vram[addr & 0x3fff];
}

void cpqvdu_recalctimings(cpqvdu_t *cpqvdu)
{
        double disptime;
	double _dispontime, _dispofftime;
#ifndef RELEASE_BUILD
        pclog("Recalc - %i %i %i\n", cpqvdu->crtc[0], cpqvdu->crtc[1], cpqvdu->cpqvdumode & 1);
#endif
        if (cpqvdu->cpqvdumode & 1)
        {
                disptime = cpqvdu->crtc[0] + 1;
                _dispontime = cpqvdu->crtc[1];
        }
        else
        {
                disptime = (cpqvdu->crtc[0] + 1) << 1;
                _dispontime = cpqvdu->crtc[1] << 1;
        }
        _dispofftime = disptime - _dispontime;
//        printf("%i %f %f %f  %i %i\n",cpqvdumode&1,disptime,dispontime,dispofftime,crtc[0],crtc[1]);
        _dispontime *= CGACONST;
        _dispofftime *= CGACONST;
//        printf("Timings - on %f off %f frame %f second %f\n",dispontime,dispofftime,(dispontime+dispofftime)*262.0,(dispontime+dispofftime)*262.0*59.92);
	cpqvdu->dispontime = (int)(_dispontime * (1 << TIMER_SHIFT) * 3.0d);
	cpqvdu->dispofftime = (int)(_dispofftime * (1 << TIMER_SHIFT) * 3.0d);
#ifndef RELEASE_BUILD
	pclog("Recalc end\n");
#endif
}

static int ntsc_col[8][8]=
{
        {0,0,0,0,0,0,0,0}, /*Black*/
        {0,0,1,1,1,1,0,0}, /*Blue*/
        {1,0,0,0,0,1,1,1}, /*Green*/
        {0,0,0,0,1,1,1,1}, /*Cyan*/
        {1,1,1,1,0,0,0,0}, /*Red*/
        {0,1,1,1,1,0,0,0}, /*Magenta*/
        {1,1,0,0,0,0,1,1}, /*Yellow*/
        {1,1,1,1,1,1,1,1}  /*White*/
};

void cpqvdu_poll(void *p)
{
        cpqvdu_t *cpqvdu = (cpqvdu_t *)p;
        uint16_t ca = (cpqvdu->crtc[15] | (cpqvdu->crtc[14] << 8)) & 0x3fff;
        int drawcursor;
        int x, c;
        int oldvc;
        uint8_t chr, attr;
        uint16_t dat;
        int cols[4];
        int col;
        int oldsc;
        int y_buf[8] = {0, 0, 0, 0, 0, 0, 0, 0}, y_val, y_tot;
        int i_buf[8] = {0, 0, 0, 0, 0, 0, 0, 0}, i_val, i_tot;
        int q_buf[8] = {0, 0, 0, 0, 0, 0, 0, 0}, q_val, q_tot;
        int r, g, b;
        if (!cpqvdu->linepos)
        {
                cpqvdu->vidtime += cpqvdu->dispofftime;
                cpqvdu->cpqvdustat |= 1;
                cpqvdu->linepos = 1;
                oldsc = cpqvdu->sc;
                if ((cpqvdu->crtc[8] & 3) == 3) 
                   cpqvdu->sc = ((cpqvdu->sc << 1) + cpqvdu->oddeven) & 7;
                if (cpqvdu->cpqvdudispon)
                {
                        if (cpqvdu->displine < cpqvdu->firstline)
                        {
                                cpqvdu->firstline = cpqvdu->displine;
//                                printf("Firstline %i\n",firstline);
                        }
                        cpqvdu->lastline = cpqvdu->displine;
                        for (c = 0; c < 8; c++)
                        {
                                if ((cpqvdu->cpqvdumode & 0x12) == 0x12)
                                {
                                        buffer->line[cpqvdu->displine][c] = 0;
                                        if (cpqvdu->cpqvdumode & 1) buffer->line[cpqvdu->displine][c + (cpqvdu->crtc[1] << 3) + 8] = 0;
                                        else                  buffer->line[cpqvdu->displine][c + (cpqvdu->crtc[1] << 4) + 8] = 0;
                                }
                                else
                                {
                                        buffer->line[cpqvdu->displine][c] = (cpqvdu->cpqvducol & 15) + 16;
                                        if (cpqvdu->cpqvdumode & 1) buffer->line[cpqvdu->displine][c + (cpqvdu->crtc[1] << 3) + 8] = (cpqvdu->cpqvducol & 15) + 16;
                                        else                  buffer->line[cpqvdu->displine][c + (cpqvdu->crtc[1] << 4) + 8] = (cpqvdu->cpqvducol & 15) + 16;
                                }
                        }
                        if (cpqvdu->cpqvdumode & 1)
                        {
                                for (x = 0; x < cpqvdu->crtc[1]; x++)
                                {
                                        chr = cpqvdu->charbuffer[x << 1];
                                        attr = cpqvdu->charbuffer[(x << 1) + 1];
                                        drawcursor = ((cpqvdu->ma == ca) && cpqvdu->con && cpqvdu->cursoron);
                                        if (cpqvdu->cpqvdumode & 0x20)
                                        {
                                                cols[1] = (attr & 15) + 16;
                                                cols[0] = ((attr >> 4) & 7) + 16;
                                                if ((cpqvdu->cpqvdublink & 8) && (attr & 0x80) && !cpqvdu->drawcursor) 
                                                        cols[1] = cols[0];
                                        }
                                        else
                                        {
                                                cols[1] = (attr & 15) + 16;
                                                cols[0] = (attr >> 4) + 16;
                                        }
                                        if (drawcursor)
                                        {
                                                for (c = 0; c < 8; c++)
                                                    buffer->line[cpqvdu->displine][(x << 3) + c + 8] = cols[(cga_fontdat[chr][cpqvdu->sc & 7] & (1 << (c ^ 7))) ? 1 : 0] ^ 15;
                                        }
                                        else
                                        {
                                                for (c = 0; c < 8; c++)
                                                    buffer->line[cpqvdu->displine][(x << 3) + c + 8] = cols[(cga_fontdat[chr][cpqvdu->sc & 7] & (1 << (c ^ 7))) ? 1 : 0];
                                        }
                                        cpqvdu->ma++;
                                }
                        }
                        else if (!(cpqvdu->cpqvdumode & 2))
                        {
                                for (x = 0; x < cpqvdu->crtc[1]; x++)
                                {
                                        chr  = cpqvdu->vram[((cpqvdu->ma << 1) & 0x3fff)];
                                        attr = cpqvdu->vram[(((cpqvdu->ma << 1) + 1) & 0x3fff)];
                                        drawcursor = ((cpqvdu->ma == ca) && cpqvdu->con && cpqvdu->cursoron);
                                        if (cpqvdu->cpqvdumode & 0x20)
                                        {
                                                cols[1] = (attr & 15) + 16;
                                                cols[0] = ((attr >> 4) & 7) + 16;
                                                if ((cpqvdu->cpqvdublink & 8) && (attr & 0x80)) cols[1] = cols[0];
                                        }
                                        else
                                        {
                                                cols[1] = (attr & 15) + 16;
                                                cols[0] = (attr >> 4) + 16;
                                        }
                                        cpqvdu->ma++;
                                        if (drawcursor)
                                        {
                                                for (c = 0; c < 8; c++)
                                                    buffer->line[cpqvdu->displine][(x << 4)+(c << 1) + 8] = buffer->line[cpqvdu->displine][(x << 4) + (c << 1) + 1 + 8] = cols[(cga_fontdat[chr][cpqvdu->sc & 7] & (1 << (c ^ 7))) ? 1 : 0] ^ 15;
                                        }
                                        else
                                        {
                                                for (c = 0; c < 8; c++)
                                                    buffer->line[cpqvdu->displine][(x << 4) + (c << 1) + 8] = buffer->line[cpqvdu->displine][(x << 4) + (c << 1) + 1 + 8] = cols[(cga_fontdat[chr][cpqvdu->sc & 7] & (1 << (c ^ 7))) ? 1 : 0];
                                        }
                                }
                        }
                        else if (!(cpqvdu->cpqvdumode & 16))
                        {
                                cols[0] = (cpqvdu->cpqvducol & 15) | 16;
                                col = (cpqvdu->cpqvducol & 16) ? 24 : 16;
                                if (cpqvdu->cpqvdumode & 4)
                                {
                                        cols[1] = col | 3;
                                        cols[2] = col | 4;
                                        cols[3] = col | 7;
                                }
                                else if (cpqvdu->cpqvducol & 32)
                                {
                                        cols[1] = col | 3;
                                        cols[2] = col | 5;
                                        cols[3] = col | 7;
                                }
                                else
                                {
                                        cols[1] = col | 2;
                                        cols[2] = col | 4;
                                        cols[3] = col | 6;
                                }
                                for (x = 0; x < cpqvdu->crtc[1]; x++)
                                {
                                        dat = (cpqvdu->vram[((cpqvdu->ma << 1) & 0x1fff) + ((cpqvdu->sc & 1) * 0x2000)] << 8) | cpqvdu->vram[((cpqvdu->ma << 1) & 0x1fff) + ((cpqvdu->sc & 1) * 0x2000) + 1];
                                        cpqvdu->ma++;
                                        for (c = 0; c < 8; c++)
                                        {
                                                buffer->line[cpqvdu->displine][(x << 4) + (c << 1) + 8] =
                                                  buffer->line[cpqvdu->displine][(x << 4) + (c << 1) + 1 + 8] = cols[dat >> 14];
                                                dat <<= 2;
                                        }
                                }
                        }
                        else
                        {
                                cols[0] = 0; cols[1] = (cpqvdu->cpqvducol & 15) + 16;
                                for (x = 0; x < cpqvdu->crtc[1]; x++)
                                {
                                        dat = (cpqvdu->vram[((cpqvdu->ma << 1) & 0x1fff) + ((cpqvdu->sc & 1) * 0x2000)] << 8) | cpqvdu->vram[((cpqvdu->ma << 1) & 0x1fff) + ((cpqvdu->sc & 1) * 0x2000) + 1];
                                        cpqvdu->ma++;
                                        for (c = 0; c < 16; c++)
                                        {
                                                buffer->line[cpqvdu->displine][(x << 4) + c + 8] = cols[dat >> 15];
                                                dat <<= 1;
                                        }
                                }
                        }
                }
                else
                {
                        cols[0] = ((cpqvdu->cpqvdumode & 0x12) == 0x12) ? 0 : (cpqvdu->cpqvducol & 15) + 16;
                        if (cpqvdu->cpqvdumode & 1) hline(buffer, 0, cpqvdu->displine, (cpqvdu->crtc[1] << 3) + 16, cols[0]);
                        else                  hline(buffer, 0, cpqvdu->displine, (cpqvdu->crtc[1] << 4) + 16, cols[0]);
                }

                if (cpqvdu->cpqvdumode & 1) x = (cpqvdu->crtc[1] << 3) + 16;
                else                  x = (cpqvdu->crtc[1] << 4) + 16;
                if (cga_comp)
                {
                        for (c = 0; c < x; c++)
                        {
                                y_buf[(c << 1) & 6] = ntsc_col[buffer->line[cpqvdu->displine][c] & 7][(c << 1) & 6] ? 0x6000 : 0;
                                y_buf[(c << 1) & 6] += (buffer->line[cpqvdu->displine][c] & 8) ? 0x3000 : 0;
                                i_buf[(c << 1) & 6] = y_buf[(c << 1) & 6] * i_filt[(c << 1) & 6];
                                q_buf[(c << 1) & 6] = y_buf[(c << 1) & 6] * q_filt[(c << 1) & 6];
                                y_tot = y_buf[0] + y_buf[1] + y_buf[2] + y_buf[3] + y_buf[4] + y_buf[5] + y_buf[6] + y_buf[7];
                                i_tot = i_buf[0] + i_buf[1] + i_buf[2] + i_buf[3] + i_buf[4] + i_buf[5] + i_buf[6] + i_buf[7];
                                q_tot = q_buf[0] + q_buf[1] + q_buf[2] + q_buf[3] + q_buf[4] + q_buf[5] + q_buf[6] + q_buf[7];

                                y_val = y_tot >> 10;
                                if (y_val > 255) y_val = 255;
                                y_val <<= 16;
                                i_val = i_tot >> 12;
                                if (i_val >  39041) i_val =  39041;
                                if (i_val < -39041) i_val = -39041;
                                q_val = q_tot >> 12;
                                if (q_val >  34249) q_val =  34249;
                                if (q_val < -34249) q_val = -34249;

                                r = (y_val + 249*i_val + 159*q_val) >> 16;
                                g = (y_val -  70*i_val - 166*q_val) >> 16;
                                b = (y_val - 283*i_val + 436*q_val) >> 16;

                                y_buf[((c << 1) & 6) + 1] = ntsc_col[buffer->line[cpqvdu->displine][c] & 7][((c << 1) & 6) + 1] ? 0x6000 : 0;
                                y_buf[((c << 1) & 6) + 1] += (buffer->line[cpqvdu->displine][c] & 8) ? 0x3000 : 0;
                                i_buf[((c << 1) & 6) + 1] = y_buf[((c << 1) & 6) + 1] * i_filt[((c << 1) & 6) + 1];
                                q_buf[((c << 1) & 6) + 1] = y_buf[((c << 1) & 6) + 1] * q_filt[((c << 1) & 6) + 1];
                                y_tot = y_buf[0] + y_buf[1] + y_buf[2] + y_buf[3] + y_buf[4] + y_buf[5] + y_buf[6] + y_buf[7];
                                i_tot = i_buf[0] + i_buf[1] + i_buf[2] + i_buf[3] + i_buf[4] + i_buf[5] + i_buf[6] + i_buf[7];
                                q_tot = q_buf[0] + q_buf[1] + q_buf[2] + q_buf[3] + q_buf[4] + q_buf[5] + q_buf[6] + q_buf[7];

                                y_val = y_tot >> 10;
                                if (y_val > 255) y_val = 255;
                                y_val <<= 16;
                                i_val = i_tot >> 12;
                                if (i_val >  39041) i_val =  39041;
                                if (i_val < -39041) i_val = -39041;
                                q_val = q_tot >> 12;
                                if (q_val >  34249) q_val =  34249;
                                if (q_val < -34249) q_val = -34249;

                                r = (y_val + 249*i_val + 159*q_val) >> 16;
                                g = (y_val -  70*i_val - 166*q_val) >> 16;
                                b = (y_val - 283*i_val + 436*q_val) >> 16;
                                if (r > 511) r = 511;
                                if (g > 511) g = 511;
                                if (b > 511) b = 511;

                                ((uint32_t *)buffer32->line[cpqvdu->displine])[c] = makecol32(r / 2, g / 2, b / 2);
                        }
                }
		else
		{
                        for (c = 0; c < x; c++)
                        {
				buffer->line[cpqvdu->displine][c] = (buffer->line[cpqvdu->displine][c] & 0xe0) | (mdacols[buffer->line[cpqvdu->displine][c] & 0xf][0][1]);
				switch(buffer->line[cpqvdu->displine][c] & 0x1f)
				{
					case 23:
						((uint32_t *)buffer32->line[cpqvdu->displine])[c] = makecol32(255, 170, 55);
						break;
					case 24:
						((uint32_t *)buffer32->line[cpqvdu->displine])[c] = makecol32(170, 85, 0);
						break;
					case 31:
						((uint32_t *)buffer32->line[cpqvdu->displine])[c] = makecol32(255, 255, 55);
						break;
					default:
						((uint32_t *)buffer32->line[cpqvdu->displine])[c] = makecol32(85, 0, 0);
						break;
				}
			}
		}

                cpqvdu->sc = oldsc;
                if (cpqvdu->vc == cpqvdu->crtc[7] && !cpqvdu->sc)
                   cpqvdu->cpqvdustat |= 8;
                cpqvdu->displine++;
                if (cpqvdu->displine >= 360) 
                        cpqvdu->displine = 0;
        }
        else
        {
                cpqvdu->vidtime += cpqvdu->dispontime;
                if (cpqvdu->cpqvdudispon) cpqvdu->cpqvdustat &= ~1;
                cpqvdu->linepos = 0;
                if (cpqvdu->vsynctime)
                {
                        cpqvdu->vsynctime--;
                        if (!cpqvdu->vsynctime)
                           cpqvdu->cpqvdustat &= ~8;
                }
                if (cpqvdu->sc == (cpqvdu->crtc[11] & 31) || ((cpqvdu->crtc[8] & 3) == 3 && cpqvdu->sc == ((cpqvdu->crtc[11] & 31) >> 1))) 
                { 
                        cpqvdu->con = 0; 
                        cpqvdu->coff = 1; 
                }
                if ((cpqvdu->crtc[8] & 3) == 3 && cpqvdu->sc == (cpqvdu->crtc[9] >> 1))
                   cpqvdu->maback = cpqvdu->ma;
                if (cpqvdu->vadj)
                {
                        cpqvdu->sc++;
                        cpqvdu->sc &= 31;
                        cpqvdu->ma = cpqvdu->maback;
                        cpqvdu->vadj--;
                        if (!cpqvdu->vadj)
                        {
                                cpqvdu->cpqvdudispon = 1;
                                cpqvdu->ma = cpqvdu->maback = (cpqvdu->crtc[13] | (cpqvdu->crtc[12] << 8)) & 0x3fff;
                                cpqvdu->sc = 0;
                        }
                }
                else if (cpqvdu->sc == cpqvdu->crtc[9])
                {
                        cpqvdu->maback = cpqvdu->ma;
                        cpqvdu->sc = 0;
                        oldvc = cpqvdu->vc;
                        cpqvdu->vc++;
                        cpqvdu->vc &= 127;

                        if (cpqvdu->vc == cpqvdu->crtc[6]) 
                                cpqvdu->cpqvdudispon = 0;

                        if (oldvc == cpqvdu->crtc[4])
                        {
                                cpqvdu->vc = 0;
                                cpqvdu->vadj = cpqvdu->crtc[5];
                                if (!cpqvdu->vadj) cpqvdu->cpqvdudispon = 1;
                                if (!cpqvdu->vadj) cpqvdu->ma = cpqvdu->maback = (cpqvdu->crtc[13] | (cpqvdu->crtc[12] << 8)) & 0x3fff;
                                if ((cpqvdu->crtc[10] & 0x60) == 0x20) cpqvdu->cursoron = 0;
                                else                                cpqvdu->cursoron = cpqvdu->cpqvdublink & 8;
                        }

                        if (cpqvdu->vc == cpqvdu->crtc[7])
                        {
                                cpqvdu->cpqvdudispon = 0;
                                cpqvdu->displine = 0;
                                cpqvdu->vsynctime = (cpqvdu->crtc[3] >> 4) + 1;
                                if (cpqvdu->crtc[7])
                                {
                                        if (cpqvdu->cpqvdumode & 1) x = (cpqvdu->crtc[1] << 3) + 16;
                                        else                  x = (cpqvdu->crtc[1] << 4) + 16;
                                        cpqvdu->lastline++;
                                        if (x != xsize || (cpqvdu->lastline - cpqvdu->firstline) != ysize)
                                        {
                                                xsize = x;
                                                ysize = cpqvdu->lastline - cpqvdu->firstline;
                                                if (xsize < 64) xsize = 656;
                                                if (ysize < 32) ysize = 200;
                                                updatewindowsize(xsize, (ysize << 1) + 16);
                                        }
                                        
startblit();
                                        video_blit_memtoscreen(0, cpqvdu->firstline - 4, 0, (cpqvdu->lastline - cpqvdu->firstline) + 8, xsize, (cpqvdu->lastline - cpqvdu->firstline) + 8);
                                        frames++;
endblit();
                                        video_res_x = xsize - 16;
                                        video_res_y = ysize;
                                        if (cpqvdu->cpqvdumode & 1)
                                        {
                                                video_res_x /= 8;
                                                video_res_y /= cpqvdu->crtc[9] + 1;
                                                video_bpp = 0;
                                        }
                                        else if (!(cpqvdu->cpqvdumode & 2))
                                        {
                                                video_res_x /= 16;
                                                video_res_y /= cpqvdu->crtc[9] + 1;
                                                video_bpp = 0;
                                        }
                                        else if (!(cpqvdu->cpqvdumode & 16))
                                        {
                                                video_res_x /= 2;
                                                video_bpp = 2;
                                        }
                                        else
                                        {
                                                video_bpp = 1;
                                        }
                                }
                                cpqvdu->firstline = 1000;
                                cpqvdu->lastline = 0;
                                cpqvdu->cpqvdublink++;
                                cpqvdu->oddeven ^= 1;
                        }
                }
                else
                {
                        cpqvdu->sc++;
                        cpqvdu->sc &= 31;
                        cpqvdu->ma = cpqvdu->maback;
                }
                if ((cpqvdu->sc == (cpqvdu->crtc[10] & 31) || ((cpqvdu->crtc[8] & 3) == 3 && cpqvdu->sc == ((cpqvdu->crtc[10] & 31) >> 1)))) 
                        cpqvdu->con = 1;
                if (cpqvdu->cpqvdudispon && (cpqvdu->cpqvdumode & 1))
                {
                        for (x = 0; x < (cpqvdu->crtc[1] << 1); x++)
                            cpqvdu->charbuffer[x] = cpqvdu->vram[(((cpqvdu->ma << 1) + x) & 0x3fff)];
                }
        }
}

void cpqvdu_init(cpqvdu_t *cpqvdu)
{
	// loadfont("mda.rom", 0);
}

void *cpqvdu_standalone_init()
{
        int c;
        int cpqvdu_tint = -2;
        cpqvdu_t *cpqvdu = malloc(sizeof(cpqvdu_t));
        memset(cpqvdu, 0, sizeof(cpqvdu_t));

        cpqvdu->vram = malloc(0x4000);
                
        for (c = 0; c < 8; c++)
        {
                i_filt[c] = 512.0 * cos((3.14 * (cpqvdu_tint + c * 4) / 16.0) - 33.0 / 180.0);
                q_filt[c] = 512.0 * sin((3.14 * (cpqvdu_tint + c * 4) / 16.0) - 33.0 / 180.0);
        }

        for (c = 0; c < 256; c++)
        {
                mdacols[c][0][0] = mdacols[c][1][0] = mdacols[c][1][1] = 16;
                if (c & 8) mdacols[c][0][1] = 15 + 16;
                else       mdacols[c][0][1] =  7 + 16;
        }
        mdacols[0x70][0][1] = 16;
        mdacols[0x70][0][0] = mdacols[0x70][1][0] = mdacols[0x70][1][1] = 16 + 15;
        mdacols[0xF0][0][1] = 16;
        mdacols[0xF0][0][0] = mdacols[0xF0][1][0] = mdacols[0xF0][1][1] = 16 + 15;
        mdacols[0x78][0][1] = 16 + 7;
        mdacols[0x78][0][0] = mdacols[0x78][1][0] = mdacols[0x78][1][1] = 16 + 15;
        mdacols[0xF8][0][1] = 16 + 7;
        mdacols[0xF8][0][0] = mdacols[0xF8][1][0] = mdacols[0xF8][1][1] = 16 + 15;
        mdacols[0x00][0][1] = mdacols[0x00][1][1] = 16;
        mdacols[0x08][0][1] = mdacols[0x08][1][1] = 16 + 8;
        mdacols[0x80][0][1] = mdacols[0x80][1][1] = 16;
        mdacols[0x88][0][1] = mdacols[0x88][1][1] = 16 + 8;

        timer_add(cpqvdu_poll, &cpqvdu->vidtime, TIMER_ALWAYS_ENABLED, cpqvdu);
        mem_mapping_add(&cpqvdu->mapping, 0xb8000, 0x08000, cpqvdu_read, NULL, NULL, cpqvdu_write, NULL, NULL,  NULL, 0, cpqvdu);
        io_sethandler(0x03d0, 0x0010, cpqvdu_in, NULL, NULL, cpqvdu_out, NULL, NULL, cpqvdu);
        return cpqvdu;
}

void cpqvdu_speed_changed(void *p)
{
        cpqvdu_t *cpqvdu = (cpqvdu_t *)p;
        
        cpqvdu_recalctimings(cpqvdu);
}

void cpqvdu_close(void *p)
{
        cpqvdu_t *cpqvdu = (cpqvdu_t *)p;

        free(cpqvdu->vram);
        free(cpqvdu);
}

device_t cpqvdu_device =
{
        "Compaq VDU",
        0,
        cpqvdu_standalone_init,
        cpqvdu_close,
        NULL,
        cpqvdu_speed_changed,
        NULL,
        NULL
};
