#include <stdio.h>
#include <string.h>
#include <malloc.h>

#include "ibm.h"

#include "dma.h"
#include "io.h"
#include "pic.h"
#include "timer.h"

#include "fdc.h"
#include "floppy.h"
#include "win-display.h"

int output;
int lastbyte=0;
int sio=0;

uint8_t OLD_BPS = 0;
uint8_t OLD_SPC = 0;
uint8_t OLD_C = 0;
uint8_t OLD_H = 0;
uint8_t OLD_R = 0;
uint8_t OLD_N = 0;

uint8_t flag = 0;
int curoffset = 0;

int tempdiv = 0;
int m3 = 0;

int densel_polarity = {-1};
int densel_polarity_mid[2] = {-1, -1};
int densel_force = 0;
int rwc_force[2] = {0, 0};
int en3mode = 0;
int diswr = 0;		/* Disable write. */
int swwp = 0;		/* Software write protect. */
int drv2en = 0;
int boot_drive = 0;

int drt[2] = {0, 0};

int fdc_os2 = 0;
int drive_swap = 0;

uint16_t fdcport = 0;

int ps2 = 0;

#define	IS_BIG	fdd[vfdd[fdc.drive]].BIGFLOPPY
#define	IS_3M	fdd[vfdd[fdc.drive]].THREEMODE
#define	VF_CLS	fdd[vfdd[fdc.drive]].CLASS
#define	VF_DEN	fdd[vfdd[fdc.drive]].DENSITY
#define M3_E	mode3_enabled()
#define M3_D	!(mode3_enabled())
#define RPS_360	(VF_DEN && (IS_BIG || (!IS_BIG && IS_3M)))
#define RPS_300	((!VF_DEN) || (IS_BIG && VF_DEN && IS_3M) || (!IS_BIG))
#define EMPTYF	{ x += fdc_fail(0xFE); return 0; }
#define	CMD_RW	discint==2 || discint==5 || discint==6 || discint==9 || discint==12 || discint==0x16 || discint==0x11 || discint==0x19 || discint==0x1D
#define IS_FR	(fdd[vfdd[fdc.drive]].IMGTYPE == IMGT_FDI) || (fdd[vfdd[fdc.drive]].IMGTYPE == IMGT_RAW)

int fifo_buf_read();

double byterate()
{
	switch(fdc.rate)
	{
		default:
		case 0:
			return 500.0;
		case 1:
			return 300.0 * 1.02;
		case 2:
			return 250.0 * 1.02;
		case 3:
			return 1000.0 * 1.02;
	}
}

void loaddisc(int d, char *fn)
{
	floppy_load_image(d, fn);
}

uint8_t construct_wp_byte(int d)
{
	return fdd[d].WP | (fdd[d].XDF << 1) | (fdd[d].CLASS << 2) | (fdd[d].LITE << 5);
}

void savedisc(int d)
{
        FILE *f;
        int h,t,s,b;
	int dw;
        if (!fdd[d].discmodified) return;
	if ((d == 1) && ((romset == ROM_COLORBOOK) || !drv2en))  return;
	if (diswr)  return;
	if (swwp)  return;
        if (fdd[d].WP) return;
	if (fdd[d].XDF)  return;
	if (fdd[d].IMGTYPE == IMGT_NONE)  return;
	if (fdd[d].IMGTYPE != IMGT_RAW)  return;
        f=fopen(discfns[d],"wb");
        if (!f) return;
	if(fdd[d].IMGTYPE == IMGT_PEF)
	{
		putc('P',f);
		putc('C',f);
		putc('e',f);
		putc('m',f);
		putc((uint8_t) fdd[d].SIDES,f);
		putc((uint8_t) fdd[d].TRACKS,f);
		putc((uint8_t) fdd[d].SECTORS,f);
		putc((uint8_t) (fdd[d].BPS >> 7),f);
		putc((uint8_t) construct_wp_byte(d),f);
		putc(0,f);
		putc(0,f);
		putc(0,f);
		putc(0,f);
		putc(0,f);
		putc(0,f);
		putc(0,f);
		if (!fdd[d].LITE)
		{
			curoffset = 16;
        		for (t=0;t<fdd[d].TRACKS;t++)
        		{
                		for (h=0;h<fdd[d].SIDES;h++)
                		{
                        		for (s=0;s<fdd[d].SECTORS;s++)
                        		{
                                        	putc(fdd[d].sstat[h][t][s],f);
						curoffset++;
                        		}
                		}
        		}
			for(t = curoffset; t <= 32767; t++)
			{
				putc(0,f);
			}
		}
	}
	else if (fdd[d].IMGTYPE == IMGT_FDI)
	{
		dw = fdd[d].FDIDATA;
		fwrite(&dw, 1, 8, f);
		dw = fdd[d].RAWOFFS;
		fwrite(&dw, 1, 4, f);
		dw = fdd[d].TOTAL / fdd[d].BPS;
		fwrite(&dw, 1, 4, f);
		dw = fdd[d].BPS;
		fwrite(&dw, 1, 4, f);
		dw = fdd[d].SECTORS;
		fwrite(&dw, 1, 4, f);
		dw = fdd[d].SIDES;
		fwrite(&dw, 1, 4, f);
		dw = fdd[d].TRACKS;
		fwrite(&dw, 1, 4, f);
		for(t = 0x20; t < fdd[d].RAWOFFS; t++)
		{
			putc(0, f);
		}
	}
        for (t=0;t<fdd[d].TRACKS;t++)
        {
                for (h=0;h<fdd[d].SIDES;h++)
                {
                        for (s=0;s<fdd[d].SECTORS;s++)
                        {
				// printf("Saving? Is special: %s!\n", ISSPECIAL ? "Yes" : "No");
				if ((ISSPECIAL && !(t & 1)) || !(ISSPECIAL))
				{
					// printf("Saving: C%02X, H%02X, R%02X...\n", h, t, s);
					if (fdd[d].scid[h][t][s][3] >= 1)
					// printf("Sector code in sector ID is valid...\n");
					{
	        	                        for (b=0;b<(128 << fdd[d].scid[h][t][s][3]);b++)
	                	                {
        	                	                putc(fdd[d].disc[h][t][s][b],f);
	                                	}
					}
				}
                        }
                }
        }
        fclose(f);
}

/*
	DenSel	BigFloppy	IsHD	Description
	1	1		1	1.6 MB (360 rpm)
	1	1		0	1.0 MB (360 rpm)
					300 rpm drive: Invalid
					360 rpm drive: 1.0 MB (360 rpm)
					Bi-rpm drive: 1.0 MB (360 rpm)
	0	1		1	2.0 MB (300 rpm) (Invalid)
	0	1		0	1.0 MB (300 rpm)
					300 rpm drive: 1.0 MB (300 rpm)
					360 rpm drive: Invalid
					Bi-rpm drive: 1.0 MB (300 rpm)
	1	0		1	2.0 MB (300 rpm)
	1	0		0	Invalid
					Some drives (such as Acer AcerMate 970's drive)
					support 3.5" DD media at 360 rpm
					This might be used for that
	0	0		1	1.6 MB (360 rpm) (Only for 3-mode)
	0	0		0	1.0 MB (300 rpm)


	In short:
	DenSel	Drive			Description
	Any	5.25" DD, QD		300 rpm
	Any	5.25" HD single-RPM	360 rpm
	1	5.25" HD dual-RPM	360 rpm
	0	5.25" HD dual-RPM	300 rpm
	Any	3.5" DD			300 rpm
	Any	3.5" HD 2-mode		300 rpm
	0	3.5" HD 3-mode with	360 rpm
		rate 0 or 3
	1	3.5" HD 3-mode with	300 rpm
		rate 0 or 3
	Any	3.5" HD 3-mode with	300 rpm
		rate 1 or 2
	

	PS/2 Inverts DENSEL
*/

int get_media_density()
{
	/* So that DENSEL will always be 360 rpm on 5.25" 1.2M drives with only 360 rpm support. */
	/* For 5.25" drives, DENSEL 1 means 360 rpm, for 3.5" any DENSEL is 300 rpm except for DENSEL 0 with 500k or 1M rate. */
	switch (fdc.rate)
	{
		case 0:
		case 3:
			return 1;
		case 1:
		case 2:
			return 0;
	}

	/* switch (VF_DEN)
	{
		case 0:
			return 1;
		default:
			if (IS_BIG && !IS_3M)  return 0;
			if (VF_CLS < CLASS_1600)  return 1;
			return 0;
	} */
}

/* int densel_by_class()
{
	return (get_media_density()) ? (ps2 ? 1 : 0) : (ps2 ? 0 : 1);
} */

int densel_pin()
{
	uint8_t dsel;

	dsel = get_media_density();

	if (ps2)  dsel = (dsel ? 0 : 1);

	/* If polarity is set to 1, invert it. */
	if (densel_polarity == 0)  dsel = (dsel ? 0 : 1);

	if (!en3mode)  goto normal_densel;

	switch(rwc_force[vfdd[fdc.drive]])
	{
		case 0:
normal_densel:
			pclog("densel_force is: %i\n", densel_force);
			switch(densel_force)
			{
				case 0:
					/* Return DENSEL as per normal. */
					return dsel;
				case 1:
					/* Reserved, make it behave like normal. */
					return dsel;
				case 2:
					/* Force 1. */
					return 1;
				case 3:
					/* Force 0. */
					return 0;
			}
		case 1:
		case 3:
			/* Force 0. */
			return 0;
		case 2:
			/* Force 1. */
			return 1;
	}
}

/* This specifies, if 360 rpm mode is enabled. */
int mode3_enabled()
{
	if (fdd[vfdd[fdc.drive]].BIGFLOPPY)
	{
		/* Always 300 rpm for low-density drives. */
		if (fdd[vfdd[fdc.drive]].DENSITY == DEN_DD)  return 0;
		/* Always 360 rpm for single-RPM drives. */
		if (!fdd[vfdd[fdc.drive]].THREEMODE)  return 1;
		/* 360 RPM if DenSel is 1. */
		return densel_pin() ? (!ps2 ? 1 : 0) : (!ps2 ? 0 : 1);
	}
	else
	{
		/* Always 300 rpm for low-density drives. */
		if (fdd[vfdd[fdc.drive]].DENSITY == DEN_DD)  return 0;
		/* Always 300 rpm for non-3-mode drives. */
		if (!fdd[vfdd[fdc.drive]].THREEMODE)  return 0;
		/* For 3-mode drives if DRT is 1 and rate is 1, that means 500k @ 360 rpm unless DENSEL is inverted or forced to 1. */
		if (drt[vfdd[fdc.drive]] == 1)  if (fdc.rate == 1)  return densel_pin() ? (!ps2 ? 0 : 1) : (!ps2 ? 1 : 0);
		/* Always 300 rpm for 250k rate. */
		if (fdc.rate == 2)  return 0;
		/* 360 RPM if DenSel is 0. */
		return densel_pin() ? (!ps2 ? 0 : 1) : (!ps2 ? 1 : 0);
	}
}

int current_rpm(int d)
{
	return (mode3_enabled) ? 360 : 300;
}

void reset_fifo_bus()
{
	int i = 0;
	for (i = 0; i < 16; i++)
	{
		fdc.fifobuf[i] = 0;
	}
	fdc.fifobufpos = 0;
}

void config_default()
{
	int i = 0;

	fdc.dsr = 2;
	fdc.st1 = 0;
	fdc.st2 = 0;
	fdc.dor |= 0xF8;
	fdc.dor &= 0xFC;
	fdc.format_started[0] = 0;
	fdc.format_started[1] = 0;
	fdc.dma = 1;
	fdc.tdr = 0;
	fdc.deldata = 0;
	fdc.fifo = 0;
	fdc.tfifo = 1;
	fdc.gotdata[0] = 0;
	fdc.gotdata[1] = 0;
}

void fdc_reset_tracks()
{
	/* fdc.track[0] = 0;
	fdc.track[1] = 0; */
}

int discint;
void fdc_reset()
{
        fdc.stat=0x80;
        fdc.pnum=fdc.ptot=0;
        fdc.st0=0xC0;
        fdc.lock = 0;
        fdc.pcn[0] = 0;
        fdc.pcn[1] = 0;
	fdc_reset_tracks();
        fdc.head[0] = 0;
        fdc.head[1] = 0;
        fdc.abort[0] = 0;
	fdc.abort[1] = 0;
	fdd[0].rws = 0;
	fdd[1].rws = 0;
        if (!AT)
           fdc.rate=RATE_250K;
}
int ins;

static void fdc_int()
{
        if (!fdc.pcjr && fdc.dma)
                picint(1 << 6);
}

static void fdc_watchdog_poll(void *p)
{
        FDC *fdc = (FDC *)p;
        
        fdc->watchdog_count--;
        if (fdc->watchdog_count)
                fdc->watchdog_timer += 1000 * TIMER_USEC;
        else
        {
//                pclog("Watchdog timed out\n");
        
                fdc->watchdog_timer = 0;
                if (fdc->dor & 0x20)
                        picint(1 << 6);
        }
}

int fdc_fail(int dint)
{
	discint=dint;
	if (dint==0xFE)  return 2;
	disctime = 1024 * (1 << TIMER_SHIFT) * 3;
	return 1;
}

int is_hded()
{
	return (fdd[vfdd[fdc.drive]].DENSITY != DEN_DD);
}

int fdc_checkrate()
{
	int x = 0;

	if (!fdd[vfdd[fdc.drive]].floppy_drive_enabled)  EMPTYF

	if ((fdc.drive == 1) && ((romset == ROM_COLORBOOK) || !drv2en))  EMPTYF

	if (fdd[vfdd[fdc.drive]].CLASS == -1)  EMPTYF

	if (fdd[vfdd[fdc.drive]].SIDES > 2)  EMPTYF

	if (fdd[vfdd[fdc.drive]].TRACKS < 30)  EMPTYF

	if ((fdd[vfdd[fdc.drive]].TRACKS < 60) && (fdd[vfdd[fdc.drive]].TRACKS > 43))  EMPTYF

	if (fdd[vfdd[fdc.drive]].TRACKS > 86)  EMPTYF

	if (fdd[vfdd[fdc.drive]].TOTAL * fdd[vfdd[fdc.drive]].BPS > (4000 * 1000))  EMPTYF

	if (!en3mode)  goto normal_rate;

	/* If DRVTYPE is 1, fail for all rates other than 0 (or 1 if DRVRATE is 1). */
	if ((rwc_force[vfdd[fdc.drive]] == 1) && (fdc.rate != RATE_500K))
	{
		if (fdc.rate == RATE_300K)
		{
			if (drt[vfdd[fdc.drive]] != 1)  x += fdc_fail(0xFF);
		}
		else
		{
			x += fdc_fail(0xFF);
		}
	}

	/* If DRVTYPE is 2, fail for all rates other than 0. */
	if ((rwc_force[vfdd[fdc.drive]] == 2) && (fdc.rate != RATE_500K))  x += fdc_fail(0xFF);

	/* If DRVTYPE is 3, fail for all rates other than 2. */
	if ((rwc_force[vfdd[fdc.drive]] == 3) && (fdc.rate != RATE_250K))  x += fdc_fail(0xFF);

normal_rate:
	switch(fdc.rate)
	{
		case RATE_500K:
rate_500k:
			if (VF_DEN == DEN_DD)  x += fdc_fail(0xFF);
			switch(VF_CLS)
			{
				case CLASS_1600:
					if (M3_D)  x += fdc_fail(0xFF);
					if (!RPS_360) x += fdc_fail(0xFF);
					break;
				case CLASS_2000:
					if (M3_E)  x += fdc_fail(0xFF);
					if (!RPS_300) x += fdc_fail(0xFF);
					break;
				default:
					x += fdc_fail(0xFF);
			}
			break;
		case RATE_300K:
			if (drt[vfdd[fdc.drive]] == 1)  goto rate_500k;
			if (drt[vfdd[fdc.drive]] == 2)  goto rate_2meg;
			if (VF_DEN == DEN_DD)  x += fdc_fail(0xFF);
			switch(VF_CLS)
			{
				case CLASS_500:
					if (M3_D)  x += fdc_fail(0xFF);
					if (!RPS_360)  x += fdc_fail(0xFF);
					if (!IS_BIG)  x += fdc_fail(0xFE);
					break;
				case CLASS_1000:
					if (M3_D)  x += fdc_fail(0xFF);
					if (!RPS_360)  x += fdc_fail(0xFF);
					break;
				case CLASS_600:
					if (M3_E)  x += fdc_fail(0xFF);
					if (!RPS_300)  x += fdc_fail(0xFF);
					if (!IS_BIG)  x += fdc_fail(0xFE);
					break;
				case CLASS_1200:
					if (M3_E)  x += fdc_fail(0xFF);
					if (!RPS_300)  x += fdc_fail(0xFF);
					break;
				default:
					x += fdc_fail(0xFF);
			}
			break;
		case RATE_250K:
rate_250k:
			switch(fdd[vfdd[fdc.drive]].CLASS)
			{
				case CLASS_400:
					if (M3_D)  x += fdc_fail(0xFF);
					if (!RPS_360)  x += fdc_fail(0xFF);
					if (!IS_BIG)  x += fdc_fail(0xFE);
					break;
				case CLASS_800:
					if (M3_D)  x += fdc_fail(0xFF);
					if (!RPS_360)  x += fdc_fail(0xFF);
					break;
				case CLASS_500:
					if (M3_E)  x += fdc_fail(0xFF);
					if (!RPS_300)  x += fdc_fail(0xFF);
					if (!IS_BIG)  x += fdc_fail(0xFE);
					break;
				case CLASS_1000:
					if (M3_E)  x += fdc_fail(0xFF);
					if (!RPS_300)  x += fdc_fail(0xFF);
					break;
				default:
					x += fdc_fail(0xFF);
			}
			break;
		case RATE_1M:
			if (VF_DEN != DEN_ED)  x += fdc_fail(0xFF);
			switch(VF_CLS)
			{
				case CLASS_3200:
					if (M3_D)  x += fdc_fail(0xFF);
					if (!RPS_360) x += fdc_fail(0xFF);
					break;
				case CLASS_4000:
					if (M3_E)  x += fdc_fail(0xFF);
					if (!RPS_300) x += fdc_fail(0xFF);
					break;
				default:
					x += fdc_fail(0xFF);
			}
			break;
		case (RATE_1M + 1):
rate_2meg:
			if (VF_DEN != DEN_ED)  x += fdc_fail(0xFF);
			switch(VF_CLS)
			{
				case CLASS_6400:
					if (M3_D)  x += fdc_fail(0xFF);
					if (!RPS_360) x += fdc_fail(0xFF);
					break;
				case CLASS_8000:
					if (M3_E)  x += fdc_fail(0xFF);
					if (!RPS_300) x += fdc_fail(0xFF);
					break;
				default:
					x += fdc_fail(0xFF);
			}
			break;
		default:
			x += fdc_fail(0xFF);
			break;
	}

rc_common:
	if (discint == 0xFE)  return 0;

	/* 128-byte sectors are only used for FM floppies, which we do not support. */
	if (fdd[vfdd[fdc.drive]].BPS < 256)  EMPTYF

	// Don't allow too small floppies on 3.5" drives
	if (!fdd[vfdd[fdc.drive]].BIGFLOPPY && (fdd[vfdd[fdc.drive]].TRACKS < 60))  EMPTYF

	if (fdd[vfdd[fdc.drive]].driveempty)  x += fdc_fail(0xFE);

	if (x)  return 0;
	return 1;
}

int fdc_checkparams()
{
	int x = 0;
	// Basically, if with current track number we don't reach above the maximum size for our floppy class
	x = samediskclass(fdc.drive, fdd[vfdd[fdc.drive]].TRACKS, fdc.params[2], fdc.params[1]);
	if (!x)  fdc_fail(0x101);
	return x;
}

// XDF-aware bytes per sector returner
int real_bps_code()
{
	// Modified so it returns the code from the sector ID rather than the old hardcoded stuff
	return fdd[vfdd[fdc.drive]].scid[fdc.head[fdc.drive]][fdc.track[fdc.drive]][fdc.sector[fdc.drive]-1][3];
}

int sector_state(int read)
{
	uint8_t state = current_state();

	if (ss_good(state))  return 0;

	if (!(ss_idam_present(state)) && (discint != 13))  // Do *NOT* verify the ID address mark on formatting, as that's when it's being written in the first place!
	{
		fdc.st1 |= 1;
		return 1;
	}
	else
	{
		if (ss_id_crc_present(state) || (discint == 13))
		{
			if (!(ss_id_crc_correct(state)) && (discint != 13))
			{
				fdc.st1 |= 0x20;
				fdc.st2 &= 0xdf;
				return 1;
			}
		}
		else
		{
			fdc.st1 |= 0x20;
			fdc.st2 &= 0xdf;
			return 1;
		}
	}

	if (read)
	{
		if (!(ss_dam_present(state)))
		{
			fdc.st1 |= 1;
			return 1;
		}
		else
		{
			if (ss_data_crc_present(state))
			{
				if (!(ss_data_crc_correct(state)))
				{
					fdc.st1 |= 0x20;
					fdc.st2 |= 0x20;				
					return 1;
				}
			}
			else
			{
				fdc.st1 |= 0x20;
				fdc.st2 |= 0x20;
				return 1;
			}
		}
	}

	if (read == 3)
	{
		if (!fdc.deldata && ss_dam_nondel(state))  fdc.st2 |= 0x40;
		if (fdc.deldata && !(ss_dam_nondel(state)))  fdc.st2 |= 0x40;
	}

	if ((fdc.st1 == 0) && (fdc.st2 == 0))  return 0;

	return 1;
}

static int fdc_reset_stat = 0;

/* uint8_t get_step()
{
	if (IS_BIG && VF_DEN && (VF_CLS < CLASS_800) && (VF_CLS != -1))  return 2;
	if (IS_BIG && !VF_DEN && IS_3M && (VF_CLS < CLASS_800) && (VF_CLS != -1))  return 2;

	return 1;
} */

void fdc_implied_seek(uint8_t track)
{
	uint8_t dir = (fdc.command & 0x40) ? 1 : 0;
	uint8_t i = 0;
	uint8_t n = 0;
	int move = -1;

	if (track > fdc.pcn[fdc.drive])
	{
		dir = 1;
		n = track - fdc.pcn[fdc.drive];
	}
	else if (track < fdc.pcn[fdc.drive])
	{
		dir = 0;
		n = fdc.pcn[fdc.drive] - track;
	}

	if (track != fdc.pcn[fdc.drive])
	{
		move = -1;
		if (dir)  move = 1;

		fdd_seek(vfdd[fdc.drive], n, dir);
		for (i = 0; i < n; i++)
		{
			fdc.pcn[fdc.drive] += move;
		}
		fdc.st0 = 0x20 | fdc.drive;
	}

	timer_process();
	disctime = 2048 * (1 << TIMER_SHIFT) * 3;
	timer_update_outstanding();

#ifndef RELEASE_BUILD
	pclog("Seeked (implied) to track %i, real track %i, TRK0 is %i\n", fdc.pcn[fdc.drive], fdd[vfdd[fdc.drive]].track, fdd[vfdd[fdc.drive]].trk0);
#endif
}

void fdc_seek()
{
	uint8_t rel = (fdc.command & 0x80) ? 1 : 0;
	uint8_t dir = (fdc.command & 0x40) ? 1 : 0;
	uint8_t i = 0;
	uint8_t max = 0;
	uint8_t eos = 0;
	uint8_t p1 = 0;
	uint8_t n = 0;
	int move = -1;

	if (dir)  move = 1;

	if (romset == ROM_ENDEAVOR)
	{
		max = 85;
	}
	else
	{
		max = 79;
	}

	fdc_reset_stat = 0;

	fdc.head[fdc.drive] = (fdc.params[0] & 4) >> 2;

	if (fdc.command == 7)
	{
		fdd_seek(vfdd[fdc.drive], max, 0);
		fdc.pcn[fdc.drive] = 0;
		fdc.st0 = 0x20 | fdc.drive;
		if (!fdd[vfdd[fdc.drive]].trk0)  fdc.st0 |= 0x10;

		if (!fdd[vfdd[fdc.drive]].driveempty)  fdd[vfdd[fdc.drive]].discchanged = 0;
		discint=-3;
                timer_process();
                disctime = 2048 * (1 << TIMER_SHIFT) * 3;
                timer_update_outstanding();
       	        fdc.stat = 0x80 | (1 << fdc.drive);

		// pclog("Recalibrated to track %i, real track %i, TRK0 is %i\n", fdc.pcn[fdc.drive], fdd[vfdd[fdc.drive]].track, fdd[vfdd[fdc.drive]].trk0);
	}
	else
	{
		if (rel)
		{
			fdd_seek(vfdd[fdc.drive], fdc.params[1], dir);
			fdc.st0 = 0x20 | fdc.drive;
			for (i = 0; i < fdc.params[1]; i++)
			{
				fdc.pcn[fdc.drive] += move;
				if (fdc.pcn[fdc.drive] == 0)
				{
					if (!dir)
					{
						if (!fdd[vfdd[fdc.drive]].trk0)
						{
							fdc.st0 |= 0x10;
						}
						else
						{
							fdc.st0 &= 0xEF;
						}
					}
				}
			}
		}
		else
		{
			if (fdc.params[1] > fdc.pcn[fdc.drive])
			{
				dir = 1;
				n = fdc.params[1] - fdc.pcn[fdc.drive];
			}
			else if (fdc.params[1] < fdc.pcn[fdc.drive])
			{
				dir = 0;
				n = fdc.pcn[fdc.drive] - fdc.params[1];
			}

			if (fdc.params[1] != fdc.pcn[fdc.drive])
			{
				move = -1;
				if (dir)  move = 1;

				fdd_seek(vfdd[fdc.drive], n, dir);
				for (i = 0; i < n; i++)
				{
					fdc.pcn[fdc.drive] += move;
				}
			}
			/* Set ST0 correctly even if NCN = PCN. */
			fdc.st0 = 0x20 | fdc.drive;
		}

		// pclog("Seeked to track %i, real track %i, TRK0 is %i\n", fdc.pcn[fdc.drive], fdd[vfdd[fdc.drive]].track, fdd[vfdd[fdc.drive]].trk0);

		if (!fdd[vfdd[fdc.drive]].driveempty)  fdd[vfdd[fdc.drive]].discchanged = 0;

		discint=-3;
                timer_process();
                disctime = 2048 * (1 << TIMER_SHIFT) * 3;
                timer_update_outstanding();
       	        fdc.stat = 0x80 | (1 << fdc.drive);
	}
}

#ifdef CRUDE_SEEK
void fdc_seek(uint8_t during_rw)
{
	uint8_t rel = (fdc.command & 0x80);
	uint8_t dir = (fdc.command & 0x40);
	uint8_t step = 1;
	uint8_t i = 0;
	uint8_t max = 0;
	uint8_t eos = 0;
	uint8_t p1 = 0;
	uint8_t vt = 80;

	step = get_step();

	// pclog("fdc_seek with IS_BIG=%02X, VF_DEN=%02X, and VF_CLS=%02X\n", IS_BIG, VF_DEN, VF_CLS);
#ifndef RELEASE_BUILD
	if (step == 2)  pclog("Step is 2\n");
#endif

	max = 85;
	// if (((VF_CLS < CLASS_800) && (VF_CLS != -1)) && (step == 1))  max = 42;
	if (IS_BIG && !VF_DEN && !IS_3M)
	{
		max = 42;
		vt = 40;
	}

	fdc_reset_stat = 0;

	fdc.head[fdc.drive] = (fdc.params[0] & 4) >> 2;

	if (during_rw)  goto non_relative_seek;

	if (fdc.command == 7)
	{
		/* Recalibrate */
		// pclog("RECALIBRATE START: PCN %02X, TRK %02X\n", fdc.pcn[fdc.drive], fdc.track[fdc.drive]);
		if (romset != ROM_ENDEAVOR)
		{
			max = 79;
			if (IS_BIG && !VF_DEN && !IS_3M)  max = 39;
		}

		/* Seek outwards by as many steps as current tracks is. */
		for (i = 0; i < max; i ++)
		{
			if (fdc.pcn[fdc.drive] == 0)  break;
			if (step == 1)  if (fdc.track[fdc.drive] == 0)  break;
			fdc.pcn[fdc.drive]--;
			if (step == 2)
			{
				fdc.track[fdc.drive] = fdc.pcn[fdc.drive] >> 1;
			}
			else
			{
				fdc.track[fdc.drive]--;
			}
			// pclog("SEEK OUT: PCN %02X, TRK %02X\n", fdc.pcn[fdc.drive], fdc.track[fdc.drive]);
		}
		if (fdc.track[fdc.drive] != 0)
			fdc.st0 |= 0x10;

		// fdc.st0=(fdc.head[fdc.drive]?4:0)|fdc.drive;
		fdc.st0 = fdc.drive;
		fdc.st0 |= 0x20;
		// pclog("RECALIBRATE END: ST0 %02X\n", fdc.st0);

		if (!fdd[vfdd[fdc.drive]].driveempty)  fdd[vfdd[fdc.drive]].discchanged = 0;
		discint=-3;
                timer_process();
                disctime = 2048 * (1 << TIMER_SHIFT) * 3;
                timer_update_outstanding();
       	        fdc.stat = 0x80 | (1 << fdc.drive);
	}
	else
	{
		p1 = fdc.params[1];
		// if (step == 2)  p1 &= 0xFE;
		/* Seek */
		if (rel)
		{
			if (dir)
			{
				/* Step inwards. */
				eos = fdc.pcn[fdc.drive] + p1;
				for (i = 0; i < p1; i++)
				{
					if (fdc.pcn[fdc.drive] == eos)
					{
						fdc.st0 = 0x20;
						break;
					}
					fdc.pcn[fdc.drive]++;
					if (step == 2)
					{
						fdc.track[fdc.drive] = fdc.pcn[fdc.drive] >> 1;
					}
					else
					{
						fdc.track[fdc.drive]++;
					}
				}
			}
			else
			{
				/* Step outwards. */
				eos = fdc.pcn[fdc.drive] - p1;
				for (i = 0; i < p1; i+= step)
				{
					if (step == 2)
					{
						if ((fdc.pcn[fdc.drive] == eos) || (fdc.pcn[fdc.drive] == 0))
						{
							fdc.st0 = 0x20;
							break;
						}
					}
					else
					{
						if ((fdc.pcn[fdc.drive] == eos) || (fdc.track[fdc.drive] == 0))
						{
							fdc.st0 = 0x20;
							break;
						}
					}
					fdc.pcn[fdc.drive]--;
					if (step == 2)
					{
						fdc.track[fdc.drive] = fdc.pcn[fdc.drive] >> 1;
					}
					else
					{
						fdc.track[fdc.drive]--;
					}
				}
			}
		}
		else
		{
non_relative_seek:
			// pclog("TRACKS: %02X\n", fdd[vfdd[fdc.drive]].TRACKS);
			// pclog("SEEK START: PCN %02X, TRK %02X\n", fdc.pcn[fdc.drive], fdc.track[fdc.drive]);
			if (fdc.pcn[fdc.drive] < p1)
			{
				/* Step inwards. */
				while(1)
				{
					if (fdc.pcn[fdc.drive] == p1)
					{
						fdc.st0 = 0x20;
						break;
					}
					fdc.pcn[fdc.drive]++;
					if (step == 2)
					{
						fdc.track[fdc.drive] = fdc.pcn[fdc.drive] >> 1;
					}
					else
					{
						fdc.track[fdc.drive]++;
					}
					// pclog("SEEK IN: PCN %02X, TRK %02X\n", fdc.pcn[fdc.drive], fdc.track[fdc.drive]);
				}
			}
			else if (fdc.pcn[fdc.drive] == p1)
			{
				fdc.st0 = 0x20;
			}
			else if (fdc.pcn[fdc.drive] > p1)
			{
				/* Step outwards. */
				while(1)
				{
					if (step == 2)
					{
						if ((fdc.pcn[fdc.drive] == p1) || (fdc.pcn[fdc.drive] == 0))
						{
							fdc.st0 = 0x20;
							break;
						}
					}
					else
					{
						if ((fdc.pcn[fdc.drive] == p1) || (fdc.track[fdc.drive] == 0))
						{
							fdc.st0 = 0x20;
							break;
						}
					}
					fdc.pcn[fdc.drive]--;
					if (step == 2)
					{
						fdc.track[fdc.drive] = fdc.pcn[fdc.drive] >> 1;
					}
					else
					{
						fdc.track[fdc.drive]--;
					}
					// pclog("SEEK OUT: PCN %02X, TRK %02X\n", fdc.pcn[fdc.drive], fdc.track[fdc.drive]);
				}
			}
			if (!fdd[vfdd[fdc.drive]].driveempty)
			{
				if (fdc.track[fdc.drive] > (fdd[vfdd[fdc.drive]].TRACKS - 1))
				{
					fdc.track[fdc.drive] = fdd[vfdd[fdc.drive]].TRACKS - 1;
				}
			}
			else
			{
				if (fdc.track[fdc.drive] > (vt - 1))
				{
					fdc.track[fdc.drive] = vt - 1;
				}
			}
		}
end_of_seek:
		// pclog("SEEK END: ST0 %02X\n", fdc.st0);
		if (!fdd[vfdd[fdc.drive]].driveempty)  fdd[vfdd[fdc.drive]].discchanged = 0;
		// fdc.st0|=(fdc.head[fdc.drive]?4:0)|fdc.drive;
		fdc.st0 |= fdc.drive;
		if (!during_rw)  discint=-3;
                timer_process();
                disctime = 2048 * (1 << TIMER_SHIFT) * 3;
                timer_update_outstanding();
       	        fdc.stat = 0x80 | (1 << fdc.drive);
	}
}
#endif

int fdc_format()
{
	int b = 0;

	if (diswr)  goto format_skip;

	// pclog("ROK!\n");
	fdc.pos[fdc.drive] = 0;

	/* Make sure the "sector" points to the correct position in the track buffer. */
	fdd[vfdd[fdc.drive]].disc[fdc.head[fdc.drive]][fdd[vfdd[fdc.drive]].track][fdc.sector[fdc.drive] - 1] = fdd[vfdd[fdc.drive]].trackbufs[fdc.head[fdc.drive]][fdc.track[fdc.drive]] + ((uint32_t) fdd[vfdd[fdc.drive]].ltpos);
	for (b = 0; b < 4; b++)
	{
		fdd[vfdd[fdc.drive]].scid[fdc.head[fdc.drive]][fdd[vfdd[fdc.drive]].track][fdc.sector[fdc.drive] - 1][b] = fdc.params[b + 5];
	}
	fdd[vfdd[fdc.drive]].scid[fdc.head[fdc.drive]][fdd[vfdd[fdc.drive]].track][fdc.sector[fdc.drive] - 1][4] = 0xbf;
	if (fdc.sector[fdc.drive] == 1)  fdd[vfdd[fdc.drive]].scid[fdc.head[fdc.drive]][fdd[vfdd[fdc.drive]].track][fdc.sector[fdc.drive] - 1][4] |= 0x40;

	for(fdc.pos[fdc.drive] = 0; fdc.pos[fdc.drive] < (128 << ((uint16_t) fdd[vfdd[fdc.drive]].scid[fdc.head[fdc.drive]][fdd[vfdd[fdc.drive]].track][fdc.sector[fdc.drive] - 1][3])); fdc.pos[fdc.drive]++)
	{
		fdd[vfdd[fdc.drive]].disc[fdc.head[fdc.drive]][fdd[vfdd[fdc.drive]].track][fdc.sector[fdc.drive] - 1][fdc.pos[fdc.drive]] = fdc.fillbyte[fdc.drive];
	}

	fdd[vfdd[fdc.drive]].spt[fdd[vfdd[fdc.drive]].track]++;
	fdd[vfdd[fdc.drive]].ltpos += (128 << fdc.params[8]);

format_skip:
	fdd[vfdd[fdc.drive]].sectors_formatted++;
	// pclog("FDCFMT: %02X %02X %02X %02X | %02X %02X %02X %02X\n", fdc.params[5], fdc.params[6], fdc.params[7], fdc.params[8], fdd[vfdd[fdc.drive]].scid[fdc.head[fdc.drive]][fdd[vfdd[fdc.drive]].track][fdc.sector[fdc.drive] - 1][4], fdd[vfdd[fdc.drive]].sstates, fdc.st1, fdc.st2);
	if (fdc.sector[fdc.drive] < fdd[vfdd[fdc.drive]].temp_spt)  fdc.sector[fdc.drive]++;
	return 0;
}

int paramstogo=0;

void fdc_format_command()
{
	int i = 0;
	int quantity = 1;
	int mfm = (fdc.command & 0x40);
	int maxs = 0;
	int sc = 0;
	int temp = 0;
	int rbps = 0;
	uint8_t internal_in = 0;
	uint8_t rt = 0;
	uint8_t sr = 0;

	if (fdc.fifo)  quantity = fdc.tfifo;

	if (fdd[vfdd[fdc.drive]].WP || swwp)
	{
		discint = 0x100;
		fdc_poll();
		return;
	}
	fdd[vfdd[fdc.drive]].discmodified = 1;

	if (!fdc.format_started[fdc.drive])
	{
		if (!diswr)
		{
			fdd[vfdd[fdc.drive]].temp_bps = fdc.params[1];
			fdd[vfdd[fdc.drive]].temp_spt = fdc.params[2];
			fdd[vfdd[fdc.drive]].spt[fdd[vfdd[fdc.drive]].track] = 0;
			fdc.eot[fdc.drive] = fdc.params[2];
			fdc.sector[fdc.drive] = 1;
			fdc.pos[fdc.drive] = 0;
			fdc.head[fdc.drive] = (fdc.params[0] & 4) >> 2;
			fdd[vfdd[fdc.drive]].sectors_formatted = 0;
			fdc.fillbyte[fdc.drive] = fdc.params[4];
			fdc.fdmaread[fdc.drive] = 0;
			fdd[vfdd[fdc.drive]].sstates = 0;
			fdd[vfdd[fdc.drive]].ltpos = 0;
			freetracksectors(fdc.drive, fdc.head[fdc.drive], fdd[vfdd[fdc.drive]].track);
		}
		fdc.format_started[fdc.drive] = 1;
	}

	if (fdd[vfdd[fdc.drive]].sectors_formatted<fdd[vfdd[fdc.drive]].temp_spt)
	{
		for(i = 0; i < quantity; i++)
		{
			if (fdc.pcjr || !fdc.dma)
			{
				fdc.stat = 0xb0;
				fdc.dat = fifo_buf_read();
			}
			else
			{
				fdc.stat = 0x90;
				temp = dma_channel_read(2);
				fdc.dat = temp;
				if (temp & DMA_OVER)
					fdc.abort[fdc.drive] = 1;
				if (temp == DMA_NODATA)
				{
					fdc.abort[fdc.drive] = 1;
				}
			}

			if (temp != DMA_NODATA)
			{
				fdc.params[5 + fdc.fdmaread[fdc.drive]] = fdc.dat;
			}

                       	timer_process();
			disctime = 600 * (1 << TIMER_SHIFT) * 3;
                       	timer_update_outstanding();

			fdc.fdmaread[fdc.drive]++;
			if(fdc.fdmaread[fdc.drive] >= 4)
			{
				fdc.fdmaread[fdc.drive] = 0;
				fdc.sector[fdc.drive] = fdd[vfdd[fdc.drive]].sectors_formatted + 1;
				fdc_format();
				if (fdd[vfdd[fdc.drive]].sectors_formatted == fdd[vfdd[fdc.drive]].temp_spt)  return;
			}
		}
		return;
	}
	else
	{
		/* Format is finished! */
#ifndef RELEASE_BUILD
		pclog("Format finished, track %u with %u sectors!\n", fdd[vfdd[fdc.drive]].track, fdd[vfdd[fdc.drive]].spt[fdd[vfdd[fdc.drive]].track]);
#endif
		disctime=0;
		discint=-2;
		fdc_int();
		fdc.stat=0xd0;
		fdc.res[4]=(fdc.head[fdc.drive]?4:0)|fdc.drive;
		if (fdd[vfdd[fdc.drive]].sstates)  fdc.res[4] |= 0x40;
		fdc.res[4] |= 0x20;
		fdc.st0=fdc.res[4];
		fdc.res[5]=fdc.st1;
		fdc.res[6]=fdc.st2;
		fdc.st1=0;
		fdc.st2=0;
		fdc.res[7]=0;
		fdc.res[8]=0;
		fdc.res[9]=0;
		fdc.res[10]=0;
		paramstogo=7;
		fdc.format_started[fdc.drive] = 0;
		return;
	}
}

int act = 0;

void fifo_buf_write(int val)
{
	if (fdc.fifobufpos < fdc.tfifo)
	{
		fdc.fifobufpos++;
		fdc.fifobufpos %= fdc.tfifo;
		fdc.fifobuf[fdc.fifobufpos] = val;
		pclog("FIFO buffer position = %02X\n", fdc.fifobufpos);
		if (fdc.fifobufpos == fdc.tfifo)  fdc.fifobufpos = 0;
	}
}

int fifo_buf_read()
{
	int temp = 0;
	if (fdc.fifobufpos < fdc.tfifo)
	{
		fdc.fifobufpos++;
		fdc.fifobufpos %= fdc.tfifo;
		temp = fdc.fifobuf[fdc.fifobufpos];
		pclog("FIFO buffer position = %02X\n", fdc.fifobufpos);
		if (fdc.fifobufpos == fdc.tfifo)  fdc.fifobufpos = 0;
	}
	return temp;
}

void fdc_write(uint16_t addr, uint8_t val, void *priv)
{
	// printf("Write FDC %04X %02X %04X:%04X %i %02X %i rate=%i\n",addr,val,cs>>4,pc,ins,fdc.st0,ins,fdc.rate);
	printf("OUT 0x%04X, %02X\n", addr, val);
	// if ((addr&7) == 3)  printf("OUT 0x%04X, %02X\n", addr, val);
        switch (addr&7)
        {
		case 0: /*Configuration*/
			return;
                case 1: /*Change configuration*/
			return;
                case 2: /*DOR*/
	                // if (val == 0xD && (cs >> 4) == 0xFC81600 && ins > 769619936) output = 3;
			// printf("DOR was %02X\n",fdc.dor);
	                if (fdc.pcjr)
	                {
				pclog("PCjr DOR write\n");
				if (!(val&0x80) && !(fdc.dor&0x80))  return;		
	                        if ((fdc.dor & 0x40) && !(val & 0x40))
	                        {
	                                fdc.watchdog_timer = 1000 * TIMER_USEC;
					fdc.watchdog_count = 1000;
					picintc(1 << 6);
#ifndef RELEASE_BUILD
					pclog("watchdog set %i %i\n", fdc.watchdog_timer, TIMER_USEC);
#endif
        	                }
                	        if ((val & 0x80) && !(fdc.dor & 0x80))
                        	{
	        			timer_process();
        	                        disctime = 128 * (1 << TIMER_SHIFT) * 3;
                	                timer_update_outstanding();
                        	        discint=-1;
                                	fdc_reset();
	                        }
        	        }
	                else
        	        {
				if (!(val&4) && !(fdc.dor&4))  return;	/* If in RESET state, return */
	                       	if ((val&4) && !(fdc.dor&4))
	                       	{
	       				timer_process();
	                               	disctime = 128 * (1 << TIMER_SHIFT) * 3;
        	                       	timer_update_outstanding();
	                               	discint=-1;
	                               	fdc.stat=0x80;
	                               	fdc.pnum=fdc.ptot=0;
	                               	fdc_reset();
	                       	}
				/* else
				{
					if (val & (0x10 << (val & 0x3)))  fdd[vfdd[fdc.dor]].discchanged = 1;
				} */
	                }
        	        fdc.dor=val;
			fdc.dor |= 0x30;
			if (!fdc.pcjr)
			{
				fdc.dor &= ((romset == ROM_COLORBOOK) || !drv2en) ? 0x1F : 0x3F;
			}
			// printf("DOR now %02X\n",val);
	                return;
		case 3:
			if (!(fdc.dor&4))  return;
			if (!(fdc.dor&0x80) && fdc.pcjr)  return;
			if (AT && en3mode)
			{
				fdc.tdr=val & 0x33;
				if ((fdc.dor & 3) < 2)  rwc_force[fdc.dor & 3] = ((fdc.tdr >> 4) & 3);
			}
			else
			{
				fdc.tdr=val & 3;
			}
			return;
                case 4:
			if (!(fdc.dor&4))  return;
			if (!(fdc.dor&0x80) && fdc.pcjr)  return;
                	if (val & 0x80)
	                {
				if (!fdc.pcjr)  fdc.dor &= 0xFB;
				if (fdc.pcjr)  fdc.dor &= 0x7F;
				timer_process();
	                        disctime = 128 * (1 << TIMER_SHIFT) * 3;
	                        timer_update_outstanding();
        	                discint=-1;
                	        fdc_reset();
				if (!fdc.pcjr) fdc.dor |= 4;
				if (fdc.pcjr) fdc.dor |= 0x80;
	                }
			if (val & 0x40)
			{
				timer_process();
				disctime = 128 * (1 << TIMER_SHIFT) * 3;
				timer_update_outstanding();
				discint=-1;
				fdc_reset();
			}
                return;
                case 5: /*Command register*/
			if (!(fdc.dor&4) && !fdc.pcjr)  return;		
			if (!(fdc.dor&0x80) && fdc.pcjr)  return;
	                if ((fdc.stat & 0xf0) == 0xb0)
        	        {
				if (!fdc.fifo)
				{
	                        	fdc.dat = val;
					fifo_buf_write(val);
	                        	fdc.stat &= ~0x80;
				}
				else
				{
					fifo_buf_write(val);
					if (fdc.fifobufpos == 0)  fdc.stat &= ~0x80;
				}
	                        break;
	                }
	                // pclog("Write command reg %i %i\n",fdc.pnum, fdc.ptot);
	                if (fdc.pnum==fdc.ptot)
	                {
        	                fdc.command=val;
	                        // printf("Starting FDC command %02X\n",fdc.command);
				fdd[0].sstates = 0;
				fdd[1].sstates = 0;
				fdc.deldata = 0;
				fdc.res[4] = 0;
				fdc.res[5] = 0;
				fdc.res[6] = 0;
/* This serves to make this command work properly. */
	                        switch (fdc.command&0x1F)
	                        {
	                                case 2: /*Read track*/
        		                        fdc.pnum=0;
		                                fdc.ptot=8;
		                                fdc.stat=0x90;
		                                break;
	                                case 3: /*Specify*/
		                                fdc.pnum=0;
		                                fdc.ptot=2;
		                                fdc.stat=0x90;
		                                break;
	                                case 4: /*Sense drive status*/
		                                fdc.pnum=0;
		                                fdc.ptot=1;
		                                fdc.stat=0x90;
		                                break;
					case 9: /*Write deleted data*/
						fdc.deldata = 1;
	                                case 5: /*Write data*/
						if ((fdc.command&0x1F) == 5)  fdc.deldata = 0;
		                                fdc.pnum=0;
		                                fdc.ptot=8;
		                                fdc.stat=0x90;
	        	                        readflash=1;
	                	                break;
					case 12: /*Read deleted data*/
						fdc.deldata = 1;
					case 0x16: /*Verify data*/
	                                case 6: /*Read data*/
					case 0x11: /* Scan Equal */
					case 0x19: /* Scan Low or Equal */
					case 0x1D: /* Scan High or Equal */
						if ((fdc.command&0x1F) != 12)  fdc.deldata = 0;
		                                fullspeed();
		                                fdc.pnum=0;
		                                fdc.ptot=8;
		                                fdc.stat=0x90;
		                                readflash=1;
		                                break;
	                                case 7: /*Recalibrate*/
						// if (fdc_reset_stat)  goto bad_fdc_command;
		                                fdc.pnum=0;
		                                fdc.ptot=1;
		                                fdc.stat=0x90;
		                                break;
	                                case 8: /*Sense interrupt status*/
		                                fdc.lastdrive = fdc.drive;
		                                discint = 8;
		                                fdc_poll();
		                                break;
	                                case 10: /*Read sector ID*/
		                                fdc.pnum=0;
		                                fdc.ptot=1;
		                                fdc.stat=0x90;
		                                break;
	                                case 13: /*Format*/
		                                fdc.pnum=0;
		                                fdc.ptot=5;
		                                fdc.stat=0x90;
		                                readflash=1;
		                                break;
	                                case 15: /*Seek*/
						// if (fdc_reset_stat)  goto bad_fdc_command;
						fdc.relative=fdc.command & 0x80;
						fdc.direction=fdc.command & 0x40;
        		                        fdc.pnum=0;
		                                fdc.ptot=2;
		                                fdc.stat=0x90;
	        	                        break;
	                                case 0x0e: /*Dump registers*/
		                                fdc.lastdrive = fdc.drive;
		                                discint = 0x0e;
		                                fdc_poll();
		                                break;
	                                case 0x10: /*Get version*/
		                                fdc.lastdrive = fdc.drive;
		                                discint = 0x10;
		                                fdc_poll();
		                                break;
	                                case 0x12: /*Set perpendicular mode*/
		                                fdc.pnum=0;
		                                fdc.ptot=1;
		                                fdc.stat=0x90;
		                                break;
	                                case 0x13: /*Configure*/
		                                fdc.pnum=0;
		                                fdc.ptot=3;
		                                fdc.stat=0x90;
		                                break;
	                                case 0x14: /*Unlock*/
	                                // case 0x94: /*Lock*/
		                                fdc.lastdrive = fdc.drive;
		                                discint = fdc.command;
		                                fdc_poll();
		                                break;
	                                case 0x18:
		                                fdc.stat = 0x10;
        		                        discint  = 0xfc;
	                	                fdc_poll();
        	                	        break;
	                                default:
bad_fdc_command:
#ifndef RELEASE_BUILD
		                                pclog("Bad FDC command %02X\n",val);
#endif
	        	                        fdc.stat=0x10;
		                                discint=0xfc;
		                                timer_process();
	        	                        disctime = 200 * (1 << TIMER_SHIFT) * 3;
	                	                timer_update_outstanding();
		                                break;
	                        }
	                }
	                else
	                {
	                        fdc.params[fdc.pnum++]=val;
	                        if (fdc.pnum==fdc.ptot)
        	                {
					if ((fdc.command) == 3)
					{
						discint = 3;
						fdc_poll();
						return;
					}
	                                fdc.stat=0x30;
        	                        discint=fdc.command&0x1F;
	                                timer_process();
					disctime = 1024 * (1 << TIMER_SHIFT) * 3;
					if ((discint!=9) && (discint!=12))  fdc.deldata = 0;
					if ((discint!=8) && (discint!=0x12) && (discint!=0x14) && (discint!=0x94) && (discint!=0xE) && (discint!=0x13) && (discint!=3) && (discint!= 0x10) && ((discint<=0x16) || (discint == 0x19) || (discint == 0x1D)))
					{
						// This is so we make sure fdc.drive isn't changed on commands that don't change it
	                                	fdc.drive=fdc.params[0]&1;
						fdc.abort[fdc.drive] = 0;
						fdc.pos[fdc.drive] = 0;
					}
	                                if (CMD_RW)
	                                {
	                                        fdc.track[fdc.drive]=fdc.params[1];
	                                        fdc.head[fdc.drive]=fdc.params[2];
        	                                fdc.sector[fdc.drive]=fdc.params[3];
                	                        fdc.eot[fdc.drive] = fdc.params[5];
                        	                if (!fdc.params[5])
                                	        {
                                        	        fdc.params[5]=fdc.sector[fdc.drive];
	                                        }
	                                        if (fdc.params[5]>fdd[vfdd[fdc.drive]].spt[fdc.track[fdc.drive]]) fdc.params[5]=fdd[vfdd[fdc.drive]].spt[fdc.track[fdc.drive]];
					}
					if (CMD_RW || discint==10 || discint==13)
					{
	                                        if (fdd[vfdd[fdc.drive]].driveempty)
        	                                {
#ifndef RELEASE_BUILD
                	                                pclog("Drive empty\n");
#endif
                        	                        discint=0xFE;
							goto end_of_dwrite;
	                                        }

						if ((discint == 13) && (!fdc.format_started[fdc.drive]))
						{
							/* Prevent formatting an XDF-formatted image and bail out early if write protection is on. */
							if ((fdd[vfdd[fdc.drive]].WP || swwp) || (fdd[vfdd[fdc.drive]].XDF))
							{
								discint=0x100;
								goto end_of_dwrite;
							}
							else
							{
								if (fdd[vfdd[fdc.drive]].track <= (is_48tpi(vfdd[fdc.drive]) ? 42 : 85))
								{
									if (fdc.params[2] != fdd[vfdd[fdc.drive]].SECTORS)
									{
										/* All tracks must have the same sector count on FDI or RAW image. */
										if (IS_FR)
										{
											discint=0x100;
											goto end_of_dwrite;
										}
									}
								}
								if (fdd[vfdd[fdc.drive]].track < 0)
								{
									fdc_fail(0x101);
									goto end_of_dwrite;
								}
								if (fdd[vfdd[fdc.drive]].track >= fdd[vfdd[fdc.drive]].TRACKS)
								{
									if (fdd[vfdd[fdc.drive]].track <= (is_48tpi(vfdd[fdc.drive]) ? 42 : 85))
										fdd[vfdd[fdc.drive]].TRACKS++;
									else
									{
										fdc_fail(0x101);
										goto end_of_dwrite;
									}
								}
								fdd[vfdd[fdc.drive]].spt[fdd[vfdd[fdc.drive]].track] = fdc.params[2];
							}
						}
	                                }
					/* Only make sure the data rate and RPM match on formatting in case of FDI or RAW image. */
	                                if (CMD_RW || discint==10 || (discint==13 && (IS_FR)))
	                                {
						// Check rate after the format stuff
#ifndef RELEASE_BUILD
	                                        pclog("Rate (di = %08X) %i %i %i at %i RPM (dp %i, drt %i, df %i, ds %i)\n", discint, fdc.rate, VF_CLS, fdd[vfdd[fdc.drive]].driveempty, (M3_E ? 360 : 300), densel_polarity, drt[vfdd[fdc.drive]], densel_force, densel_pin());
#endif
						if (discint < 0xFC)  fdc_checkrate();
#ifndef RELEASE_BUILD
						// if (discint < 0xFC)  pclog("Rate OK\n");
						if (discint == 0xFE)  pclog("Not ready\n");
						if (discint == 0xFF)  pclog("Wrong rate\n");
						if (discint == 0x100)  pclog("Write protected\n");
						if (discint == 0x101)  pclog("Track not found\n");
#endif
	                                }
	                                if (discint == 0xf || discint == 10)  fdc.head[fdc.drive] = (fdc.params[0] & 4) ? 1 : 0;
                                	if (discint == 5 && (fdc.pcjr || !fdc.dma))  fdc.stat = 0xb0;
	                                if (discint == 9 && (fdc.pcjr || !fdc.dma))  fdc.stat = 0xb0;
	                                if (discint == 13 && (fdc.pcjr || !fdc.dma))  fdc.stat = 0xb0;
	                                timer_update_outstanding();
	                        }
	                }
end_of_dwrite:
	                return;
                case 7:
                        if (!AT) return;
	                fdc.rate=val&3;
	                disc_3f7=val;
	                return;
        }
	// printf("Write FDC %04X %02X\n",addr,val);
}

uint8_t fdc_read(uint16_t addr, void *priv)
{
        uint8_t temp;
	uint8_t fdcd;
//        /*if (addr!=0x3f4) */printf("Read FDC %04X %04X:%04X %04X %i %02X %i ",addr,cs>>4,pc,BX,fdc.pos[fdc.drive],fdc.st0,ins);
        switch (addr&7)
        {
		case 0: /*Configuration, index, and status register A*/
			temp = 0xFF;
			break;
		case 1:	/*Data, and status register B*/
			temp = 0x70;
			if (fdc.dor & 1)
				temp &= ~0x40;
			else
				temp &= ~0x20;
			break;
		case 2:
			temp = fdc.dor;
			if (!AT)  temp = 0xFF;
			if (AT)
			{
				if (!fdd[vfdd[0]].floppy_drive_enabled)  temp &= 0xEF;
				if (!fdd[vfdd[1]].floppy_drive_enabled)  temp &= 0xDF;
				temp &= 0x3F;
			}
			break;
                case 3:
			temp = 0x30;
			if (AT)
			{
				temp = 0;
				if (en3mode)
				{
					temp |= (boot_drive << 2);
					if ((fdc.dor & 3) > 2)  temp |= (rwc_force[fdc.dor & 3] << 4);
				}
			}
			break;
                case 4: /*Status*/
			temp = fdc.stat;
			break;
			temp=fdc.stat;
	                break;
                case 5: /*Data*/
	                fdc.stat&=~0x80;
	                if ((fdc.stat & 0xf0) == 0xf0)
        	        {
				temp = fifo_buf_read();
				if (fdc.fifobufpos != 0)  fdc.stat |= 0x80;
	                        break;
        	        }
	                if (paramstogo)
        	        {
                	        paramstogo--;
	                        temp=fdc.res[10 - paramstogo];
	                        if (!paramstogo)
        	                {
                	                fdc.stat=0x80;
	                        }
        	                else
                	        {
                        	        fdc.stat|=0xC0;
	                        }
	                }
	                else
        	        {
                	        if (lastbyte)  fdc.stat=0x80;
	                        lastbyte=0;
	                        temp=fdc.dat;
        	        }
	                if (discint==0xA) 
			{
				timer_process();
				disctime = 1024 * (1 << TIMER_SHIFT) * 3;
				timer_update_outstanding();
			}
        	        fdc.stat &= 0xf0;
	                break;
                case 7: /*Disk change*/
	                if (fdc.dor & (0x10 << (fdc.dor & 1)))
				temp = (fdd[vfdd[fdc.dor & 1]].discchanged || fdd[vfdd[fdc.dor & 1]].driveempty)?0x80:0;
	                else
				temp = 0;
	                if (fdc.dskchg_activelow)  /*PC2086/3086 seem to reverse this bit*/
				temp ^= 0x80;
	                break;
                default:
                        temp=0xFF;
			// printf("Bad read FDC %04X\n",addr);
        }
	// /*if (addr!=0x3f4) */printf("%02X rate=%i\n",temp,fdc.rate);
	printf("IN 0x%04X, %02X\n", addr, temp);
	// if (addr == 0x3F3)  printf("IN 0x%04X, %02X\n", addr, temp);
        return temp;
}

int return_sstate()
{
	return fdd[vfdd[fdc.drive]].sstat[fdc.head[fdc.drive]][fdd[vfdd[fdc.drive]].track][fdc.sector[fdc.drive] - 1];
}

int sds_match()
{
	int d = 1;
	int e = 0;
	if (fdc.deldata)  d = 0;
	e = (ss_dam_nondel(current_state()) ? 1 : 0);
	return (d == e);
}

int real_bps()
{
	return (128 << real_bps_code());
}

uint8_t fdc_seek_by_id(uint32_t chrn, uint8_t *rc, uint8_t *rh, uint8_t *rr)
{
	int ic, ih, ir;
	*rr = 0;
	// ic = *(uint8_t *) &chrn;
	ic = fdd[vfdd[fdc.drive]].track;

	for (ih = 0; ih < fdd[vfdd[fdc.drive]].SIDES; ih++)
	{
		for (ir = 0; ir < fdd[vfdd[fdc.drive]].spt[fdd[vfdd[fdc.drive]].track]; ir++)
		{
			if ((*(uint32_t *) fdd[vfdd[fdc.drive]].scid[ih][ic][ir]) == chrn)
			{
				*rc = ic;
				*rh = ih;
				*rr = ir + 1;
				return 1;
			}
		}
	}
	return 0;
}

int sector_has_id()
{
	return (fdd[vfdd[fdc.drive]].scid[fdc.head[fdc.drive]][fdd[vfdd[fdc.drive]].track][fdc.sector[fdc.drive]-1][1] != 255) ? 1 : 0;
}

uint32_t scan_results = 0;
uint32_t satisfying_sectors = 0;

void fdc_readwrite(int mode)
{
	int i = 0;
	int quantity = 1;
	int mt = (fdc.command & 0x80);
	int mfm = (fdc.command & 0x40);
	int sk = (fdc.command & 0x20);
	int ec = (fdc.params[0] & 0x80);
	uint8_t step = (fdc.params[7]);
	int maxs = 0;
	int sc = 0;
	int temp = 0;
	int rbps = 0;
	uint8_t internal_in = 0;
	uint8_t rt = 0;
	uint8_t sr = 0;
	uint8_t xd = 0;

	if ((fdc.command & 0x1F) == 2)
	{
		rt = 1;
		mt = 0;
		sk = 0;
	}

	if (mode != 2)  ec = 0;

	if (mode == 0)  sk = 0;

	if (ec)  maxs = fdc.params[7];
	if (ec && !maxs)  maxs = 256;

	if (fdc.fifo)  quantity = fdc.tfifo;

	// Find real sector position in the array by ID
	if (!fdd[vfdd[fdc.drive]].rws)
	{
		if (fdc.eis)
		{
			if (FDDSPECIAL)
			{
				fdc_implied_seek(fdc.track[fdc.drive] << 1);
			}
			else
			{
				fdc_implied_seek(fdc.track[fdc.drive]);
			}
		}
		else
		{
			if (FDDSPECIAL)
			{
				if((fdc.track[fdc.drive] << 1) != fdc.pcn[fdc.drive])
				{
#ifndef RELEASE_BUILD
					pclog("Wrong cylinder: %i != %i\n", (fdc.track[fdc.drive] << 1), fdc.pcn[fdc.drive]);
#endif
					discint = 0x102;
					fdc_poll();
					return;
				}
			}
			else
			{
				if(fdc.track[fdc.drive] != fdc.pcn[fdc.drive])
				{
#ifndef RELEASE_BUILD
					pclog("Wrong cylinder: %i != %i\n", fdc.track[fdc.drive], fdc.pcn[fdc.drive]);
#endif
					discint = 0x102;
					fdc_poll();
					return;
				}
			}
		}

		/* If not XDF, sector ID's are normal so we already point at the correct sector. */
		sr = fdc_seek_by_id(*(uint32_t *) &(fdc.params[1]), &(fdc.track[fdc.drive]), &(fdc.head[fdc.drive]), &(fdc.sector[fdc.drive]));
		if (!sr)
		{
#ifndef RELEASE_BUILD
			pclog("Seek by ID failed (%08X)\n", *(uint32_t *) &(fdc.params[1]));
#endif
			discint = 0xFF;
			fdc_poll();
			return;
		}
		scan_results = 0;
		satisfying_sectors = 0;
		if (fdc.sector[fdc.drive] == 0)
		{
#ifndef RELEASE_BUILD
			pclog("Sector is zero\n");
#endif
			discint = 0xFF;
			fdc_poll();
			return;
		}
		fdd[vfdd[fdc.drive]].rws = 1;
	}
	rbps = real_bps();

	if ((mode == 0) && fdd[vfdd[fdc.drive]].WP || swwp)
	{
		discint = 0x100;
		fdc_poll();
		return;
	}
	if (mode == 0)
	{
		fdd[vfdd[fdc.drive]].discmodified = 1;
	}

	if (fdc.abort[fdc.drive])
	{
		goto rw_result_phase;
	}

	if (fdc.pos[fdc.drive]<rbps)
	{
		for(i = 0; i < quantity; i++)
		{
			if ((mode == 1) || (mode > 3))
			{
				if (!sk || sds_match())
				{
					if (mode == 1)
						fdc.dat = fdd[vfdd[fdc.drive]].disc[fdc.head[fdc.drive]][fdc.track[fdc.drive]][fdc.sector[fdc.drive] - 1][fdc.pos[fdc.drive]];
					else
						internal_in = fdd[vfdd[fdc.drive]].disc[fdc.head[fdc.drive]][fdc.track[fdc.drive]][fdc.sector[fdc.drive] - 1][fdc.pos[fdc.drive]];
				}

				if (!sk)
				{
					if (!(sds_match()))  fdc.st2 |= 0x40;
				}
			}

			if (mode != 2)
			{
				if (mode == 1)
				{
					if (!sk || sds_match())
					{
						if (fdc.pcjr || !fdc.dma)
						{
							fdc.stat = 0xf0;
							fifo_buf_write(fdc.dat);
						}
						else
						{
							fdc.stat = 0xd0;
							if (dma_channel_write(2, fdc.dat) & DMA_OVER)
								fdc.abort[fdc.drive] = 1;
						}
					}
				}
				else
				{
					if (fdc.pcjr || !fdc.dma)
					{
						fdc.stat = 0xb0;
						fdc.dat = fifo_buf_read();
					}
					else
					{
						fdc.stat = 0x90;
						temp = dma_channel_read(2);
						fdc.dat = temp;
						if (temp & DMA_OVER)
							fdc.abort[fdc.drive] = 1;
						if (temp == DMA_NODATA)
						{
							fdc.abort[fdc.drive] = 1;
						}
					}
				}
			}
			// I think this is a must to make sure the abort occur before the sector ID is increased
			if (mode == 0)
			{
				if (temp != DMA_NODATA)
				{
					if (!diswr)  fdd[vfdd[fdc.drive]].disc[fdc.head[fdc.drive]][fdc.track[fdc.drive]][fdc.sector[fdc.drive] - 1][fdc.pos[fdc.drive]] = fdc.dat;
				}

				// Normal state is 0x43 actually
				if (fdc.deldata)
				{
					if (!diswr)  fdd[vfdd[fdc.drive]].sstat[fdc.head[fdc.drive]][fdc.track[fdc.drive]][fdc.sector[fdc.drive]-1] &= 0xBF;
				}
				else
				{
					if (!diswr)  fdd[vfdd[fdc.drive]].sstat[fdc.head[fdc.drive]][fdc.track[fdc.drive]][fdc.sector[fdc.drive]-1] |= 0x40;
				}
			}

			if (mode == 3)
			{
				fdc.st2 |= 8;
				if ((internal_in != fdc.dat) && ((internal_in != 0xFF) || !fdc.scan_wildcard))
				{
					fdc.st2 &= 0xF3;
					scan_results = 2;
					goto rw_result_phase;
				}
			}
			else if (mode == 4)
			{
				fdc.st2 |= 8;
				if ((internal_in > fdc.dat) && ((internal_in != 0xFF) || !fdc.scan_wildcard))
				{
					fdc.st2 &= 0xF3;
					scan_results = 2;
					goto rw_result_phase;
				}
				if ((internal_in < fdc.dat) && ((internal_in != 0xFF) || !fdc.scan_wildcard))
				{
					fdc.st2 &= 0xF3;
				}
			}
			else if (mode == 5)
			{
				fdc.st2 |= 8;
				if ((internal_in < fdc.dat) && ((internal_in != 0xFF) || !fdc.scan_wildcard))
				{
					fdc.st2 &= 0xF3;
					scan_results = 2;
					goto rw_result_phase;
				}
				if ((internal_in > fdc.dat) && ((internal_in != 0xFF) || !fdc.scan_wildcard))
				{
					fdc.st2 &= 0xF3;
				}
			}

                       	timer_process();
			disctime = ((mode == 0) ? 600 : (((fdc.command & 0x1f) == 2) ? 60 : 256)) * (1 << TIMER_SHIFT) * 3;
                       	timer_update_outstanding();

			fdc.pos[fdc.drive]++;
			if(fdc.pos[fdc.drive] >= rbps)
			{
				// We've gone beyond the sector
				// We've gone beyond the sector
				if (mode > 2)
				{
					if (mode == 3)
					{
						if ((fdc.st2 & 0xC) == 8)  satisfying_sectors++;
					}
					else
					{
						if (!(fdc.st2 & 4))
						{
							satisfying_sectors++;
							if (fdc.st2 & 8)
								if (scan_results == 0)  scan_results = 0;
							else
								scan_results = 1;
						}						
					}
					fdc.st2 &= 0xF3;
				}
				
				fdc.pos[fdc.drive] -= rbps;
				if (!sk || sds_match())  fdd[vfdd[fdc.drive]].sstates += sector_state(mode);
				/* Abort if CRC error is found and command is not read track. */
				if (!rt && sector_state(mode))
					goto rw_result_phase;
				if (mode < 3)
					fdc.sector[fdc.drive]++;
				else
					fdc.sector[fdc.drive] += step;
				sc++;
				if (!(sector_has_id()))
				{
					// We've auto-incremented into a sector without an ID
					// This means we've reached the end of the track
					// Decrease sector and abort
					if (!mt || (mt && (fdc.head[fdc.drive] == (fdd[vfdd[fdc.drive]].SIDES - 1))))
					{
						fdc.sector[fdc.drive]--;
						goto end_of_track;
					}
				}
				if ((fdc.sector[fdc.drive] > fdc.params[5]) && !ec)
				{
					// Sector is bigger than the limit
					fdc.sector[fdc.drive] = 1;
					if (mt)
					{
						// Multi-track mode
						fdc.head[fdc.drive] ^= 1;
#if 0
						if ((fdc.head[fdc.drive] == 0) || (fdd[vfdd[fdc.drive]].SIDES == 1))
						{
							fdc.sector[fdc.drive] = 1;
							fdc.head[fdc.drive] = 0;
							fdc.pos[fdc.drive] = 0;
							fdc.track[fdc.drive]++;
							fdd_seek(vfdd[fdc.drive], 1, 1);
							fdc.pcn[fdc.drive]++;
							fdc.abort[fdc.drive] = 1;
						}
#endif
						/* Multitrack should end at the end of the track. */
						if ((fdc.head[fdc.drive] == 0) && (fdd[vfdd[fdc.drive]].SIDES == 2))
						{
							fdc.head[fdc.drive] = 1;
							fdc.abort[fdc.drive] = 1;
						}
						if (fdd[vfdd[fdc.drive]].SIDES == 1)
						{
							fdc.head[fdc.drive] = 0;
							fdc.abort[fdc.drive] = 1;
						}
						if (fdc.abort[fdc.drive])
						{
							fdc.sector[fdc.drive] = fdd[vfdd[fdc.drive]].spt[fdd[vfdd[fdc.drive]].track];
							fdc.pos[fdc.drive] = 0;
						}
					}
					else
					{
end_of_track:
						fdc.sector[fdc.drive] = 1;
						if (fdc.head[fdc.drive])
						{
							fdc.head[fdc.drive] = 0;
							fdc.track[fdc.drive]++;
							fdd_seek(vfdd[fdc.drive], 1, 1);
							fdc.pcn[fdc.drive]++;
						}
						else
							fdc.head[fdc.drive] = 1;
						fdc.pos[fdc.drive] = 0;
						fdc.abort[fdc.drive] = 1;
					}
				}
				if ((fdc.sector[fdc.drive] > fdd[vfdd[fdc.drive]].spt[fdc.track[fdc.drive]]) && ec)
				{
					// Sector is bigger than the limit
					fdc.sector[fdc.drive] = 1;

					fdc.head[fdc.drive] ^= 1;
					if ((fdc.head[fdc.drive] == 0) || (fdd[vfdd[fdc.drive]].SIDES == 1))
					{
						fdc.track[fdc.drive]++;
						fdd_seek(vfdd[fdc.drive], 1, 1);
						fdc.pcn[fdc.drive]++;
						if (fdd[vfdd[fdc.drive]].SIDES == 1)  fdc.head[fdc.drive] = 0;
						if (fdc.track[fdc.drive] >= fdd[vfdd[fdc.drive]].TRACKS)
						{
#ifndef RELEASE_BUILD
							pclog("Reached the end of the disk\n");
#endif
							fdc.track[fdc.drive] = fdd[vfdd[fdc.drive]].TRACKS - 1;
							fdc.pos[fdc.drive] = rbps;
							fdc.abort[fdc.drive] = 1;
						}
					}
				}
				if ((sc > maxs) && ec)
				{
					fdc.pos[fdc.drive] = rbps;
					// Make sure we point to the last sector read/written, not to the next
					fdc.abort[fdc.drive] = 1;
				}
			}
rw_break:
			;
		}
		return;
	}
	else
	{
rw_result_phase:
		fdc.abort[fdc.drive] = 0;
		disctime=0;
		discint=-2;
		fdc_int();
		fdc.stat=0xd0;
		fdc.res[4]=(fdc.head[fdc.drive]?4:0)|fdc.drive;
		if (fdd[vfdd[fdc.drive]].sstates)  fdc.res[4] |= 0x40;
		// if (fdd[vfdd[fdc.drive]].sstates)  pclog("ERROR: sstates = %02X\n", fdd[vfdd[fdc.drive]].sstates);
		fdc.st0=fdc.res[4];
		fdc.res[5]=fdc.st1;

		if (mode > 2)
		{
			fdc.st2 &= 0xF3;

			switch (scan_results)
			{
				case 0:
					fdc.st2 |= 8;
					break;
				case 1:
					fdc.st2 |= 0;
					break;
				case 2:
					fdc.st2 |= 4;
					break;
			}
		}

		fdc.res[6]=fdc.st2;
rw_result_phase_after_statuses:
		fdc.st1 = 0;
		fdc.st2 = 0;
		if (fdd[vfdd[fdc.drive]].XDF && (fdc.track[fdc.drive] >= 1))
		{
			xd = fdd[vfdd[fdc.drive]].XDF;
			for (i = 0; i < xdf_spt[xd]; i++)
			{
				if ((xdf_map[xd][i][0] == fdc.params[2]) && (xdf_map[xd][i][2] == fdc.params[4]))
				{
					if ((i + 1) < xdf_spt[xd])
					{
						fdc.res[8] = xdf_map[xd][i + 1][0];
						fdc.res[10] = xdf_map[xd][i + 1][2];
					}
					else
					{
						fdc.res[8] = xdf_map[xd][i][0];
						fdc.res[10] = xdf_map[xd][i][2];
					}
					fdc.res[7] = fdc.track[fdc.drive];
					fdc.res[10] &= 0x7F;
					fdc.res[9] = fdc.res[10] | 0x80;
					break;
				}
			}
		}
		else
		{
			fdc.res[7]=fdd[vfdd[fdc.drive]].scid[fdc.head[fdc.drive]][fdc.track[fdc.drive]][fdc.sector[fdc.drive]-1][0];
			fdc.res[8]=fdd[vfdd[fdc.drive]].scid[fdc.head[fdc.drive]][fdc.track[fdc.drive]][fdc.sector[fdc.drive]-1][1];
			fdc.res[9]=fdd[vfdd[fdc.drive]].scid[fdc.head[fdc.drive]][fdc.track[fdc.drive]][fdc.sector[fdc.drive]-1][2];
			fdc.res[10]=fdd[vfdd[fdc.drive]].scid[fdc.head[fdc.drive]][fdc.track[fdc.drive]][fdc.sector[fdc.drive]-1][3];
		}
		paramstogo=7;
		fdd[vfdd[fdc.drive]].rws=0;
		return;
	}
}

void fdc_poll()
{
        int temp;
	int a = 0;
	int n = 0;
	int rbps = 0;
	int maxsteps = 79;
	if (romset == ROM_ENDEAVOR)  maxsteps = 85;
        disctime = 0;
        // pclog("fdc_poll %08X %i %02X\n", discint, fdc.drive, fdc.st0);
        switch (discint)
        {
                case -4: /*End of command with interrupt and no result phase*/
	                fdc_int();
			fdc.stat = 0;
			return;
                case -3: /*End of command with interrupt*/
	                fdc_int();
                case -2: /*End of command*/
	                fdc.stat = (fdc.stat & 0xf) | 0x80;
        	        return;
                case -1: /*Reset*/
	                fdc_int();
	                fdc_reset_stat = 4;
	                return;
                case 2: /*Read track*/
			fdc_readwrite(1);
	                return;
                case 3: /*Specify*/
	                fdc.stat=0x80;
	                fdc.specify[0] = fdc.params[0];
	                fdc.specify[1] = fdc.params[1];
			fdc.dma = 1 - (fdc.specify[1] & 1);
	                return;

                case 4: /*Sense drive status*/
			fdc.res[10] = fdc.params[0] & 7;
	                fdc.res[10] |= 0x28;
			if (!fdd[vfdd[fdc.drive]].driveempty)
			{
				if (fdd[vfdd[fdc.drive]].WP || swwp) fdc.res[10] |= 0x40;
			}

			if (fdd[vfdd[fdc.drive]].trk0)  fdc.res[10] |= 0x10;
#ifdef CRUDE_SEEK
			if ((fdc.drive == 1) && ((romset == ROM_COLORBOOK) || !drv2en))  goto no_track_0;
			if (fdd[vfdd[fdc.drive]].floppy_drive_enabled)
			{
				if (get_step() == 2)
				{
	        	        	if (fdc.pcn[fdc.drive] == 0) fdc.res[10] |= 0x10;
				}
				else
				{
	        	        	if (fdc.track[fdc.drive] == 0) fdc.res[10] |= 0x10;
				}
			}
			// if (IS_BIG && !VF_DEN)  if (fdc.pcn[fdc.drive] == 1) fdc.res[10] |= 0x10;

no_track_0:
#endif
	                fdc.stat = (fdc.stat & 0xf) | 0xd0;
	                paramstogo = 1;
	                discint = 0;
	                disctime = 0;
	                return;
		case 5:
		case 9:
			fdc_readwrite(0);
			return;
		case 6:
		case 12:
			fdc_readwrite(1);
			return;
		case 0x16:
			fdc_readwrite(2);
			return;
                case 7: /*Recalibrate*/
			fdc_seek();
			return;

                case 8: /*Sense interrupt status*/               
	                fdc.dat = fdc.st0;

			/* Make first post-reset sense return ST0 as 0, which Grey Cat Linux apparently expects. */
			if (fdc_reset_stat == 16)
			{
				fdc.st0 = 0;
				fdc_reset_stat = 0;
			}

	                if (fdc_reset_stat)
        	        {
				fdc.st0 &= 0xf8;
				/* Modified to avoid spurious values when non-existent 3rd and 4th drives are being reset. */
				if ((4 - fdc_reset_stat) > 1)
					fdc.st0|=(4 - fdc_reset_stat);
				else
					fdc.st0|=(fdc.head[4 - fdc_reset_stat]?4:0)|(4 - fdc_reset_stat);
	                        fdc_reset_stat--;
				if (!fdc_reset_stat)  fdc_reset_stat = 16;
	                }

	                fdc.stat    = (fdc.stat & 0xf) | 0xd0;
			// pclog("SIS: ST0: %02X\n", fdc.st0);
	                fdc.res[9]  = fdc.st0;
	                fdc.res[10] = fdc.pcn[fdc.drive];
			// fdc.res[10] = fdc.track[fdc.drive];
	                if ((!fdc_reset_stat) || (fdc_reset_stat == 16)) fdc.st0 = 0x80;

	                paramstogo = 2;
	                discint = 0;
	                disctime = 0;
	                return;
                case 10: /*Read sector ID*/
	                disctime=0;
	                discint=-2;
	                fdc_int();
	                fdc.stat=0xD0;
			fdc.drive = fdc.params[0] & 3;
			fdc.head[fdc.drive] = (fdc.params[0] & 4) ? 1 : 0;
			if (fdc.sector[fdc.drive] <= 0)  fdc.sector[fdc.drive] = 1;
			fdc.res[4]=(fdc.head[fdc.drive]?4:0)|fdc.drive;
			// pclog("RSID: C:%02X H:%02X R:%02X\n", fdc.track[fdc.drive], fdc.head[fdc.drive], fdc.sector[fdc.drive]);
			/* fdd[vfdd[fdc.drive]].sstates = sector_state(3);
			if (fdd[vfdd[fdc.drive]].sstates)  fdc.res[4] |= 0x40;
			if (fdd[vfdd[fdc.drive]].sstates)  fatal("%c: ID not found\n", 0x41 + fdc.drive); */
			fdc.st0=fdc.res[4];
	                fdc.res[5]=fdc.st1;
	                fdc.res[6]=fdc.st2;
			fdc.st1=0;
			fdc.st2=0;
			fdc.res[7]=fdd[vfdd[fdc.drive]].scid[fdc.head[fdc.drive]][fdd[vfdd[fdc.drive]].track][fdc.sector[fdc.drive]-1][0];
			fdc.res[8]=fdd[vfdd[fdc.drive]].scid[fdc.head[fdc.drive]][fdd[vfdd[fdc.drive]].track][fdc.sector[fdc.drive]-1][1];
			fdc.res[9]=fdd[vfdd[fdc.drive]].scid[fdc.head[fdc.drive]][fdd[vfdd[fdc.drive]].track][fdc.sector[fdc.drive]-1][2];
			fdc.res[10]=fdd[vfdd[fdc.drive]].scid[fdc.head[fdc.drive]][fdd[vfdd[fdc.drive]].track][fdc.sector[fdc.drive]-1][3];
#ifndef RELEASE_BUILD
			pclog("RSID: %02X %02X %02X %02X %02X %02X %02X\n", fdc.res[4], fdc.res[5], fdc.res[6], fdc.res[7], fdc.res[8], fdc.res[9], fdc.res[10]);
#endif
	                paramstogo=7;
	                return;

		case 13: /*Format*/
			fdc_format_command();
			return;

                case 15: /*Seek*/
			fdc_seek();
			return;

		case 0x11:
			fdc_readwrite(3);
			return;
		case 0x19:
			fdc_readwrite(4);
			return;
		case 0x1D:
			fdc_readwrite(5);
			return;
                case 0x0e: /*Dump registers*/
	                fdc.stat = (fdc.stat & 0xf) | 0xd0;
	                fdc.res[1] = fdc.pcn[0];
	                fdc.res[2] = fdc.pcn[1];
	                fdc.res[3] = 0;
	                fdc.res[4] = 0;
	                fdc.res[5] = fdc.specify[0];
	                fdc.res[6] = fdc.specify[1];
	                fdc.res[7] = fdc.eot[fdc.drive];
	                fdc.res[8] = (fdc.perp & 0x7f) | ((fdc.lock) ? 0x80 : 0);
			fdc.res[9] = fdc.config;
			fdc.res[10] = fdc.pretrk;
	                paramstogo=10;
	                discint=0;
	                disctime=0;
        	        return;

                case 0x10: /*Version*/
	                fdc.stat = (fdc.stat & 0xf) | 0xd0;
                	fdc.res[10] = 0x90;
	                paramstogo=1;
	                discint=0;
	                disctime=0;
	                return;
                
                case 0x12:
	                fdc.perp = fdc.params[0];
	                fdc.stat = 0x80;
	                disctime = 0;
	                return;
                case 0x13: /*Configure*/
	                fdc.config = fdc.params[1];
	                fdc.pretrk = fdc.params[2];
			fdc.fifo = (fdc.params[1] & 0x20) ? 0 : 1;
			fdc.tfifo = (fdc.params[1] & 0xF) + 1;
			pclog("FIFO is now %02X, threshold is %02X\n", fdc.fifo, fdc.tfifo);
			fdc.eis = (fdc.params[1] & 0x40) ? 1 : 0;
#ifndef RELEASE_BUILD
			pclog("Implied seek now %s\n", fdc.eis ? "enabled" : "disabled");
#endif
	                fdc.stat = 0x80;
	                disctime = 0;
	                return;
                case 0x14: /*Unlock*/
			// Reusing the same value for lock
	                fdc.lock = 0;
	                fdc.stat = (fdc.stat & 0xf) | 0xd0;
	                fdc.res[10] = 0;
	                paramstogo=1;
	                discint=0;
	                disctime=0;
	                return;
                case 0x94: /*Lock*/
	                fdc.lock = 1;
	                fdc.stat = (fdc.stat & 0xf) | 0xd0;
	                fdc.res[10] = 0x10;
	                paramstogo=1;
	                discint=0;
	                disctime=0;
	                return;

                case 0xfc: /*Invalid*/
	                fdc.dat = fdc.st0 = 0x80;
	                fdc.stat = (fdc.stat & 0xf) | 0xd0;
	                fdc.res[10] = fdc.st0;
	                paramstogo=1;
	                discint=0;
	                disctime=0;
	                return;

                case 0xFD: /*DMA aborted (PC1512)*/
	                /*In the absence of other information, lie and claim the command completed successfully.
	                  The PC1512 BIOS likes to program the FDC to write to all sectors on the track, but
	                  program the DMA length to the number of sectors actually transferred. Not aborting
	                  correctly causes disc corruption.
	                  This only matters on writes, on reads the DMA controller will ignore excess data.
	                  */
			/*This also happens with regular PC BIOS'es, such as the i430VX, for both reads and
			  writes. Maybe the FDC is supposed to keep reading but stop filling the DMA buffer
			  after it's full? Yes, that is exactly what is supposed to happen. */
	                disctime=0;
	                discint=-2;
	                fdc_int();
        	        fdc.stat=0xd0;
			fdc.res[4]=(fdc.head[fdc.drive]?4:0)|fdc.drive;
			fdc.st0=fdc.res[4];
	                fdc.res[5]=0;
	                fdc.res[6]=0;
			fdc.res[7]=fdd[vfdd[fdc.drive]].scid[fdc.head[fdc.drive]][fdd[vfdd[fdc.drive]].track][fdc.sector[fdc.drive]-1][0];
			fdc.res[8]=fdd[vfdd[fdc.drive]].scid[fdc.head[fdc.drive]][fdd[vfdd[fdc.drive]].track][fdc.sector[fdc.drive]-1][1];
			fdc.res[9]=fdd[vfdd[fdc.drive]].scid[fdc.head[fdc.drive]][fdd[vfdd[fdc.drive]].track][fdc.sector[fdc.drive]-1][2];
			fdc.res[10]=fdd[vfdd[fdc.drive]].scid[fdc.head[fdc.drive]][fdd[vfdd[fdc.drive]].track][fdc.sector[fdc.drive]-1][3];
	                /* fdc.res[7]=fdc.track[fdc.drive];
	                fdc.res[8]=fdc.head[fdc.drive];
	                fdc.res[9]=fdc.sector[fdc.drive];
	                fdc.res[10]=fdc.params[4]; */
	                paramstogo=7;
	                return;
                case 0xFE: /*Drive empty*/
	                fdc.stat = 0x10;
	                disctime = 0;
	                return;
                case 0xFF: /*Sector not found*/
                case 0x102: /*Wrong cylinder*/
                case 0x103: /*Bad cylinder*/
                case 0x104: /*No sectors*/
	                fdc.stat = 0x10;
	               	disctime=0;
	               	discint=-2;
	               	fdc_int();
	                fdc.stat=0xd0;
			fdc.res[4]=(fdc.head[fdc.drive]?4:0)|fdc.drive;
			fdc.res[4]|=0x40;
			fdc.st0=fdc.res[4];
	               	fdc.res[5]=5;
			if (discint == 0xFF)
		               	fdc.res[6]=0;
			else if (discint == 0x102)
		               	fdc.res[6]=0x10;
			else if (discint == 0x103)
		               	fdc.res[6]=2;
			else if (discint == 0x104)
		               	fdc.res[6]=1;
	               	fdc.res[7]=0;
	               	fdc.res[8]=0;
	               	fdc.res[9]=0;
	               	fdc.res[10]=0;
	               	paramstogo=7;
	                return;

                case 0x100: /*Write protected*/
	                fdc.stat = 0x10;
	                disctime=0;
	                discint=-2;
	                fdc_int();
	                fdc.stat=0xd0;
			fdc.res[4]=0x40|(fdc.head[fdc.drive]?4:0)|fdc.drive;
			fdc.st0=fdc.res[4];
	                fdc.res[5]=2;
	                fdc.res[6]=0;
	                fdc.res[7]=0;
	                fdc.res[8]=0;
	                fdc.res[9]=0;
	                fdc.res[10]=0;
	                paramstogo=7;
	                return;

                case 0x101: /*Track too big*/
	                fdc.stat = 0x10;
	                disctime=0;
	                discint=-2;
	                fdc_int();
	                fdc.stat=0xd0;
			fdc.res[4]=0x40|(fdc.head[fdc.drive]?4:0)|fdc.drive;
			fdc.st0=fdc.res[4];
	                fdc.res[5]=0x80;
	                fdc.res[6]=0;
	                fdc.res[7]=0;
	                fdc.res[8]=0;
	                fdc.res[9]=0;
	                fdc.res[10]=0;
	                paramstogo=7;
	                return;

		default:
#ifndef RELEASE_BUILD
			pclog("Unknown discint %08X issued\n", discint);
#endif
			return;
        }
#ifndef RELEASE_BUILD
        printf("Bad FDC disc int %i\n",discint);
#endif
//        dumpregs();
//        exit(-1);
}

void fdc_init()
{
	timer_add(fdc_poll, &disctime, &disctime, NULL);
	fdc.dskchg_activelow = 0;
	config_default();
	/*
		Setting this to -1 means "do not care, return always 1 for 3.5 inch floppy drives".
		Whatever Super I/O Chip actually cares about this, should set it to 0 or 1 accordingly.
	*/
	// densel_polarity = -1;
	densel_polarity = 1;
	densel_polarity_mid[0] = -1;
	densel_polarity_mid[1] = -1;
	densel_force = 0;
	rwc_force[0] = 0;
	rwc_force[1] = 0;
	en3mode = 0;
	diswr = 0;
	swwp = 0;
	drv2en = 1;
	fdc_setswap(0);
	fdc.dor |= (fdc.pcjr ? 0x80 : 4);
	// fdc.track[1] = 0x16;
	fdc.eis = 1;
	fdc.config |= 0x40;
}

void fdc_hard_reset()
{
	timer_add(fdc_poll, &disctime, &disctime, NULL);
	config_default();
	densel_polarity = 1;
	densel_polarity_mid[0] = -1;
	densel_polarity_mid[1] = -1;
	densel_force = 0;
	rwc_force[0] = 0;
	rwc_force[1] = 0;
	en3mode = 0;
	diswr = 0;
	swwp = 0;
	drv2en = 1;
	fdc.dor |= (fdc.pcjr ? 0x80 : 4);
	fdc_setswap(0);
	fdc.eis = 1;
	fdc.config |= 0x40;
}

void fdc_add_ex(uint16_t port, uint8_t superio)
{
	// pclog("Readding FDC (superio = %u)...\n", superio);
	sio = superio;
	fdcport = port;
	if (superio)
	        io_sethandler(port + 2, 0x0004, fdc_read, NULL, NULL, fdc_write, NULL, NULL, NULL);
	else
	        io_sethandler(port, 0x0006, fdc_read, NULL, NULL, fdc_write, NULL, NULL, NULL);
        io_sethandler(port + 7, 0x0001, fdc_read, NULL, NULL, fdc_write, NULL, NULL, NULL);
        fdc.pcjr = 0;
}

void fdc_add()
{
	fdc_add_ex(0x3f0, 0);
}

void fdc_add_pcjr()
{
        io_sethandler(0x00f0, 0x0006, fdc_read, NULL, NULL, fdc_write, NULL, NULL, NULL);
	timer_add(fdc_watchdog_poll, &fdc.watchdog_timer, &fdc.watchdog_timer, &fdc);
        fdc.pcjr = 1;
	fdc_set_dskchg_activelow();
}

void fdc_remove_ex(uint16_t port)
{
	// pclog("Removing FDC (sio = %u)...\n", sio);
        if (sio)
		io_removehandler(port + 2, 0x0004, fdc_read, NULL, NULL, fdc_write, NULL, NULL, NULL);
	else
		io_removehandler(port, 0x0006, fdc_read, NULL, NULL, fdc_write, NULL, NULL, NULL);
        if (!fdc.pcjr)  io_removehandler(port + 7, 0x0001, fdc_read, NULL, NULL, fdc_write, NULL, NULL, NULL);        
}

void fdc_remove_stab()
{
	/* Remove 0x3F0 (STA) and 0x3F1 (STB) so that a Super I/O Chip can be installed on these two ports. */
	sio = 0;
	fdc_remove_ex(fdcport);
	fdc_add_ex(fdcport, 1);
}

void fdc_remove()
{
	/* Remove both */
	fdc_remove_ex(0x3f0);
	fdc_remove_ex(0x370);
}

FDD fdd[2];
uint8_t vfdd[2] = {0, 1};

void fdc_setswap(int val)
{
	drive_swap = val;

	switch(val)
	{
		case 0:
			vfdd[0] = 0;
			vfdd[1] = 1;
			break;
		case 1:
			vfdd[0] = 1;
			vfdd[1] = 0;
			break;
	}
}

void fdc_set_dskchg_activelow()
{
	fdc.dskchg_activelow = 1;
}

void fdc_clear_dskchg_activelow()
{
	fdc.dskchg_activelow = 0;
}
