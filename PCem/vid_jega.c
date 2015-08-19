/*JEGA emulation*/
#include <stdio.h>
#include <stdlib.h>
#include "ibm.h"
#include "device.h"
#include "io.h"
#include "mem.h"
#include "rom.h"
#include "timer.h"
#include "video.h"
#include "vid_jega.h"

/*	Differences between the two modes:

	Superimpose mode:	Text mode, masking background of each character to the graphics buffer;
	Virtual text mode:	Graphics mode, masking characters onto the graphics buffer.	*/

extern uint8_t edatlookup[4][4];

static uint8_t jega_rotate[8][256];

static uint32_t pallook8[256], pallook16[256], pallook32[256], pallook64[256];

int nec_compat;

int was_dbcs = 0;

// 225 + 50 = 275 - 256 = 19
// 200, 250, 350, 400, 480
uint16_t resolutions[5][4] = {{260, 199, 225, 224}, {310, 249, 275, 274}, {364, 349, 350, 350}, {0x1BF, 0x18F, 0x19C, 0x196}, {0x20F, 0x1DF, 0x1FC, 0x1F6}};
// 8x8, 8x10, 8x14, 8x16, 8x19
uint8_t charmodes[5][4] = {{7, 6, 7, 8}, {9, 6, 7, 10}, {13, 6, 7, 15}, {15, 13, 14, 31}, {18, 16, 17, 34}};

/*3C2 controls default mode on EGA. On VGA, it determines monitor type (mono or colour)*/
int jegaswitchread,jegaswitches=9; /*7=CGA mode (200 lines), 9=EGA mode (350 lines), 8=EGA mode (200 lines)*/
/*0111, 1001, 1000*/
/*Pseudo-JEGA adds A=JEGA mode (480 lines), B=Low-res kanji mode (400 lines)*/

void jega_defaults(jega_t *jega)
{
	jega->mode = 0;
	jega->dispmode = 0;
	jega->egamparams = 0x717;
	jega->jegamparams = 0;
	jega->colorbufferseg = 0xB800;
	jega->monobufferseg = 0xB000;
	jega->mwtarget = NULL;
	jega->mrsource = NULL;
	jega->mwcurpos = 0;
	jega->mrcurpos = 0;
	// jega->cr[1] = 1;
	jega->cr[1] = 0x51;
	jega->cr[2] = 0;
	jega->cr[3] = 3;
	jega->cr[4] = 0xC;
	jega->cr[5] = 0x10;
	jega->seqregs[1] |= 1;
	jega->lastegacharheight = 14;
	jega->lastjegacharheight = 19;
}

int underline_loc(jega_t *jega)
{
	return jega->crtc[0x14];
}

void jega_forcemode(jega_t *jega, uint8_t val)
{
	int vtotal = 0;
	int dispend = 0;
	int vsyncstart = 0;
	int vblankstart = 0;

	return;

        vtotal = jega->crtc[6];
        dispend = jega->crtc[0x12];
        vsyncstart = jega->crtc[0x10];
        vblankstart = jega->crtc[0x15];

        if (jega->crtc[7] & 1)  vtotal |= 0x100;
        if (jega->crtc[7] & 32)  vtotal |= 0x200;
        vtotal++;

        if (jega->crtc[7] & 2)  dispend |= 0x100;
        if (jega->crtc[7] & 64)  dispend |= 0x200;
        dispend++;

        if (jega->crtc[7] & 4)   vsyncstart |= 0x100;
        if (jega->crtc[7] & 128)  vsyncstart |= 0x200;
        vsyncstart++;

        if (jega->crtc[7] & 8)   vblankstart |= 0x100;
        if (jega->crtc[9] & 32)  vblankstart |= 0x200;
        vblankstart++;

	pclog("Force mode: Old values are: vtotal %u, dispend %u (%02X), vsyncstart %u\n", vtotal, dispend, jega->crtc[0x12], vsyncstart);

	if (jega->gdcreg[6] & 1)  return;

	return;

	if (vtotal == 365)
	{
		jega->crtc[6] = 0xBF;
		jega->crtc[0x10] = 0x9C;
		jega->crtc[0x12] = 0x8F;
		jega->crtc[0x15] = 0x96;
		jega->crtc[0x16] = 0xB9;
		jega->crtc[7] = 0x1F;
		jega->crtc[9] = 0x0F;
		jega->crtc[0xA] = 0x0D;
		jega->crtc[0xB] = 0x0E;
		jega->crtc[0x11] = 0x8E;
		jega->crtc[0x14] = 0x1F;
	}

	if (dispend == 350)
	{
		jega->crtc[6] = 0xBF;
		jega->crtc[0x10] = 0x9C;
		jega->crtc[0x12] = 0x8F;
		jega->crtc[0x15] = 0x96;
		jega->crtc[0x16] = 0xB9;
		jega->crtc[7] = 0x1F;
		jega->crtc[9] = 0x0F;
		jega->crtc[0xA] = 0x0D;
		jega->crtc[0xB] = 0x0E;
		jega->crtc[0x11] = 0x8E;
		jega->crtc[0x14] = 0x1F;
	}

	if (vsyncstart == 351)
	{
		jega->crtc[6] = 0xBF;
		jega->crtc[0x10] = 0x9C;
		jega->crtc[0x12] = 0x8F;
		jega->crtc[0x15] = 0x96;
		jega->crtc[0x16] = 0xB9;
		jega->crtc[7] = 0x1F;
		jega->crtc[9] = 0x0F;
		jega->crtc[0xA] = 0x0D;
		jega->crtc[0xB] = 0x0E;
		jega->crtc[0x11] = 0x8E;
		jega->crtc[0x14] = 0x1F;
	}

	if (vblankstart == 351)
	{
		jega->crtc[6] = 0xBF;
		jega->crtc[0x10] = 0x9C;
		jega->crtc[0x12] = 0x8F;
		jega->crtc[0x15] = 0x96;
		jega->crtc[0x16] = 0xB9;
		jega->crtc[7] = 0x1F;
		jega->crtc[9] = 0x0F;
		jega->crtc[0xA] = 0x0D;
		jega->crtc[0xB] = 0x0E;
		jega->crtc[0x11] = 0x8E;
		jega->crtc[0x14] = 0x1F;
	}

	if (jega->crtc[0x12] != 0x5D)  if (vsyncstart != 351)  return;

	return;

	// if (jega->cr[2] & 0x40)
	// {
		// [6] = 0x18, [7] = 000? ?001, [0x10] = 0xFA, [0x12] = 0xF9
		// jega->crtc[6] = 8;
		/* jega->crtc[6] = 8;
		jega->crtc[7] = 0x11;
		jega->crtc[0x10] = 0x7D;
		jega->crtc[0x12] = 0x63; */

		jega->crtc[0] = 0x70;
		jega->crtc[2] = 0x5C;
		jega->crtc[3] = 0x2F;
		jega->crtc[4] = 0x5F;
		jega->crtc[5] = 7;
		jega->crtc[6] = 4;
		jega->crtc[7] = 0x11;
		jega->crtc[9] = 7;
		jega->crtc[0xA] = 6;
		jega->crtc[0xB] = 7;
		jega->crtc[0x10] = 0xE1;
		jega->crtc[0x11] = 0x24;
		jega->crtc[0x12] = 0xC7;
		jega->crtc[0x14] = 8;	// Underline!
		jega->crtc[0x15] = 0xE0;	// Blanking
		jega->crtc[0x16] = 0xF0;
		jega->seqregs[4] = 0x23;

		jega->cr[2] |= 0x80;
	// }
	
	return;
	if ((!(jega->gdcreg[6] & 1)) && (jega->crtc[0x10] = 0x5E))
	{
		jega->crtc[0] = 0x5F;
		jega->crtc[1] = 0x4F;
		jega->crtc[2] = 0x50;
		jega->crtc[3] = 0x82;
		jega->crtc[4] = 0x55;
		jega->crtc[5] = 0x81;
		// wtf
		jega->crtc[6] = 0xBF;
		jega->crtc[7] = 0x1F;
		jega->crtc[8] = 0x00;
		jega->crtc[9] = 0x4F;
		jega->crtc[0xA] = 0x0D;
		jega->crtc[0xB] = 0x0E;
		jega->crtc[0xC] = 0;
		jega->crtc[0xD] = 0;
		jega->crtc[0xE] = 0;
		jega->crtc[0xF] = 0;
		jega->crtc[0x10] = 0x9C;
		jega->crtc[0x11] = 0x8E;
		jega->crtc[0x12] = 0x8F;
		jega->crtc[0x13] = 0x28;
		jega->crtc[0x14] = 0x1F;
		jega->crtc[0x15] = 0x96;
		jega->crtc[0x16] = 0xB9;
		jega->crtc[0x17] = 0xA3;
		jega->crtc[0x18] = 0xFF;
	}
}

void jega_dbcs_fonts_load(jega_t *jega)
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
	uint32_t vram_pos = 0;
	// First, initialize everything
	for (i = 0; i < 4; i++)
	{
		for (j = 0; j < 256; j++)
		{
			for (k = 0; k < 19; k++)
			{
				jega->dbcs_userchars16[i][j][0][k]  = 0;
				jega->dbcs_userchars16[i][j][1][k]  = 0;
			}
			jega->dbcs_userchstat[i][j] = 0;
		}
	}
	for (i = 0; i < 2; i++)
	{
		jega->lastdbcs[i] = 0;
		jega->lastsbcs[i] = 0;
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
				jega->vram[0x80000 + (i * 19) + j] = fgetc(f);
			}
		}
		fclose(f);
	}
	for (i = 0x80; i < 0xFF; i++)
	{
		vram_pos = 0x82000 + ((lead_start & 0x7F) * 207 * 33);
		for (j = 0x30; j < 0xFF; j++)
		{
			jega->vram[vram_pos + ((j - 0x30) * 33)] = 0;
		}
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
			vram_pos = 0x82000 + ((lead_start & 0x7F) * 207 * 33);
			pclog("Loading DBCS bitmaps for characters from (%02X %02X) to (%02X to %02X) starting from offset %08X to starting from offset %08X\n", lead_start, trail_start, lead_end, trail_end, cboffs, vram_pos + ((trail_start - 0x30) * 33));
			for (j = trail_start; j <= trail_end; j++)
			{
				for(k = 1; k < 33; k++)
				{
					jega->vram[vram_pos + ((j - 0x30) * 33) + k] = fgetc(f);
				}
				jega->vram[vram_pos + ((j - 0x30) * 33)] = 1;
			}
			cboffs = ftell(f);
		}
		for (i = 0xF0; i < 0xF4; i++)
		{
			vram_pos = 0x82000 + ((lead_start & 0x7F) * 207 * 33);
			for (j = 0x30; j < 0xFF; j++)
			{
				jega->vram[vram_pos + ((j - 0x30) * 33)] |= 2;
			}
		}
		fclose(f);
	}
}

int enable_kanji(jega_t *jega)
{
	// If we're in 9 pixels character width mode, return 0.
	if (!(jega->seqregs[1] & 1))  return 0;
	// If we're in graphics mode, return 0, except for virtual text mode.
	if (jega->cr[7] & 32)  return 1;
	if (jega->gdcreg[6] & 1)  return 0;
	// If character height is not 19, return 0.
	if ((jega->crtc[9] & 0x1F) != 0x12)  return 0;
	return 1;
}

uint8_t sbcs_dat(jega_t *jega, uint8_t c, uint8_t sc)
{
	uint32_t vram_pos = 0;
	vram_pos = 0x80000 + (c * 19);
	return jega->vram[vram_pos + sc];
}

uint8_t dbcs_dat(jega_t *jega, uint8_t lead, uint8_t trail, uint8_t sc, uint8_t side)
{
	uint32_t vram_pos = 0;
	if ((lead < 0x81) || (lead > 0xFE))
	{
		if (side)
		{
			return sbcs_dat(jega, trail, sc);
		}
		else
		{
			return sbcs_dat(jega, lead, sc);
		}
	}
	if ((trail < 0x30) || (trail > 0xFE) || (trail == 0x7E))
	{
		if (side)
		{
			return sbcs_dat(jega, trail, sc);
		}
		else
		{
			return sbcs_dat(jega, lead, sc);
		}
	}
	if ((sc == 0) || (sc > 0x10))  return 0;
	vram_pos = 0x82000 + ((lead & 0x7F) * 207 * 33) + ((trail - 0x30) * 33);
	return jega->vram[vram_pos + 1 + ((sc - 1) << 1) + side];
}

uint8_t dbcs_stat(jega_t *jega, uint8_t lead, uint8_t trail)
{
	uint32_t vram_pos = 0;
	if ((lead < 0x81) || (lead > 0xFE))  return 0;
	if ((trail < 0x30) || (trail > 0xFE) || (trail == 0x7E))  return 0;
	vram_pos = 0x82000 + ((lead & 0x7F) * 207 * 33) + ((trail - 0x30) * 33);
	return jega->vram[vram_pos];
}

void write_gdcreg(jega_t *jega, uint8_t addr, uint8_t val)
{
	int c = 0;
	int old = 0;
	int o = 0;

	jega->gdcreg[addr & 15] = val;
	switch (addr & 15)
	{
		case 2: 
		jega->colourcompare = val; 
		break;
		case 4: 
		jega->readplane = val & 3; 
		break;
		case 5: 
		jega->writemode = val & 3;
		jega->readmode = val & 8; 
		break;
		case 6:
		jega->chain2 = val & 2;
		if (jega->cr[7] & 32)
		{
			if ((val & 0xc) >= 0x8)
			{
				mem_mapping_set_addr(&jega->mapping, 0xa0000, 0x10000);
				break;
			}
		}
		if (jega->cr[4] & 4)
		{
			if ((val & 0xc) == 0x8)
			{
				mem_mapping_set_addr(&jega->mapping, 0xa0000, 0x18000);
			}
			else if ((val & 0xc) == 0xC)
			{
				mem_mapping_set_addr(&jega->mapping, 0xa0000, 0x1ffc0);
			}
			break;
		}
		switch (val & 0xc)
		{
			// Bit 3 is 0 for graphics mode
			case 0x0: /*128k at A0000*/
			// mem_mapping_set_addr(&jega->mapping, 0xa0000, 0x20000);
			mem_mapping_set_addr(&jega->mapping, 0xa0000, 0x1ffc0);
			break;
			case 0x4: /*64k at A0000*/
			mem_mapping_set_addr(&jega->mapping, 0xa0000, 0x10000);
			break;
			case 0x8: /*32k at B0000*/
			mem_mapping_set_addr(&jega->mapping, 0xb0000, 0x08000);
			break;
			case 0xC: /*32k at B8000*/
			// mem_mapping_set_addr(&jega->mapping, 0xb8000, 0x08000);
			mem_mapping_set_addr(&jega->mapping, 0xb8000, 0x07fc0);
			break;
		}
		break;
		case 7: 
		jega->colournocare = val; 
		break;
	}
}

void jega_process_config(jega_t *jega, uint8_t nval)
{
	int c = 0;
	int ccrattr = jega->ccr - 0x6C;
	uint8_t val = 0;

	pclog("Processing config, CR%02X = %02X\n", jega->ccr, val);

	if ((jega->ccr >= 0x6C) && (jega->ccr <= 0x7F))
	{
		if ((jega->rconfig) || (jega->cr[2] & 8) || (jega->forceattr))  return;
		jega->attrregs[ccrattr & 31] = val;
		if (ccrattr < 16) 
			fullchange = changeframecount;
		if (ccrattr == 0x10 || ccrattr == 0x14 || ccrattr < 0x10)
		{
			for (c = 0; c < 16; c++)
			{
				/* if (jega->attrregs[0x10] & 0x80) jega->jegapal[c] = (jega->attrregs[c] &  0xf) | ((jega->attrregs[0x14] & 0xf) << 4);
				else                            */ jega->jegapal[c] = (jega->attrregs[c] & 0x3f) | ((jega->attrregs[0x14] & 0xc) << 4);
			}
		}
		return;
	}
	else if (jega->ccr > 0x7F)
	{
		jega->storage[jega->ccr & 0x7F] = nval;
	}
	else
	{
		val = jega->cr[jega->ccr];
		if (jega->ccr != 2)
		{
			if (jega->ccr == 6)
			{
				jega->cr[jega->ccr] &= 1;
				jega->cr[jega->ccr] |= (nval & 0xFE);
			}
			else
			{
				jega->cr[jega->ccr] = nval;
			}
			if (jega->ccr == 4)
			{
				// Addressing mode not settable by software
				jega->cr[jega->ccr] &= 0xFD;
				if (jega->gdcreg[6] & 1)
				{
					if (!(jega->cr[7] & 32))  jega->cr[jega->ccr] &= 0xFB;
				}
			}
		}
		else
		{
			jega->cr[jega->ccr] &= 0x80;
			jega->cr[jega->ccr] |= (nval & 0x7F);
		}

		if ((jega->ccr == 1) && (val ^ jega->cr[1]))
		{
			jega->seqregs[1] &= 0xFE;
			if ((nval != 0x51) && !(jega->cr[4] & 4))  jega->seqregs[1] |= (jega->cr[6] & 1);
			// jega_switchmode(jega);
		}
		if ((jega->ccr == 2) && ((val & 0x7F) ^ (jega->cr[2] & 0x7F)))
		{
			// jega_switchmode(jega);

			if (jega->cr[2] & 6)
			{
				jega->forceattr = 1;
			}
			else
			{
				jega->forceattr = 0;
			}

			if (jega->cr[2] & 0x20)
			{
				jega->forceattr = 1;
			}
			else
			{
				jega->forceattr = 1;
			}
		}
		if ((jega->ccr == 4) && (val ^ jega->cr[4]))
		{
			jega->seqregs[1] &= 0xFE;
			if (!nval && (jega->cr[1] != 0x51))
			{
				jega->seqregs[1] |= (jega->cr[6] & 1);
			}
		}
		if (jega->cr[4] & 0x10)
		{
			jega->pallook = pallook8;
		}
		else
		{
			if (jega->cr[5] & 0x10)
			{
				jega->pallook = pallook32;
			}
			else
			{
				jega->pallook = pallook16;
			}
		}
		if ((jega->ccr == 7) && ((val ^ jega->cr[7]) & 32))
		{
			if (jega->cr[4] & 4)
			{
				jega->attrregs[0x10] |= ((jega->cr[7] & 32) ? 1 : 0);
				write_gdcreg(jega, 6, ((jega->gdcreg[6]) & 0xfe) | ((jega->cr[7] & 32) ? 1 : 0));
				jega_recalctimings(jega);
			}
		}
		if ((jega->ccr == 4) && ((val ^ jega->cr[4]) & 4))
		{
			write_gdcreg(jega, 6, jega->gdcreg[6]);
			jega_recalctimings(jega);
		}
		if ((jega->ccr == 4) && ((val ^ jega->cr[4]) & 8))
		{
			if (jega->cr[4] & 8)
			{
				mem_mapping_enable(&jega->necmapping);
				mem_mapping_set_addr(&jega->necmapping, 0xe0000, 0x10000);
				// Turn on so the BIOS writes to the graphics buffer
				// The BIOS then clears the bit after setting the mode
				jega->cr[5] |= 8;
			}
			else
			{
				mem_mapping_disable(&jega->necmapping);
			}
		}
		/* if ((jega->ccr == 4) && ((val ^ jega->cr[4]) & 2))
		{
			write_gdcreg(jega, 6, jega->gdcreg[6]);
		} */
		if ((jega->ccr >= 1) && (jega->ccr <= 6))
		{
			jega_recalctimings(jega);
		}
	}
}

void jega_out(uint16_t addr, uint8_t val, void *p)
{
        jega_t *jega = (jega_t *)p;
        int c;
        uint8_t o, old;

	if (jega->blockwrite)  return;
        
        if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(jega->miscout & 1)) 
                addr ^= 0x60;
        
        switch (addr)
        {
                case 0x3c0:
                if (!jega->attrff)
                   jega->attraddr = val & 31;
                else
                {
			if ((jega->rconfig) || (jega->cr[2] & 8) || (jega->forceattr))
			{
				jega->attrff ^= 1;
				break;
			}
			pclog("Setting attribute register %02X to %02X\n", jega->attraddr & 31, val);
			if (jega->attraddr == 0x10)
			{
				if (jega->cr[4] & 4)
				{
                        		jega->attrregs[jega->attraddr & 31] = (val & 0xfe) | ((jega->cr[7] & 32) ? 1 : 0);
				}
				else
				{
                        		jega->attrregs[jega->attraddr & 31] = val;
				}
			}
			else
			{
                        	jega->attrregs[jega->attraddr & 31] = val;
			}
                        if (jega->attraddr < 16) 
                                fullchange = changeframecount;
                        if (jega->attraddr == 0x10 || jega->attraddr == 0x14 || jega->attraddr < 0x10)
                        {
                                for (c = 0; c < 16; c++)
                                {
                                        /* if (jega->attrregs[0x10] & 0x80) jega->jegapal[c] = (jega->attrregs[c] &  0xf) | ((jega->attrregs[0x14] & 0xf) << 4);
                                        else                            */ jega->jegapal[c] = (jega->attrregs[c] & 0x3f) | ((jega->attrregs[0x14] & 0xc) << 4);
                                }
                        }
                }
                jega->attrff ^= 1;
                break;
                case 0x3c2:
                jegaswitchread = val & 0xc;
                jega->vres = !(val & 0x80);
                jega->pallook = jega->vres ? pallook16 : pallook64;
		if (jega->cr[5] & 0x10)  jega->pallook = pallook32;
		if (jega->cr[4] & 0x10)  jega->pallook = pallook8;
                jega->vidclock = val & 4; printf("3C2 write %02X\n",val);
                jega->miscout=val;
                break;
                case 0x3c4: 
                jega->seqaddr = val; 
                break;
                case 0x3c5:
                o = jega->seqregs[jega->seqaddr & 0xf];
                jega->seqregs[jega->seqaddr & 0xf] = val;
		// In superimpose mode, always force this to 1
		if (jega->cr[4] & 4)  jega->seqregs[1] |= 1;
		// Same with country being Japan
		if (jega->cr[1] == 0x51)  jega->seqregs[1] |= 1;
		if ((jega->seqaddr & 0xf) == 1)  jega->cr[6] = (val & 1);
		pclog("Setting SEQ register %02X to %02X\n", jega->seqaddr & 0xf, val);
                if (o != val && (jega->seqaddr & 0xf) == 1) 
                        jega_recalctimings(jega);
                switch (jega->seqaddr & 0xf)
                {
                        case 1: 
                        if (jega->scrblank && !(val & 0x20)) 
                                fullchange = 3; 
                        jega->scrblank = (jega->scrblank & ~0x20) | (val & 0x20); 
                        break;
                        case 2: 
                        // jega->writemask = val & 0xf; 
                        jega->writemask = val; 
                        break;
                        case 3:
                        jega->charsetb = (((val >> 2) & 3) * 0x10000) + 2;
                        jega->charseta = ((val & 3)        * 0x10000) + 2;
                        break;
                }
                break;
                case 0x3ce: 
                jega->gdcaddr = val; 
                break;
                case 0x3cf:
		pclog("Setting GDC register %02X to %02X\n", jega->gdcaddr & 15, val);
		if (jega->gdcaddr == 6)
		{
			if (jega->cr[4] & 4)
			{
				write_gdcreg(jega, jega->gdcaddr, (val & 0xfe) | ((jega->cr[7] & 32) ? 1 : 0));
			}
			else
			{
				write_gdcreg(jega, jega->gdcaddr, val);
			}
		}
		else
		{
			write_gdcreg(jega, jega->gdcaddr, val);
		}
                break;
                case 0x3d4:
                        pclog("Write 3d4 %02X  %04X:%04X\n", val, CS, pc);
                // jega->crtcreg = val & 31;
		jega->crtcreg = val & 0xff;
                return;
                case 0x3d5:
                        pclog("Write 3d5 %02X %02X %02X\n", jega->crtcreg, val, jega->crtc[0x11]);
//                if (jega->crtcreg == 1 && val == 0x14)
//                        fatal("Here\n");
                if (jega->crtcreg <= 7 && jega->crtc[0x11] & 0x80) return;
                old = jega->crtc[jega->crtcreg];
                jega->crtc[jega->crtcreg] = val;
		// if (jega->crtc[0x10] == 0x5E)  jega_forcemode(jega, val);
		// if (jega->crtc[0x12] == 0x5D)  jega_forcemode(jega, val);
                if (old != val)
                {
                        if (jega->crtcreg < 0xe || jega->crtcreg > 0x10)
                        {
                                fullchange = changeframecount;
                                jega_recalctimings(jega);
                        }
                }
		jega_forcemode(jega, val);
		return;
		case 0x3d6:
		jega->ccr = val;
		return;
		case 0x3d7:
		// Added Hangeul AX support
		if ((jega->ccr == 1) && (val != 1) && (val != 0x51) && (val != 0x52))  return;
		if ((jega->ccr == 2) && ((jega->cr[2] ^ val) & 1) && (jega->cr[1] != 0x51) && (jega->cr[1] != 0x52))  return;

		jega_process_config(jega, val);
		return;
                break;
        }
}

uint8_t jega_in(uint16_t addr, void *p)
{
        jega_t *jega = (jega_t *)p;

if (addr != 0x3da && addr != 0x3ba)
        pclog("jega_in %04X\n", addr);
        if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(jega->miscout & 1)) 
                addr ^= 0x60;

        switch (addr)
        {
                case 0x3c0: 
                return jega->attraddr;
                case 0x3c1: 
                return jega->attrregs[jega->attraddr];
                case 0x3c2:
//                printf("Read jegaswitch %02X %02X %i\n",jegaswitchread,jegaswitches,VGA);
                switch (jegaswitchread)
                {
                        case 0xc: return (jegaswitches & 1) ? 0x10 : 0;
                        case 0x8: return (jegaswitches & 2) ? 0x10 : 0;
                        case 0x4: return (jegaswitches & 4) ? 0x10 : 0;
                        case 0x0: return (jegaswitches & 8) ? 0x10 : 0;
                }
                break;
                case 0x3c4: 
                return jega->seqaddr;
                case 0x3c5:
                return jega->seqregs[jega->seqaddr & 0xf];
                case 0x3ce: 
                return jega->gdcaddr;
                case 0x3cf:
                return jega->gdcreg[jega->gdcaddr & 0xf];
                case 0x3d4:
                return jega->crtcreg;
                case 0x3d5:
		printf("Reading CRTC %02X: %02X\n", jega->crtcreg, jega->crtc[jega->crtcreg]);
                return jega->crtc[jega->crtcreg];
		case 0x3d6:
		return jega->ccr;
		case 0x3d7:
		if (jega->ccr < 0x6C)
		{
			return jega->cr[jega->ccr];
		}
		else if ((jega->ccr >= 0x6C) && (jega->ccr <= 0x7F))
		{
			return jega->attrregs[jega->ccr - 0x6C];
		}
		else
		{
			return jega->storage[jega->ccr & 0x7F];
		}
                case 0x3da:
                jega->attrff = 0;
                jega->stat ^= 0x30; /*Fools IBM EGA video BIOS self-test*/
                return jega->stat;
        }
//        printf("Bad EGA read %04X %04X:%04X\n",addr,cs>>4,pc);
        return 0xff;
}

void jega_recalctimings(jega_t *jega)
{
	double _dispontime, _dispofftime, disptime;
        double crtcconst;

	if (jega->blockwrite)  return;

	was_dbcs = 0;

        jega->vtotal = jega->crtc[6];
        jega->dispend = jega->crtc[0x12];
        jega->vsyncstart = jega->crtc[0x10];
	if (jega->vsyncstart > 250)
	{
		jega->cr[2] &= 0x7F;
		jega->vres &= 0xBF;
		jega->vres |= ((jega->cr[2] & 0x80) >> 1);
	}
        jega->split = jega->crtc[0x18];
        jega->vblankstart = jega->crtc[0x15];

        if (jega->crtc[7] & 1)  jega->vtotal |= 0x100;
        if (jega->crtc[7] & 32)  jega->vtotal |= 0x200;
        jega->vtotal++;
	// if (jega->vtotal == 365)  set_vtotal(jega, 264);

        if (jega->crtc[7] & 2)  jega->dispend |= 0x100;
        if (jega->crtc[7] & 64)  jega->dispend |= 0x200;
        jega->dispend++;
	// if (jega->dispend == 350)  set_dispend(jega, 249);

        if (jega->crtc[7] & 4)   jega->vsyncstart |= 0x100;
        if (jega->crtc[7] & 128)  jega->vsyncstart |= 0x200;
        jega->vsyncstart++;
	// if (jega->vsyncstart == 351)  set_vsyncstart(jega, 250);

        if (jega->crtc[7] & 0x10) jega->split |= 0x100;
        if (jega->crtc[9] & 64)  jega->split |= 0x200;
        jega->split+=2;

        if (jega->crtc[7] & 0x08) jega->vblankstart |= 0x100;
        if (jega->crtc[9] & 32)  jega->vblankstart |= 0x200;
        jega->vblankstart++;
	// if (jega->vblankstart == 350)  set_vblankstart(jega, 249);

	// if ((jega->crtc[9] & 0x1f) == 0xd)  set_charheight(jega, 9, 8, 9, 10);

        jega->hdisp = jega->crtc[1];
        jega->hdisp++;

        jega->rowoffset = jega->crtc[0x13];

        printf("Recalc! %i %i %i %i   %i %02X\n", jega->vtotal, jega->dispend, jega->vsyncstart, jega->split, jega->hdisp, jega->attrregs[0x16]);

        if (jega->vblankstart < jega->dispend)
//                jega->dispend = jega->vblankstart;
//                jega->vblankstart = jega->dispend;
		pclog("vblankstart is %u\n", jega->vblankstart);

	crtcconst = (jega->seqregs[1] & 1) ? (((jega->vidclock) ? VGACONST2 : VGACONST1) * 8.0) : (((jega->vidclock) ? VGACONST2 : VGACONST1) * 9.0);

        /* if (jega->vidclock) crtcconst = (jega->seqregs[1] & 1) ? MDACONST : (MDACONST * (9.0 / 8.0));
        else               crtcconst = (jega->seqregs[1] & 1) ? CGACONST : (CGACONST * (9.0 / 8.0));

        disptime = jega->crtc[0] + 2;
        _dispontime = jega->crtc[1] + 1;

        printf("Disptime %f dispontime %f hdisp %i\n", disptime, _dispontime, jega->crtc[1] * 8);
        if (jega->seqregs[1] & 8) 
        { 
                disptime*=2; 
                _dispontime*=2; 
        }
        _dispofftime = disptime - _dispontime;
        _dispontime  *= crtcconst;
        _dispofftime *= crtcconst;

	jega->dispontime  = (int)(_dispontime  * (1 << TIMER_SHIFT));
	jega->dispofftime = (int)(_dispofftime * (1 << TIMER_SHIFT));
        pclog("dispontime %i (%f)  dispofftime %i (%f)\n", jega->dispontime, (float)jega->dispontime / (1 << TIMER_SHIFT),
                                                           jega->dispofftime, (float)jega->dispofftime / (1 << TIMER_SHIFT)); */

        // disptime  = jega->htotal;
	disptime = jega->crtc[0] + 2;
        _dispontime = jega->hdisp;
	// _dispontime = jega->crtc[1] + 1;
        
//        printf("Disptime %f dispontime %f hdisp %i\n",disptime,dispontime,crtc[1]*8);
        if (jega->seqregs[1] & 8) { disptime *= 2; _dispontime *= 2; }
        _dispofftime = disptime - _dispontime;
        _dispontime *= crtcconst;
        _dispofftime *= crtcconst;

	jega->dispontime = (int)(_dispontime * (1 << TIMER_SHIFT));
	jega->dispofftime = (int)(_dispofftime * (1 << TIMER_SHIFT));

//        printf("EGA horiz total %i display end %i clock rate %i vidclock %i %i\n",crtc[0],crtc[1],jegaswitchread,vidclock,((jega3c2>>2)&3) | ((tridentnewctrl2<<2)&4));
//        printf("EGA vert total %i display end %i max row %i vsync %i\n",jega_vtotal,jega_dispend,(crtc[9]&31)+1,jega_vsyncstart);
//        printf("total %f on %f cycles off %f cycles frame %f sec %f %02X\n",disptime*crtcconst,dispontime,dispofftime,(dispontime+dispofftime)*jega_vtotal,(dispontime+dispofftime)*jega_vtotal*70,seqregs[1]);
}

uint8_t nec_color(uint8_t attr)
{
	return (attr >> 5);
}

uint8_t get_charwidth(jega_t *jega)
{
	return (jega->seqregs[1] & 1) ? 8 : 9;
}

uint32_t get_pixelcolor(jega_t *jega, uint8_t cchar, uint8_t curpix)
{
	uint32_t paddr = (jega->displine * jega->hdisp * get_charwidth(jega)) + (cchar * get_charwidth(jega));
	uint8_t pixbit = (1 << (7 - curpix));
	uint8_t bits = 0;
	/* if (jega->seqregs[1] & 8)
	{
		paddr >>= 4;
	}
	else
	{
		paddr >>= 3;
	} */
	paddr >>= 3;
	paddr <<= 2;
	paddr |= 0x40000;
	bits = (jega->vram[paddr] & pixbit) ? 1 : 0;
	bits |= (jega->vram[paddr + 1] & pixbit) ? 2 : 0;
	bits |= (jega->vram[paddr + 2] & pixbit) ? 4 : 0;
	bits |= (jega->vram[paddr + 3] & pixbit) ? 8 : 0;
	return jega->pallook[jega->jegapal[bits]];
}

uint32_t effective(jega_t *jega, uint32_t color, uint8_t cchar, uint8_t curpix)
{
	// Make sure superimpose is *ALWAYS* off in 9 pixels per character mode
	// return color;
	if (!(jega->seqregs[1] & 1))
	{
		return color;
	}
	if (jega->cr[4] & 4)
	{
		if (color == 0)
		{
			return get_pixelcolor(jega, cchar, curpix);
		}
		else
		{
			return color;
		}
	}
	else
	{
		pclog("Returning color %08X\n", color);
		return color;
	}
}

uint8_t get_bg(jega_t *jega, uint8_t attr)
{
	if (jega->cr[2] & 0x10)
	{
		if (attr & 4)
		{
			return nec_color(attr);
		}
		else
		{
			return 0;
		}
	}
	else
	{
		return (attr >> 4);
	}
}

uint8_t get_fg(jega_t *jega, uint8_t attr)
{
	if (jega->cr[2] & 0x10)
	{
		if (attr & 4)
		{
			return 0;
		}
		else
		{
			return nec_color(attr);
		}
	}
	else
	{
		return (attr & 15);
	}
}

uint8_t is_blink(jega_t *jega, uint8_t attr)
{
	if (jega->cr[2] & 0x10)
	{
		return (attr & 2);
	}
	else
	{
		// return (attr & 0x80 && jega->attrregs[0x10] & 8);
		return 0;
	}
}

uint8_t is_disable(jega_t *jega, uint8_t attr)
{
	if (jega->cr[2] & 0x10)
	{
		return (attr & 1);
	}
	else
	{
		return 0;
	}
}

uint8_t curdat = 0;
uint8_t olddat = 0;

uint8_t lastlead = 0;
uint8_t lasttrail = 0;

int is_vt_mode(jega_t *jega)
{
	return (jega->cr[7] & 32);
}

uint8_t sc(uint8_t dat, int xx, uint32_t fg, uint32_t bg)
{
	return ((dat & (0x80 >> xx)) ? fg : bg);
}

void jega_render_text(jega_t *jega)
{
        uint8_t chr, nchr, nchrpos, dat, attr;
        uint32_t charaddr;
        int x, xx;
        uint32_t fg, bg;
        int offset;
        uint8_t edat[4];
        int drawcursor = 0;
	int ad = 0;

	if (fullchange)
	{
		was_dbcs = 0;
		for (x = 0; x < jega->hdisp; x++)
		{
			drawcursor = ((jega->vtma == jega->ca) && jega->con && jega->cursoron);
			ad = (jega->vtma << 1) & jega->vrammask;
			chr  = jega->vram[(jega->vtma << 1) & jega->vrammask];
			if (enable_kanji(jega))
			{
				nchr = 0;
				nchrpos = 2;
				if (jega->cr[4] & 1)  nchrpos = 1;
				if ((jega->hdisp - x) > 1)  nchr = jega->vram[((jega->vtma << 1) + nchrpos) & jega->vrammask];
			}
			if (jega->cr[4] & 1)
			{
				attr = jega->vram[((jega->vtma << 1) + 0x2000) & jega->vrammask];
			}
			else
			{
				attr = jega->vram[((jega->vtma << 1) + 1) & jega->vrammask];
			}

			if (attr & 8) charaddr = jega->charsetb + (chr * 128);
			else          charaddr = jega->charseta + (chr * 128);

			if (drawcursor) 
			{ 
				bg = jega->pallook[jega->jegapal[get_fg(jega, attr)]]; 
				fg = jega->pallook[jega->jegapal[get_bg(jega, attr)]]; 
			}
			else
			{
				fg = jega->pallook[jega->jegapal[get_fg(jega, attr)]];
				bg = jega->pallook[jega->jegapal[get_bg(jega, attr)]];
				if (is_blink(jega, attr))
				{
					bg = jega->pallook[jega->jegapal[get_bg(jega, attr) & 7]];
					if (jega->blink & 16) 
					fg = bg;
				}
				if (is_disable(jega, attr))
				{
					fg = bg;
				}
			}

			dat = jega->vram[charaddr + (jega->sc << 2)];
			if (enable_kanji(jega))
			{
				if (was_dbcs == 1)
				{
					was_dbcs = 2;
					dat = dbcs_dat(jega, lastlead, lasttrail, jega->sc, was_dbcs - 1);
				}
				else
				{
					lastlead = 0;
					lasttrail = 0;
					if (dbcs_stat(jega, chr, nchr) & 1)
					{
						lastlead = chr;
						lasttrail = nchr;
						was_dbcs = 1;
						dat = dbcs_dat(jega, lastlead, lasttrail, jega->sc, was_dbcs - 1);
					}
					else
					{
						dat = sbcs_dat(jega, chr, jega->sc);
						was_dbcs = 0;
					}
				}
			}
			else
			{
				was_dbcs = 0;
			}

			if (jega->seqregs[1] & 8)
			{
				if (jega->seqregs[1] & 1) 
				{
					for (xx = 0; xx < 8; xx++) 
						((uint32_t *)buffer32->line[jega->displine])[((x << 4) + 32 + (xx << 1)) & 2047] =
							((uint32_t *)buffer32->line[jega->displine])[((x << 4) + 33 + (xx << 1)) & 2047] = (dat & (0x80 >> xx)) ? effective(jega, fg, x, xx) : effective(jega, bg, x, xx); 
				}
				else
				{
					for (xx = 0; xx < 8; xx++) 
						((uint32_t *)buffer32->line[jega->displine])[((x * 18) + 32 + (xx << 1)) & 2047] = 
							((uint32_t *)buffer32->line[jega->displine])[((x * 18) + 33 + (xx << 1)) & 2047] = (dat & (0x80 >> xx)) ? effective(jega, fg, x, xx) : effective(jega, bg, x, xx);
					if ((chr & ~0x1f) != 0xc0 || !(jega->attrregs[0x10] & 4)) 
						((uint32_t *)buffer32->line[jega->displine])[((x * 18) + 32 + 16) & 2047] = 
							((uint32_t *)buffer32->line[jega->displine])[((x * 18) + 32 + 17) & 2047] = effective(jega, bg, x, xx);
					else                  
						((uint32_t *)buffer32->line[jega->displine])[((x * 18) + 32 + 16) & 2047] = 
							((uint32_t *)buffer32->line[jega->displine])[((x * 18) + 32 + 17) & 2047] = (dat & 1) ? effective(jega, fg, x, xx) : effective(jega, bg, x, xx);
				}
			}
			else
			{
				if (jega->seqregs[1] & 1) 
				{ 
					for (xx = 0; xx < 8; xx++) 
						if (!((sc(dat, xx, fg, bg) == 0) && (jega->cr[7] & 32)))
							((uint32_t *)buffer32->line[jega->displine])[((x << 3) + 32 + xx) & 2047] = (dat & (0x80 >> xx)) ? effective(jega, fg, x, xx) : effective(jega, bg, x, xx); 
				}
				else
				{
					for (xx = 0; xx < 8; xx++) 
						((uint32_t *)buffer32->line[jega->displine])[((x * 9) + 32 + xx) & 2047] = (dat & (0x80 >> xx)) ? effective(jega, fg, x, xx) : effective(jega, bg, x, xx);
					if ((chr & ~0x1f) != 0xc0 || !(jega->attrregs[0x10] & 4)) 
						((uint32_t *)buffer32->line[jega->displine])[((x * 9) + 32 + 8) & 2047] = effective(jega, bg, x, xx);
					else
						((uint32_t *)buffer32->line[jega->displine])[((x * 9) + 32 + 8) & 2047] = (dat & 1) ? effective(jega, fg, x, xx) : effective(jega, bg, x, xx);
				}
			}

			if (jega->cr[4] & 1)
			{
				jega->vtma += 2;
				jega->ma += 2;
			}
			else
			{
				jega->vtma += 4;
				jega->ma += 4;
			}
			if (was_dbcs == 2)
			{
				was_dbcs = 0;
			}

			jega->vtma &= jega->vrammask;
			if (!(is_vt_mode(jega)))  jega->ma &= jega->vrammask;
		}
	}
}

void jega_render_graphics(jega_t *jega)
{
        uint8_t chr, nchr, nchrpos, dat, attr;
        uint32_t charaddr;
        int x, xx;
        uint32_t fg, bg;
        int offset;
        uint8_t edat[4];
        int drawcursor = 0;
	int ad = 0;

	if (jega->cr[7] & 32)  jega->ma |= 0x40000;
	switch (jega->gdcreg[5] & 0x20)
	{
		case 0x00:
		if (jega->seqregs[1] & 8)
		{
			offset = ((8 - jega->scrollcache) << 1) + 16;
			for (x = 0; x <= jega->hdisp; x++)
			{
				if (jega->sc & 1 && !(jega->crtc[0x17] & 1))
				{
					edat[0] = jega->vram[jega->ma | 0x8000];
					edat[1] = jega->vram[jega->ma | 0x8001];
					edat[2] = jega->vram[jega->ma | 0x8002];
					edat[3] = jega->vram[jega->ma | 0x8003];
				}
				else
				{
					edat[0] = jega->vram[jega->ma];
					edat[1] = jega->vram[jega->ma | 0x1];
					edat[2] = jega->vram[jega->ma | 0x2];
					edat[3] = jega->vram[jega->ma | 0x3];
				}
				jega->ma += 4; 
				jega->ma &= jega->vrammask;

				dat = edatlookup[edat[0] & 3][edat[1] & 3] | (edatlookup[edat[2] & 3][edat[3] & 3] << 2);
				((uint32_t *)buffer32->line[jega->displine])[(x << 4) + 14 + offset] = ((uint32_t *)buffer32->line[jega->displine])[(x << 4) + 15 + offset] = jega->pallook[jega->jegapal[(dat & 0xf) & jega->attrregs[0x12]]];
				((uint32_t *)buffer32->line[jega->displine])[(x << 4) + 12 + offset] = ((uint32_t *)buffer32->line[jega->displine])[(x << 4) + 13 + offset] = jega->pallook[jega->jegapal[(dat >> 4)  & jega->attrregs[0x12]]];
				dat = edatlookup[(edat[0] >> 2) & 3][(edat[1] >> 2) & 3] | (edatlookup[(edat[2] >> 2) & 3][(edat[3] >> 2) & 3] << 2);
				((uint32_t *)buffer32->line[jega->displine])[(x << 4) + 10 + offset] = ((uint32_t *)buffer32->line[jega->displine])[(x << 4) + 11 + offset] = jega->pallook[jega->jegapal[(dat & 0xf) & jega->attrregs[0x12]]];
				((uint32_t *)buffer32->line[jega->displine])[(x << 4) +  8 + offset] = ((uint32_t *)buffer32->line[jega->displine])[(x << 4) +  9 + offset] = jega->pallook[jega->jegapal[(dat >> 4)  & jega->attrregs[0x12]]];
				dat = edatlookup[(edat[0] >> 4) & 3][(edat[1] >> 4) & 3] | (edatlookup[(edat[2] >> 4) & 3][(edat[3] >> 4) & 3] << 2);
				((uint32_t *)buffer32->line[jega->displine])[(x << 4) +  6 + offset] = ((uint32_t *)buffer32->line[jega->displine])[(x << 4) +  7 + offset] = jega->pallook[jega->jegapal[(dat & 0xf) & jega->attrregs[0x12]]];
				((uint32_t *)buffer32->line[jega->displine])[(x << 4) +  4 + offset] = ((uint32_t *)buffer32->line[jega->displine])[(x << 4) +  5 + offset] = jega->pallook[jega->jegapal[(dat >> 4)  & jega->attrregs[0x12]]];
				dat = edatlookup[edat[0] >> 6][edat[1] >> 6] | (edatlookup[edat[2] >> 6][edat[3] >> 6] << 2);
				((uint32_t *)buffer32->line[jega->displine])[(x << 4) +  2 + offset] = ((uint32_t *)buffer32->line[jega->displine])[(x << 4) +  3 + offset] = jega->pallook[jega->jegapal[(dat & 0xf) & jega->attrregs[0x12]]];
				((uint32_t *)buffer32->line[jega->displine])[(x << 4) +      offset] = ((uint32_t *)buffer32->line[jega->displine])[(x << 4) +  1 + offset] = jega->pallook[jega->jegapal[(dat >> 4)  & jega->attrregs[0x12]]];
			}
		}
		else
		{
			offset = (8 - jega->scrollcache) + 24;
			for (x = 0; x <= jega->hdisp; x++)
			{
				if (jega->sc & 1 && !(jega->crtc[0x17] & 1))
				{
					edat[0] = jega->vram[jega->ma | 0x8000];
					edat[1] = jega->vram[jega->ma | 0x8001];
					edat[2] = jega->vram[jega->ma | 0x8002];
					edat[3] = jega->vram[jega->ma | 0x8003];
				}
				else
				{
					edat[0] = jega->vram[jega->ma];
					edat[1] = jega->vram[jega->ma | 0x1];
					edat[2] = jega->vram[jega->ma | 0x2];
					edat[3] = jega->vram[jega->ma | 0x3];
				}
				jega->ma += 4; 
				jega->ma &= jega->vrammask;

				dat = edatlookup[edat[0] & 3][edat[1] & 3] | (edatlookup[edat[2] & 3][edat[3] & 3] << 2);
				((uint32_t *)buffer32->line[jega->displine])[(x << 3) + 7 + offset] = jega->pallook[jega->jegapal[(dat & 0xf) & jega->attrregs[0x12]]];
				((uint32_t *)buffer32->line[jega->displine])[(x << 3) + 6 + offset] = jega->pallook[jega->jegapal[(dat >> 4)  & jega->attrregs[0x12]]];
				dat = edatlookup[(edat[0] >> 2) & 3][(edat[1] >> 2) & 3] | (edatlookup[(edat[2] >> 2) & 3][(edat[3] >> 2) & 3] << 2);
				((uint32_t *)buffer32->line[jega->displine])[(x << 3) + 5 + offset] = jega->pallook[jega->jegapal[(dat & 0xf) & jega->attrregs[0x12]]];
				((uint32_t *)buffer32->line[jega->displine])[(x << 3) + 4 + offset] = jega->pallook[jega->jegapal[(dat >> 4)  & jega->attrregs[0x12]]];
				dat = edatlookup[(edat[0] >> 4) & 3][(edat[1] >> 4) & 3] | (edatlookup[(edat[2] >> 4) & 3][(edat[3] >> 4) & 3] << 2);
				((uint32_t *)buffer32->line[jega->displine])[(x << 3) + 3 + offset] = jega->pallook[jega->jegapal[(dat & 0xf) & jega->attrregs[0x12]]];
				((uint32_t *)buffer32->line[jega->displine])[(x << 3) + 2 + offset] = jega->pallook[jega->jegapal[(dat >> 4)  & jega->attrregs[0x12]]];
				dat = edatlookup[edat[0] >> 6][edat[1] >> 6] | (edatlookup[edat[2] >> 6][edat[3] >> 6] << 2);
				((uint32_t *)buffer32->line[jega->displine])[(x << 3) + 1 + offset] = jega->pallook[jega->jegapal[(dat & 0xf) & jega->attrregs[0x12]]];
				((uint32_t *)buffer32->line[jega->displine])[(x << 3) +     offset] = jega->pallook[jega->jegapal[(dat >> 4)  & jega->attrregs[0x12]]];
			}
		}
		break;
		case 0x20:
		offset = ((8 - jega->scrollcache) << 1) + 16;
		for (x = 0; x <= jega->hdisp; x++)
		{
			if (jega->sc & 1 && !(jega->crtc[0x17] & 1))
			{
				edat[0] = jega->vram[(jega->ma << 1) + 0x8000];
				edat[1] = jega->vram[(jega->ma << 1) + 0x8004];
			}
			else
			{
				edat[0] = jega->vram[(jega->ma << 1)];
				edat[1] = jega->vram[(jega->ma << 1) + 4];
			}
			jega->ma += 4; 
			jega->ma &= jega->vrammask;

			((uint32_t *)buffer32->line[jega->displine])[(x << 4) + 14 + offset]=  ((uint32_t *)buffer32->line[jega->displine])[(x << 4) + 15 + offset] = jega->pallook[jega->jegapal[edat[1] & 3]];
			((uint32_t *)buffer32->line[jega->displine])[(x << 4) + 12 + offset] = ((uint32_t *)buffer32->line[jega->displine])[(x << 4) + 13 + offset] = jega->pallook[jega->jegapal[(edat[1] >> 2) & 3]];
			((uint32_t *)buffer32->line[jega->displine])[(x << 4) + 10 + offset] = ((uint32_t *)buffer32->line[jega->displine])[(x << 4) + 11 + offset] = jega->pallook[jega->jegapal[(edat[1] >> 4) & 3]];
			((uint32_t *)buffer32->line[jega->displine])[(x << 4) +  8 + offset] = ((uint32_t *)buffer32->line[jega->displine])[(x << 4) +  9 + offset] = jega->pallook[jega->jegapal[(edat[1] >> 6) & 3]];
			((uint32_t *)buffer32->line[jega->displine])[(x << 4) +  6 + offset] = ((uint32_t *)buffer32->line[jega->displine])[(x << 4) +  7 + offset] = jega->pallook[jega->jegapal[(edat[0] >> 0) & 3]];
			((uint32_t *)buffer32->line[jega->displine])[(x << 4) +  4 + offset] = ((uint32_t *)buffer32->line[jega->displine])[(x << 4) +  5 + offset] = jega->pallook[jega->jegapal[(edat[0] >> 2) & 3]];
			((uint32_t *)buffer32->line[jega->displine])[(x << 4) +  2 + offset] = ((uint32_t *)buffer32->line[jega->displine])[(x << 4) +  3 + offset] = jega->pallook[jega->jegapal[(edat[0] >> 4) & 3]];
			((uint32_t *)buffer32->line[jega->displine])[(x << 4) +      offset] = ((uint32_t *)buffer32->line[jega->displine])[(x << 4) +  1 + offset] = jega->pallook[jega->jegapal[(edat[0] >> 6) & 3]];
		}
		break;
	}
}

void jega_poll(void *p)
{
        jega_t *jega = (jega_t *)p;
	jega_t *jega_bak = (jega_t *) malloc(sizeof(jega_t));
        uint8_t chr, nchr, nchrpos, dat, attr;
        uint32_t charaddr;
        int x, xx;
        uint32_t fg, bg;
        int offset;
        uint8_t edat[4];
        int drawcursor = 0;
	int ad = 0;

        if (!jega->linepos)
        {
                jega->vidtime += jega->dispofftime;

                jega->stat |= 1;
                jega->linepos = 1;

                if (jega->dispon)
                {
                        if (jega->firstline == 2000) 
                                jega->firstline = jega->displine;

                        if (jega->scrblank)
                        {
                                for (x = 0; x < jega->hdisp; x++)
                                {
                                        switch (jega->seqregs[1] & 9)
                                        {
                                                case 0:
                                                for (xx = 0; xx < 9; xx++)  ((uint32_t *)buffer32->line[jega->displine])[(x * 9) + xx + 32] = 0;
                                                break;
                                                case 1:
                                                for (xx = 0; xx < 8; xx++)  ((uint32_t *)buffer32->line[jega->displine])[(x * 8) + xx + 32] = 0;
                                                break;
                                                case 8:
                                                for (xx = 0; xx < 18; xx++) ((uint32_t *)buffer32->line[jega->displine])[(x * 18) + xx + 32] = 0;
                                                break;
                                                case 9:
                                                for (xx = 0; xx < 16; xx++) ((uint32_t *)buffer32->line[jega->displine])[(x * 16) + xx + 32] = 0;
                                                break;
                                        }
                                }
                        }
                        else if (!(jega->gdcreg[6] & 1))
                        {
				// if (jega->cr[7] & 32)  jega_render_graphics(jega);
				jega_render_text(jega);
                        }
                        else
                        {
				if (jega->cr[7] & 32)
				{
					jega_render_text(jega);
				}
				else
				{
					jega_render_graphics(jega);
				}
                        }
                        if (jega->lastline < jega->displine) 
                                jega->lastline = jega->displine;
                }

                jega->displine++;
                if ((jega->stat & 8) && ((jega->displine & 15) == (jega->crtc[0x11] & 15)) && jega->vslines)
                        jega->stat &= ~8;
                jega->vslines++;
                if (jega->displine > 500) 
                        jega->displine = 0;
        }
        else
        {
                jega->vidtime += jega->dispontime;
//                if (output) printf("Display on %f\n",vidtime);
                if (jega->dispon) 
                        jega->stat &= ~1;
                jega->linepos = 0;
                if (jega->sc == (jega->crtc[11] & 31)) 
                   jega->con = 0; 
                if (jega->dispon)
                {
                        if (jega->sc == (jega->crtc[9] & 31))
                        {
                                jega->sc = 0;

                                jega->maback += (jega->rowoffset << 3);
                                jega->maback &= jega->vrammask;
                                jega->ma = jega->maback;
				jega->vtma = jega->ma;
				if (is_vt_mode(jega))  jega->ma |= 0x40000;
                        }
                        else
                        {
                                jega->sc++;
                                jega->sc &= 31;
                                jega->ma = jega->maback;
				jega->vtma = jega->ma;
				if (is_vt_mode(jega))  jega->ma |= 0x40000;
                        }
                }
                jega->vc++;
                jega->vc &= 1023;
//                printf("Line now %i %i ma %05X\n",vc,displine,ma);
                if (jega->vc == jega->split)
                {
//                        printf("Split at line %i %i\n",displine,vc);
                        jega->vtma = jega->ma = jega->maback = 0;
			if (is_vt_mode(jega))  jega->ma |= 0x40000;
                        if (jega->attrregs[0x10] & 0x20)
                                jega->scrollcache = 0;
                }
                if (jega->vc == jega->dispend)
                {
//                        printf("Display over at line %i %i\n",displine,vc);
                        jega->dispon=0;
                        if (jega->crtc[10] & 0x20) jega->cursoron = 0;
                        else                      jega->cursoron = jega->blink & 16;
                        if ((!(jega->gdcreg[6] & 1) || (jega->cr[7] & 32)) && !(jega->blink & 15)) 
                                fullchange = 2;
                        jega->blink++;

                        if (fullchange) 
                                fullchange--;
                }
                if (jega->vc == jega->vsyncstart)
                {
                        int wx, wy;
                        jega->dispon = 0;
//                        printf("Vsync on at line %i %i\n",displine,vc);
                        jega->stat |= 8;
                        if (jega->seqregs[1] & 8) x = jega->hdisp * ((jega->seqregs[1] & 1) ? 8 : 9) * 2;
                        else                     x = jega->hdisp * ((jega->seqregs[1] & 1) ? 8 : 9);
//                        pclog("Cursor %02X %02X\n",crtc[10],crtc[11]);
//                        pclog("Firstline %i Lastline %i wx %i %i\n",firstline,lastline,wx,oddeven);
//                        doblit();
                        if (x != xsize || (jega->lastline - jega->firstline) != ysize)
                        {
                                xsize = x;
                                ysize = jega->lastline - jega->firstline;
				// printf("jega %u %u\n", jega->lastline, jega->firstline);
                                if (xsize < 64) xsize = 656;
                                if (ysize < 32) ysize = 200;
                                if (jega->vres || (ysize <= 300))
                                        updatewindowsize(xsize, ysize << 1);
                                else
                                        updatewindowsize(xsize, ysize);
				// updatewindowsize(xsize, (int) (((float) xsize / 4.0) * 3));
                        }
                                        
startblit();
                        video_blit_memtoscreen(32, 0, jega->firstline, jega->lastline, xsize, jega->lastline - jega->firstline);
endblit();

                        frames++;
                        
                        jega->video_res_x = wx;
                        jega->video_res_y = wy + 1;
                        if (!(jega->gdcreg[6] & 1) || (jega->cr[7] & 32)) /*Text and virtual text modes*/
                        {
                                jega->video_res_x /= (jega->seqregs[1] & 1) ? 8 : 9;
                                jega->video_res_y /= (jega->crtc[9] & 31) + 1;
                                jega->video_bpp = 0;
                        }
                        else
                        {
                                if (jega->crtc[9] & 0x80)
                                   jega->video_res_y /= 2;
                                if (!(jega->crtc[0x17] & 1))
                                   jega->video_res_y *= 2;
                                jega->video_res_y /= (jega->crtc[9] & 31) + 1;                                   
                                if (jega->seqregs[1] & 8)
                                   jega->video_res_x /= 2;
                                jega->video_bpp = (jega->gdcreg[5] & 0x20) ? 2 : 4;
                        }

//                        wakeupblit();
                        readflash=0;
                        //framecount++;
                        jega->firstline = 2000;
                        jega->lastline = 0;

                        jega->maback = jega->ma = jega->vtma = (jega->crtc[0xc] << 8)| jega->crtc[0xd];
                        jega->ca = (jega->crtc[0xe] << 8) | jega->crtc[0xf];
                        jega->ma <<= 2;
			if (is_vt_mode(jega))  jega->ma |= 0x40000;
                        jega->maback <<= 2;
			if (is_vt_mode(jega))  jega->maback |= 0x40000;
			jega->vtma <<= 2;
                        jega->ca <<= 2;
                        changeframecount = 2;
                        jega->vslines = 0;
                }
                if (jega->vc == jega->vtotal)
                {
                        jega->vc = 0;
                        jega->sc = 0;
                        jega->dispon = 1;
                        jega->displine = 0;
                        jega->scrollcache = jega->attrregs[0x13] & 7;
                }
                if (jega->sc == (jega->crtc[10] & 31)) 
                        jega->con = 1;
        }
}

void jega_buffer_write(uint32_t addr, uint8_t val, void *p)
{
	return;
}

void jega_write(uint32_t addr, uint8_t val, void *p)
{
        jega_t *jega = (jega_t *)p;
        uint8_t vala, valb, valc, vald, vale, valf, valg, valh;
	int writemask2 = jega->writemask;
	uint32_t oa = addr;

	// Ignore writes to B000-B7FF in AT-compatible superimpose mode
	if (((jega->cr[4] & 6) == 4) && ((addr & 0xFFFF8000) == 0xB0000))
	{
		jega->extram[addr & 0x7fff] = val;
		return;
	}

	if ((addr & 0xFFFF0000) == 0xE0000)  return;

        egawrites++;
        cycles -= video_timing_b;
        cycles_lost += video_timing_b;
        
       	if (addr >= 0xE0000) addr &= 0xffff;
       	else if (addr >= 0xB0000) addr &= 0x7fff;
       	else                 addr &= 0xffff;

        if (jega->chain2)
        {
                writemask2 &= ~0xa;
                if (addr & 1)
                        writemask2 <<= 1;
                addr &= ~1;
        }

        addr <<= 2;

	if (!(jega->gdcreg[6] & 1) || (jega->cr[7] & 32)) 
                fullchange = 2;
        /* if (!(jega->gdcreg[6] & 1)) 
                pclog ("Write mode is: %u\n", jega->writemode); */

	if (jega->cr[5] & 8)
	{
		addr |= 0x40000;
	}
	/* if (jega->cr[4] & 4)
	{
		if (oa <= 0xB0000)  addr |= 0x40000;
	} */
	if (oa >= 0xD0000)  addr |= 0x40000;

//        pclog("%i %08X %i %i %02X   %02X %02X %02X %02X\n",chain4,addr,writemode,writemask,gdcreg[8],vram[0],vram[1],vram[2],vram[3]);
        switch (jega->writemode)
        {
                case 1:
                if (writemask2 & 1) jega->vram[addr]       = jega->la;
                if (writemask2 & 2) jega->vram[addr | 0x1] = jega->lb;
                if (writemask2 & 4) jega->vram[addr | 0x2] = jega->lc;
                if (writemask2 & 8) jega->vram[addr | 0x3] = jega->ld;
                break;
                case 0:
                if (jega->gdcreg[3] & 7) 
                        val = jega_rotate[jega->gdcreg[3] & 7][val];
                        
                if (jega->gdcreg[8] == 0xff && !(jega->gdcreg[3] & 0x18) && !jega->gdcreg[1])
                {
                        if (writemask2 & 1) jega->vram[addr]       = val;
                        if (writemask2 & 2) jega->vram[addr | 0x1] = val;
                        if (writemask2 & 4) jega->vram[addr | 0x2] = val;
                        if (writemask2 & 8) jega->vram[addr | 0x3] = val;
                }
                else
                {
                        if (jega->gdcreg[1] & 1) vala = (jega->gdcreg[0] & 1) ? 0xff : 0;
                        else                    vala = val;
                        if (jega->gdcreg[1] & 2) valb = (jega->gdcreg[0] & 2) ? 0xff : 0;
                        else                    valb = val;
                        if (jega->gdcreg[1] & 4) valc = (jega->gdcreg[0] & 4) ? 0xff : 0;
                        else                    valc = val;
                        if (jega->gdcreg[1] & 8) vald = (jega->gdcreg[0] & 8) ? 0xff : 0;
                        else                    vald = val;
//                                pclog("Write %02X %01X %02X %02X %02X %02X  %02X\n",gdcreg[3]&0x18,writemask,vala,valb,valc,vald,gdcreg[8]);
                        switch (jega->gdcreg[3] & 0x18)
                        {
                                case 0: /*Set*/
                                if (writemask2 & 1) jega->vram[addr]       = (vala & jega->gdcreg[8]) | (jega->la & ~jega->gdcreg[8]);
                                if (writemask2 & 2) jega->vram[addr | 0x1] = (valb & jega->gdcreg[8]) | (jega->lb & ~jega->gdcreg[8]);
                                if (writemask2 & 4) jega->vram[addr | 0x2] = (valc & jega->gdcreg[8]) | (jega->lc & ~jega->gdcreg[8]);
                                if (writemask2 & 8) jega->vram[addr | 0x3] = (vald & jega->gdcreg[8]) | (jega->ld & ~jega->gdcreg[8]);
                                break;
                                case 8: /*AND*/
                                if (writemask2 & 1) jega->vram[addr]       = (vala | ~jega->gdcreg[8]) & jega->la;
                                if (writemask2 & 2) jega->vram[addr | 0x1] = (valb | ~jega->gdcreg[8]) & jega->lb;
                                if (writemask2 & 4) jega->vram[addr | 0x2] = (valc | ~jega->gdcreg[8]) & jega->lc;
                                if (writemask2 & 8) jega->vram[addr | 0x3] = (vald | ~jega->gdcreg[8]) & jega->ld;
                                break;
                                case 0x10: /*OR*/
                                if (writemask2 & 1) jega->vram[addr]       = (vala & jega->gdcreg[8]) | jega->la;
                                if (writemask2 & 2) jega->vram[addr | 0x1] = (valb & jega->gdcreg[8]) | jega->lb;
                                if (writemask2 & 4) jega->vram[addr | 0x2] = (valc & jega->gdcreg[8]) | jega->lc;
                                if (writemask2 & 8) jega->vram[addr | 0x3] = (vald & jega->gdcreg[8]) | jega->ld;
                                break;
                                case 0x18: /*XOR*/
                                if (writemask2 & 1) jega->vram[addr]       = (vala & jega->gdcreg[8]) ^ jega->la;
                                if (writemask2 & 2) jega->vram[addr | 0x1] = (valb & jega->gdcreg[8]) ^ jega->lb;
                                if (writemask2 & 4) jega->vram[addr | 0x2] = (valc & jega->gdcreg[8]) ^ jega->lc;
                                if (writemask2 & 8) jega->vram[addr | 0x3] = (vald & jega->gdcreg[8]) ^ jega->ld;
                                break;
                        }
//                                pclog("- %02X %02X %02X %02X   %08X\n",vram[addr],vram[addr|0x1],vram[addr|0x2],vram[addr|0x3],addr);
                }
                break;
                case 2:
                if (!(jega->gdcreg[3] & 0x18) && !jega->gdcreg[1])
                {
                        if (writemask2 & 1) jega->vram[addr]       = (((val & 1) ? 0xff : 0) & jega->gdcreg[8]) | (jega->la & ~jega->gdcreg[8]);
                        if (writemask2 & 2) jega->vram[addr | 0x1] = (((val & 2) ? 0xff : 0) & jega->gdcreg[8]) | (jega->lb & ~jega->gdcreg[8]);
                        if (writemask2 & 4) jega->vram[addr | 0x2] = (((val & 4) ? 0xff : 0) & jega->gdcreg[8]) | (jega->lc & ~jega->gdcreg[8]);
                        if (writemask2 & 8) jega->vram[addr | 0x3] = (((val & 8) ? 0xff : 0) & jega->gdcreg[8]) | (jega->ld & ~jega->gdcreg[8]);
                }
                else
                {
                        vala = ((val & 1) ? 0xff : 0);
                        valb = ((val & 2) ? 0xff : 0);
                        valc = ((val & 4) ? 0xff : 0);
                        vald = ((val & 8) ? 0xff : 0);
                        switch (jega->gdcreg[3] & 0x18)
                        {
                                case 0: /*Set*/
                                if (writemask2 & 1) jega->vram[addr]       = (vala & jega->gdcreg[8]) | (jega->la & ~jega->gdcreg[8]);
                                if (writemask2 & 2) jega->vram[addr | 0x1] = (valb & jega->gdcreg[8]) | (jega->lb & ~jega->gdcreg[8]);
                                if (writemask2 & 4) jega->vram[addr | 0x2] = (valc & jega->gdcreg[8]) | (jega->lc & ~jega->gdcreg[8]);
                                if (writemask2 & 8) jega->vram[addr | 0x3] = (vald & jega->gdcreg[8]) | (jega->ld & ~jega->gdcreg[8]);
                                break;
                                case 8: /*AND*/
                                if (writemask2 & 1) jega->vram[addr]       = (vala | ~jega->gdcreg[8]) & jega->la;
                                if (writemask2 & 2) jega->vram[addr | 0x1] = (valb | ~jega->gdcreg[8]) & jega->lb;
                                if (writemask2 & 4) jega->vram[addr | 0x2] = (valc | ~jega->gdcreg[8]) & jega->lc;
                                if (writemask2 & 8) jega->vram[addr | 0x3] = (vald | ~jega->gdcreg[8]) & jega->ld;
                                break;
                                case 0x10: /*OR*/
                                if (writemask2 & 1) jega->vram[addr]       = (vala & jega->gdcreg[8]) | jega->la;
                                if (writemask2 & 2) jega->vram[addr | 0x1] = (valb & jega->gdcreg[8]) | jega->lb;
                                if (writemask2 & 4) jega->vram[addr | 0x2] = (valc & jega->gdcreg[8]) | jega->lc;
                                if (writemask2 & 8) jega->vram[addr | 0x3] = (vald & jega->gdcreg[8]) | jega->ld;
                                break;
                                case 0x18: /*XOR*/
                                if (writemask2 & 1) jega->vram[addr]       = (vala & jega->gdcreg[8]) ^ jega->la;
                                if (writemask2 & 2) jega->vram[addr | 0x1] = (valb & jega->gdcreg[8]) ^ jega->lb;
                                if (writemask2 & 4) jega->vram[addr | 0x2] = (valc & jega->gdcreg[8]) ^ jega->lc;
                                if (writemask2 & 8) jega->vram[addr | 0x3] = (vald & jega->gdcreg[8]) ^ jega->ld;
                                break;
                        }
                }
                break;
        }
}

uint8_t jega_buffer_read(uint32_t addr, void *p)
{
	return (addr & 0xff);
}

uint8_t jega_read(uint32_t addr, void *p)
{
        jega_t *jega = (jega_t *)p;
        uint8_t temp, temp2, temp3, temp4;
	int readplane = jega->readplane;
	uint32_t oa = addr;
	uint32_t bp = (jega->cr[0x1B] & 0xE0) << 16;

	// Ignore reads from B000-B7FF in AT-compatible superimpose mode
	if (((jega->cr[4] & 6) == 4) && ((addr & 0xFFFF8000) == 0xB0000))  return jega->extram[addr & 0x7fff];
	if ((addr & 0xFFFF0000) == 0xE0000)  return jega->vram[bp + (addr & 0xffff)];

        egareads++;
        cycles -= video_timing_b;
        cycles_lost += video_timing_b;
//        pclog("Readjega %06X   ",addr);
       	if (addr >= 0xE0000) addr &= 0xffff;
       	else if (addr >= 0xB0000) addr &= 0x7fff;
       	else                 addr &= 0xffff;

        if (jega->chain2)
        {
                readplane = (readplane & 2) | (addr & 1);
                addr &= ~1;
        }

	addr <<= 2;

	if (jega->cr[5] & 8)
	{
		addr |= 0x40000;
	}
	/* if (jega->cr[4] & 4)
	{
		if (oa <= 0xB0000)  addr |= 0x40000;
	} */
	// if (oa >= 0xD0000)  addr |= 0x40000;
        jega->la = jega->vram[addr];
        jega->lb = jega->vram[addr | 0x1];
        jega->lc = jega->vram[addr | 0x2];
        jega->ld = jega->vram[addr | 0x3];
        if (jega->readmode)
        {
                temp   = (jega->colournocare & 1)  ? 0xff : 0;
                temp  &= jega->la;
                temp  ^= (jega->colourcompare & 1) ? 0xff : 0;
                temp2  = (jega->colournocare & 2)  ? 0xff : 0;
                temp2 &= jega->lb;
                temp2 ^= (jega->colourcompare & 2) ? 0xff : 0;
                temp3  = (jega->colournocare & 4)  ? 0xff : 0;
                temp3 &= jega->lc;
                temp3 ^= (jega->colourcompare & 4) ? 0xff : 0;
                temp4  = (jega->colournocare & 8)  ? 0xff : 0;
                temp4 &= jega->ld;
                temp4 ^= (jega->colourcompare & 8) ? 0xff : 0;
                return ~(temp | temp2 | temp3 | temp4);
        }
        return jega->vram[addr | readplane];
}

void jega_init(jega_t *jega)
{
        int c, d, e, f;
        
	/* Basically, memory above 256 kB is subdivided as follows:
		000000-00FFFF	Plane 0
		010000-01FFFF	Plane 1
		020000-02FFFF	Plane 2
		030000-03FFFF	Plane 3
		040000-04FFFF	Superimpose Plane 0
		050000-05FFFF	Superimpose Plane 1
		060000-06FFFF	Superimpose Plane 2
		070000-07FFFF	Superimpose Plane 3
		080000-1FFFFF	512 kB DBCS Font buffer */
        jega->vram = malloc(0x200000);
        jega->vrammask = 0x1fffff;
        
        for (c = 0; c < 256; c++)
        {
                e = c;
                for (d = 0; d < 8; d++)
                {
                        jega_rotate[d][c] = e;
                        e = (e >> 1) | ((e & 1) ? 0x80 : 0);
                }
        }

        for (c = 0; c < 4; c++)
        {
                for (d = 0; d < 4; d++)
                {
                        edatlookup[c][d] = 0;
                        if (c & 1) edatlookup[c][d] |= 1;
                        if (d & 1) edatlookup[c][d] |= 2;
                        if (c & 2) edatlookup[c][d] |= 0x10;
                        if (d & 2) edatlookup[c][d] |= 0x20;
                }
        }

        for (c = 0; c < 256; c++)
        {
                pallook64[c]  = makecol32(((c >> 2) & 1) * 0xaa, ((c >> 1) & 1) * 0xaa, (c & 1) * 0xaa);
                pallook64[c] += makecol32(((c >> 5) & 1) * 0x55, ((c >> 4) & 1) * 0x55, ((c >> 3) & 1) * 0x55);
                pallook16[c]  = makecol32(((c >> 2) & 1) * 0xaa, ((c >> 1) & 1) * 0xaa, (c & 1) * 0xaa);
                pallook16[c] += makecol32(((c >> 4) & 1) * 0x55, ((c >> 4) & 1) * 0x55, ((c >> 4) & 1) * 0x55);
		f = c;
		if ((f & 0x30) == 0x10)  f |= 0x30;
		if ((f & 0x30) == 0x20)  f &= 0xCF;
		f &= 0xF7;
                pallook32[c]  = makecol32(((f >> 2) & 1) * 0xaa, ((f >> 1) & 1) * 0xaa, (f & 1) * 0xaa);
                pallook32[c] += makecol32(((f >> 4) & 1) * 0x55, ((f >> 4) & 1) * 0x55, ((f >> 4) & 1) * 0x55);
                pallook8[c]  = makecol32(((c >> 2) & 1) * 0xff, ((c >> 1) & 1) * 0xff, (c & 1) * 0xff);
                if ((c & 0x17) == 6) 
		{
                        pallook16[c] = makecol32(0xaa, 0x55, 0);
                        pallook32[c] = makecol32(0xaa, 0x55, 0);
                        pallook8[c] = makecol32(0xff, 0xff, 0);
		}
        }
	pallook8[20] = makecol32(0xff, 0xff, 0);
	/* for (c = 0; c < 8; c++)
	{
		for (d = 0; d < 8; d++)
		{
                	pallook8[(d * 8) + c]  = makecol32(((c >> 2) & 1) * 0xff, ((c >> 1) & 1) * 0xff, (c & 1) * 0xff);
                	pallook8[(d * 8) + c] |= makecol32(((c >> 4) & 1) * 0xff, ((c >> 4) & 1) * 0xff, ((c >> 4) & 1) * 0xff);
			if (c == 6)
			{
                		pallook8[(d * 8) + c]  = makecol32(0xff, 0xff, 0);
                		pallook8[(d * 8) + c] |= makecol32(0xff, 0xff, 0);
			}
		}
	} */
        // jega->pallook = pallook16;
        jega->pallook = pallook32;
}

void planar_fill(jega_t *jega, uint32_t addr, uint8_t pix1, uint8_t pix2, uint8_t pix3, uint8_t pix4, uint8_t pix5, uint8_t pix6, uint8_t pix7, uint8_t pix8)
{
	uint8_t p[4] = {0, 0, 0, 0};
	uint8_t i;
	// Combine pixels into 4 bytes

	for (i = 0; i < 4; i++)
	{
		if (pix1 & (1 << i))  p[i] |= 0x80;
		if (pix2 & (1 << i))  p[i] |= 0x40;
		if (pix3 & (1 << i))  p[i] |= 0x20;
		if (pix4 & (1 << i))  p[i] |= 0x10;
		if (pix5 & (1 << i))  p[i] |= 8;
		if (pix6 & (1 << i))  p[i] |= 4;
		if (pix7 & (1 << i))  p[i] |= 2;
		if (pix8 & (1 << i))  p[i] |= 1;

		jega->vram[addr + i] = p[i];
	}
}

void *jega_standalone_init()
{
        int c, d, e;
        jega_t *jega = malloc(sizeof(jega_t));
	FILE *f;
	uint8_t pixels[8] = {0, 0, 0, 0, 0, 0, 0, 0};
	uint8_t pixels2[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        memset(jega, 0, sizeof(jega_t));
        
        rom_init(&jega->bios_rom, "roms/stb.bin", 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);

        if (jega->bios_rom.rom[0x7ffe] == 0xaa && jega->bios_rom.rom[0x7fff] == 0x55)
        {
                int c;
                pclog("Read JEGA ROM in reverse\n");

                for (c = 0; c < 0x4000; c++)
                {
                        uint8_t temp = jega->bios_rom.rom[c];
                        jega->bios_rom.rom[c] = jega->bios_rom.rom[0x7fff - c];
                        jega->bios_rom.rom[0x7fff - c] = temp;
                }
        }

	for (c = 0; c < 0x8000; c++)
	{
		jega->extram[c] = 0xFF;
	}

        jega->crtc[0] = 63;
        // jega->crtc[6] = 255;
        jega->dispontime = 1000 * (1 << TIMER_SHIFT);
	jega->dispofftime = 1000 * (1 << TIMER_SHIFT);

        jega_init(jega);
	jega_defaults(jega);
	jega_dbcs_fonts_load(jega);

	// d = 0;
	f = fopen("roms/clouds.raw", "rb");
// c = 0
// 2000 - c = 2000
// 2000 - 8 = 1992
// 1992 - 8 = 1984 
	for (c = 0; c < (640 * 480); c+= 8)
	{
		fseek(f, c, SEEK_SET);
		for (d = 0; d < 8; d++)
		{
			pixels[d] = fgetc(f);
		}
		/* for (d = 0; d < 8; d++)
		{
			pixels[d] = pixels2[7 - d];
		} */
		planar_fill(jega, ((c >> 3) << 2) | 0x40000, pixels[0], pixels[1], pixels[2], pixels[3], pixels[4], pixels[5], pixels[6], pixels[7]);
	}
	fclose(f);

        mem_mapping_add(&jega->mapping, 0xa0000, 0x1ffc0, jega_read, NULL, NULL, jega_write, NULL, NULL, NULL, 0, jega);
        mem_mapping_add(&jega->necmapping, 0xe0000, 0x10000, jega_read, NULL, NULL, jega_write, NULL, NULL, NULL, 0, jega);
        mem_mapping_add(&jega->bufmapping, 0xbffc0, 0x40, jega_buffer_read, NULL, NULL, jega_buffer_write, NULL, NULL, NULL, 0, jega);
	mem_mapping_disable(&jega->necmapping);
	// mem_mapping_disable(&jega->bufmapping);
	mem_mapping_set_addr(&jega->bufmapping, 0xbffc0, 0x40);
        // mem_mapping_add(&jega->mapping, 0xa0000, 0x40000, jega_read, NULL, NULL, jega_write, NULL, NULL, NULL, 0, jega);
        // mem_mapping_add(&jega->mapping, 0xe0000, 0x40000, jega_read, NULL, NULL, jega_write, NULL, NULL, NULL, 0, jega);
        timer_add(jega_poll, &jega->vidtime, TIMER_ALWAYS_ENABLED, jega);
        vramp = jega->vram;
        io_sethandler(0x03a0, 0x0040, jega_in, NULL, NULL, jega_out, NULL, NULL, jega);
	nec_compat = 0;
        return jega;
}

static int jega_standalone_available()
{
        return rom_present("roms/stb.bin");
}

void jega_close(void *p)
{
        jega_t *jega = (jega_t *)p;

        free(jega->vram);
        free(jega);
}

void jega_speed_changed(void *p)
{
        jega_t *jega = (jega_t *)p;
        
        jega_recalctimings(jega);
}

device_t jega_device =
{
        "JEGA",
        0,
        jega_standalone_init,
        jega_close,
        jega_standalone_available,
        jega_speed_changed,
        NULL,
        NULL
};
