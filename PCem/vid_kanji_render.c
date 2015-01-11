#include <stdio.h>
#include <stdlib.h>
#include "ibm.h"
#include "mem.h"
#include "video.h"
#include "vid_svga.h"
#include "vid_svga_render.h"
#include "vid_kanji_render.h"

uint8_t sbcs_chars19[256][19];
uint16_t dbcs_chars16[256][256][2][19];
uint8_t dbcs_chstat[256][256];

int last_was_dbcs = 2;
uint8_t cur_lead = 0;
uint8_t cur_trail = 0;

void svga_dbcs_fonts_load()
{
	FILE *f;
	int i = 0;
	int j = 0;
	int k = 0;
	int l = 0;
	uint32_t boffs = 0;
	uint32_t coffs = 0;
	uint32_t cboffs = 0;
	uint8_t ccount = 0;
	uint8_t trail_start = 0;
	uint8_t lead_start = 0;
	uint8_t trail_end = 0;
	uint8_t lead_end = 0;
	// First, initialize everything
	for (i = 0; i < 256; i++)
	{
		for (j = 0; j < 19; j++)
		{
			sbcs_chars19[i][j]  = 0;
		}
	}
	for (i = 0; i < 256; i++)
	{
		for (j = 0; j < 256; j++)
		{
			for (k = 0; k < 19; k++)
			{
				dbcs_chars16[i][j][0][k]  = 0;
				dbcs_chars16[i][j][1][k]  = 0;
			}
			dbcs_chstat[i][j] = 0;
		}
	}
	// Next, do the actual loading
	f = fopen("JHSLCC19.FNT", "rb");
	if (f != NULL)
	{
		// fseek(f, 0x16, SEEK_SET);
		fseek(f, 0x11, SEEK_SET);
		for (i = 0; i < 256; i++)
		{
			for (j = 0; j < 19; j++)
			{
				sbcs_chars19[i][j] = fgetc(f);
			}
		}
		fclose(f);
	}
	// Number of codes at 0x16
	// Codes at 0x17 - 0x31A
	// Bitmaps at 0x620
	f = fopen("SJIS_D16.FNT", "rb");
	if (f != NULL)
	{
		fseek(f, 0x14, SEEK_SET);
		fread(&boffs, 2, 1, f);
		fseek(f, 0x16, SEEK_SET);
		ccount = fgetc(f);
		cboffs = boffs;
		for (i = 0; i < ccount; i++)
		{
			coffs = 0x17 + (i * 4);
			fseek(f, coffs, SEEK_SET);
			trail_start = fgetc(f);
			lead_start = fgetc(f);
			trail_end = fgetc(f);
			lead_end = fgetc(f);
			fseek(f, cboffs, SEEK_SET);
			pclog("Loading DBCS bitmaps for characters from (%02X %02X) to (%02X to %02X) starting from offset %08X\n", lead_start, trail_start, lead_end, trail_end, cboffs);
			for (j = trail_start; j <= trail_end; j++)
			{
				for(k = 0; k < 16; k++)
				{
					dbcs_chars16[lead_start][j][0][k+1] = fgetc(f);
					dbcs_chars16[lead_start][j][1][k+1] = fgetc(f);
				}
				dbcs_chstat[lead_start][j] = 1;
			}
			cboffs = ftell(f);
		}
		fclose(f);
	}
}

void svga_render_dbcs_40(svga_t *svga)
{     
        if (svga->firstline_draw == 2000) 
                svga->firstline_draw = svga->displine;
        svga->lastline_draw = svga->displine;
        
        if (svga->fullchange)
        {
                uint32_t *p = &((uint32_t *)buffer32->line[svga->displine])[32];
                int x, xx;
                int drawcursor;
                uint8_t chr, chrn, attr, dat;
                uint32_t charaddr;
                int fg, bg;
                // int xinc = (svga->seqregs[1] & 1) ? 16 : 18;
		int xinc = 16;
                
                for (x = 0; x < svga->hdisp; x += xinc)
                {
                        drawcursor = ((svga->ma == svga->ca) && svga->con && svga->cursoron);
                        chr  = svga->vram[(svga->ma << 1) & svga->vrammask];
			chrn = 0;
			if (last_was_dbcs == 2)
			{
				if ((x + xinc) < svga->hdisp)  chrn = svga->vram[((svga->ma + 4) << 1) & svga->vrammask];
				if (dbcs_chstat[chr][chrn])
				{
					last_was_dbcs = 0;
					cur_lead = chr;
					cur_trail = chrn;
				}
			}
			else
			{
				last_was_dbcs = 1;
			}
                        attr = svga->vram[((svga->ma << 1) + 4) & svga->vrammask];

                        if (drawcursor) 
                        { 
                                bg = svga->pallook[svga->egapal[attr & 15]]; 
                                fg = svga->pallook[svga->egapal[attr >> 4]]; 
                        }
                        else
                        {
                                fg = svga->pallook[svga->egapal[attr & 15]];
                                bg = svga->pallook[svga->egapal[attr >> 4]];
                                if (attr & 0x80 && svga->attrregs[0x10] & 8)
                                {
                                        bg = svga->pallook[svga->egapal[(attr >> 4) & 7]];
                                        if (svga->blink & 16) 
                                                fg = bg;
                                }
                        }

			if (last_was_dbcs == 2)
			{
				dat = sbcs_chars19[chr][svga->sc];
			}
			else
			{
				dat = dbcs_chars16[cur_lead][cur_trail][last_was_dbcs][svga->sc];
				if (last_was_dbcs == 1)  last_was_dbcs = 2;
			}
			for (xx = 0; xx < 16; xx += 2) 
				p[xx] = p[xx + 1] = (dat & (0x80 >> (xx >> 1))) ? fg : bg;
                        svga->ma += 4; 
                        p += xinc;
                }
                svga->ma &= svga->vrammask;
        }
}

void svga_render_dbcs_80(svga_t *svga)
{
        if (svga->firstline_draw == 2000)
                svga->firstline_draw = svga->displine;
        svga->lastline_draw = svga->displine;
        
        if (svga->fullchange)
        {
                uint32_t *p = &((uint32_t *)buffer32->line[svga->displine])[32];
                int x, xx;
                int drawcursor;
                uint8_t chr, chrn, attr, dat;
                uint32_t charaddr;
                int fg, bg;
                // int xinc = (svga->seqregs[1] & 1) ? 8 : 9;
		int xinc = 8;

                for (x = 0; x < svga->hdisp; x += xinc)
                {
                        drawcursor = ((svga->ma == svga->ca) && svga->con && svga->cursoron);
                        chr  = svga->vram[(svga->ma << 1) & svga->vrammask];
			chrn = 0;
			if (last_was_dbcs == 2)
			{
				if ((x + xinc) < svga->hdisp)  chrn = svga->vram[((svga->ma + 4) << 1) & svga->vrammask];
				if (dbcs_chstat[chr][chrn])
				{
					last_was_dbcs = 0;
					cur_lead = chr;
					cur_trail = chrn;
				}
			}
			else
			{
				last_was_dbcs = 1;
			}
                        attr = svga->vram[((svga->ma << 1) + 4) & svga->vrammask];

                        if (drawcursor) 
                        { 
                                bg = svga->pallook[svga->egapal[attr & 15]]; 
                                fg = svga->pallook[svga->egapal[attr >> 4]]; 
                        }
                        else
                        {
                                fg = svga->pallook[svga->egapal[attr & 15]];
                                bg = svga->pallook[svga->egapal[attr >> 4]];
                                if (attr & 0x80 && svga->attrregs[0x10] & 8)
                                {
                                        bg = svga->pallook[svga->egapal[(attr >> 4) & 7]];
                                        if (svga->blink & 16) 
                                                fg = bg;
                                }
                        }

			if (last_was_dbcs == 2)
			{
				dat = sbcs_chars19[chr][svga->sc];
			}
			else
			{
				dat = dbcs_chars16[cur_lead][cur_trail][last_was_dbcs][svga->sc];
				if (last_was_dbcs == 1)  last_was_dbcs = 2;
			}
			for (xx = 0; xx < 8; xx++) 
				p[xx] = (dat & (0x80 >> xx)) ? fg : bg;
                        svga->ma += 4; 
                        p += xinc;
                }
                svga->ma &= svga->vrammask;
        }
}
