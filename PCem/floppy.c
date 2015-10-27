#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include "ibm.h"

#include "dma.h"
#include "io.h"
#include "pic.h"
#include "timer.h"

#include "fdc.h"
#include "floppy.h"

/* Bit multiplex functions */
// Head-sector size multiplex
uint8_t get_h_from_hn(uint8_t hn)
{
	return ((hn >> 6) & 1) | (hn & 0xf0);
}

uint8_t get_n_from_hn(uint8_t hn)
{
	return (hn & 7);
}

/* Functions for dealing with sector states */
uint8_t current_state()
{
	return fdd[vfdd[fdc.drive]].scid[fdc.head[fdc.drive]][fdc.track[fdc.drive]][fdc.sector[fdc.drive]-1][4];
}

int ss_good_sector1(uint8_t state)
{
	return ((state & 0xff) == 0xff);
}

int ss_good_other(uint8_t state)
{
	return ((state & 0xbf) == 0xbf);
}

int ss_good(uint8_t state)
{
	return (fdc.sector[fdc.drive] == 1) ? ss_good_sector1(state) : ss_good_other(state);
}

int ss_idam_present(uint8_t state)
{
	return (state & 0x80) ? 1 : 0;
}

int ss_iam_present(uint8_t state)
{
	return (state & 0x40) ? 1 : 0;
}

int ss_dam_nondel(uint8_t state)
{
	return (state & 0x20) ? 1 : 0;
}

int ss_dam_present(uint8_t state)
{
	return (state & 0x10) ? 1 : 0;
}

int ss_data_crc_present(uint8_t state)
{
	return (state & 8) ? 1 : 0;
}

int ss_id_crc_present(uint8_t state)
{
	return (state & 4) ? 1 : 0;
}

int ss_data_crc_correct(uint8_t state)
{
	return (state & 2) ? 1 : 0;
}

int ss_id_crc_correct(uint8_t state)
{
	return (state & 1) ? 1 : 0;
}

/* Floppy class functions */
int getshift(int val)
{
	int i = 0;
	for (i = 0; i <= 7; i++)
	{
		if ((128 << i) == val)  return i;
	}
	return 8;
}

int get_class(int total, int sides, uint8_t mid)
{
	if (sides > 2)  return -1;
	if (sides < 1)  return -1;
	if (((total / sides) * 2) > 8000000)  return -1;

	switch(mid)
	{
		case M_35_2H2E:
			if (sides == 1)  return -1;
			if (total <= 1000000)  return -1;
			if (total <= 2000000)  return CLASS_2000;
			if (total <= 4000000)  return CLASS_4000;
			return CLASS_8000;
			break;
		case M_35_1D9:
			if (sides == 2)  return -1;
			if (total <= 250000)  return -1;
			if (total > 500000)  return -1;
			return CLASS_1000;
			break;
		case M_35_2D9_525_2H2E:
			if (sides == 1)  return -1;
			if (total <= 500000)  return -1;
			if (total > 6666666)  return -1;
			if (total <= 1000000)  return CLASS_1000;
			if (total <= 1666666)  return CLASS_1600;
			if (total <= 3333333)  return CLASS_3200;
			return CLASS_6400;
			break;
		case M_35_1D8:
			if (sides == 2)  return -1;
			if (total <= 250000)  return -1;
			if (total > 500000)  return -1;
			return CLASS_1000;
			break;
		case M_35_2D8:
			if (sides == 1)  return -1;
			if (total <= 500000)  return -1;
			if (total > 1000000)  return -1;
			return CLASS_1000;
			break;
		case M_525_1D9:
			if (sides == 2)  return -1;
			if (total > 250000)  return -1;
			return CLASS_500;
			break;
		case M_525_2D9:
			/* Actually, allow single-sided images too, due to WinImage incorrectly using this
			   Media Type ID for 160 kB, 180 kB, and 320 kB images. */
			// if (sides == 1)  return -1;
			// if (total <= 250000)  return -1;
			// Make sure 5.25" 1QD/3.5" 1DD images with this Media Type ID are rejected.
			if ((total > 250000) && (sides == 1))  return -1;
			if (total > 500000)  return -1;
			return CLASS_500;
			break;
		case M_35_3M_525_1D8:
			if ((total <= 250000) && (sides == 2))  return -1;
			if ((total > 1000000) && (sides == 1))  return -1;
			if ((total > 250000) && (total <= 1000000))  return -1;
			if (total > 6666666)  return -1;
			if (total <= 250000)  return CLASS_500;
			if (total <= 1666666)  return CLASS_1600;
			if (total <= 3333333)  return CLASS_3200;
			return CLASS_6400;
			break;
		case M_525_2D8:
			if (sides == 1)  return -1;
			if (total <= 250000)  return -1;
			if (total > 500000)  return -1;
			return CLASS_500;
			break;
		default:
			return -1;
			break;
	}
}

int getmaxbytes(int d)
{
	switch(fdd[d].CLASS)
	{
		case CLASS_400:
			return (fdd[d].SIDES == 2) ? 416666 : 208333;
			break;
		case CLASS_500:
			return 250000 * fdd[d].SIDES;
			break;
		case CLASS_800:
			return (fdd[d].SIDES == 2) ? 833333 : 416666;
			break;
		case CLASS_1000:
			return 500000 * fdd[d].SIDES;
			break;
		case CLASS_1600:
			return (fdd[d].SIDES == 2) ? 1666666 : 833333;
			break;
		case CLASS_2000:
			return 1000000 * fdd[d].SIDES;
			break;
		case CLASS_3200:
			return (fdd[d].SIDES == 2) ? 3333333 : 1666666;
			break;
		case CLASS_4000:
			return 2000000 * fdd[d].SIDES;
			break;
		case CLASS_6400:
			return (fdd[d].SIDES == 2) ? 6666666 : 3333333;
			break;
		case CLASS_8000:
			return 4000000 * fdd[d].SIDES;
			break;
		default:
			return -1;
			break;
	}
}

int getminbytes(int d)
{
	switch(fdd[d].CLASS)
	{
		case CLASS_400:
			return (fdd[d].SIDES == 2) ? 208333 : 104166;
			break;
		case CLASS_500:
			return 125000 * fdd[d].SIDES;
			break;
		case CLASS_800:
			return (fdd[d].SIDES == 2) ? 416666 : 208333;
			break;
		case CLASS_1000:
			return 250000 * fdd[d].SIDES;
			break;
		case CLASS_1600:
			return (fdd[d].SIDES == 2) ? 833333 : 416666;
			break;
		case CLASS_2000:
			return 500000 * fdd[d].SIDES;
			break;
		case CLASS_3200:
			return (fdd[d].SIDES == 2) ? 1666666 : 833333;
			break;
		case CLASS_4000:
			return 1000000 * fdd[d].SIDES;
			break;
		case CLASS_6400:
			return (fdd[d].SIDES == 2) ? 3333333 : 1666666;
			break;
		case CLASS_8000:
			return 2000000 * fdd[d].SIDES;
			break;
		default:
			return -1;
			break;
	}
}

int samediskclass(int d, int c, int s, int n)
{
	int b = 128 << n;
	int total = c * s * fdd[d].SIDES;

	int rate = 2;

#ifndef RELEASE_BUILD
	pclog("samediskclass(%02X, %02X, %02X, %02X)\n", d, c, s, n);
#endif

	// Special (easier) processing for PEF images
	// Now testing it for all images
	if ((total * b) < getmaxbytes(d))
	{
		if ((total * b) <= getminbytes(d))
		{
			return 0;
		}
		else
		{
			return 1;
		}
	}
	else
	{
		return 0;
	}
}

/* Other */
void initsectors(int d)
{
	int t = 0;
	int h = 0;
	int s = 0;
	int b = 0;

        for (t=0;t<86;t++)
        {
                for (h=0;h<2;h++)
                {
                        for (s=0;s<255;s++)
                        {
				// memset(fdd[d].scid[h][t][s], 255, 4);
                        }
                }
		fdd[d].spt[t]=0;
        }
}

void freesectors(int d)
{
	int t = 0;
	int h = 0;
	int s = 0;
	int b = 0;

        for (t=0;t<85;t++)
        {
                for (h=0;h<2;h++)
                {
			// memset(fdd[d].trackbufs[h][t], 0, 50000);
                        for (s=0;s<255;s++)
                        {
				fdd[d].disc[h][t][s] = NULL;
                                for (b=0;b<4;b++)
                                {
					/* Illegal sector state */
					fdd[d].scid[h][t][s][b]=0xE3;
                                }
                        }
                }
		fdd[d].spt[t]=0;
        }
}

void freetracksectors(int d, int h, int t)
{
	int s = 0;

	for (s=0;s<255;s++)
	{
		// if (!(fdd[d].scid[h][t][s][4] & 0xC0))  fdd[d].disc[h][t][s] = NULL;
		fdd[d].disc[h][t][s] = NULL;
		fdd[d].scid[h][t][s][0] = 255;
		fdd[d].scid[h][t][s][1] = 255;
		fdd[d].scid[h][t][s][2] = 255;
		fdd[d].scid[h][t][s][3] = 255;
		fdd[d].scid[h][t][s][4] = 3;
	}
}

void defaultsstates(int d)
{
	int t = 0;
	int h = 0;
	int s = 0;
	if (fdd[d].CLASS != CLASS_INV)
	{
       		for (t=0;t<85;t++)
       		{
               		for (h=0;h<2;h++)
               		{
                       		for (s=0;s<255;s++)
                       		{
                                       	fdd[d].sstat[h][t][s]=0xBF;
					if (s == 0)  fdd[d].sstat[h][t][s]|=0x40;
                       		}
               		}
       		}
	}
}

void process_byte_A(int d, uint8_t byteA)
{
	if (byteA & 1)
	{
		fdd[d].XDF = (byteA >> 1) & 0xf;
		fdd[d].CLASS = (byteA >> 5) & 7;
	}
}

void create_byte_A(int d, uint8_t *byteA)
{
	if (*byteA & 1)
	{
		*byteA = 1;
		*byteA |= (fdd[d].XDF << 1);
		*byteA |= (fdd[d].CLASS << 5);
	}
	else
	{
		*byteA = 0;
	}
}

void ejectdisc(int d)
{
	freesectors(d);
        discfns[d][0]=0;
        fdd[d].SECTORS=9; fdd[d].SIDES=1;
        fdd[d].driveempty=1;
        fdd[d].discchanged=1;
	fdd[d].CLASS = -1;
	fdd[d].IMGTYPE = IMGT_NONE;
	fdc.format_started[vfdd[d]] = 0;
	fdc.track[vfdd[d]] = 0;
	fdc.head[vfdd[d]] = 0;
	fdc.sector[vfdd[d]] = 1;
	fdc.pos[vfdd[d]] = 0;
	fdc.deldata = 0;
	fdc.gotdata[vfdd[d]] = 0;
	fdd[d].XDF = 0;
	// fdd[d].CLASS = 0;
	fdd[d].LITE = 0;
	fdd[d].discmodified=0;
}

void initialize_sector(int d, int t, int h, int s, int nb, uint8_t b)
{
	int i = 0;

	for (i = 0; i < (128 << nb); i++)
	{
		fdd[d].disc[h][t][s][i] = b;
	}
}

void set_sector_id(int d, int t, int h, int s, int sid, int nb)
{
	uint8_t t2 = t;
	if (ISSPECIAL)  t2 >>= 1;

	if ((fdd[d].IMGTYPE == IMGT_PEF) && (fdd[d].IDTYPE != 1))  return;

	fdd[d].scid[h][t][s][0]=t2;
	fdd[d].scid[h][t][s][1]=h;
	fdd[d].scid[h][t][s][2]=sid;
	fdd[d].scid[h][t][s][3]=nb;

	if (fdd[d].IMGTYPE == IMGT_PEF)  return;

	if ((fdd[d].scid[h][t][s][4] & 0xf) == 3)
	{
		fdd[d].scid[h][t][s][4]=0xbf;
		if (!s)  fdd[d].scid[h][t][s][4]|=0x40;
	}

	fdd[d].disc[h][t][s] = (uint8_t *) malloc(128 << nb);
	initialize_sector(d, t, h, s, nb, 0xF6);
}

void set_sector_id_nh(int d, int t, int h, int s, int sid, int nb)
{
	if ((fdd[d].IMGTYPE == IMGT_PEF) && (fdd[d].IDTYPE != 1))  return;

	fdd[d].scid[h][t][s][0]=t;
	fdd[d].scid[h][t][s][1]=1-h;
	fdd[d].scid[h][t][s][2]=sid;
	fdd[d].scid[h][t][s][3]=nb;

	if (fdd[d].IMGTYPE == IMGT_PEF)  return;

	if ((fdd[d].scid[h][t][s][4] & 0xf) == 3)
	{
		fdd[d].scid[h][t][s][4]=0xbf;
		if (!s)  fdd[d].scid[h][t][s][4]|=0x40;
	}

	fdd[d].disc[h][t][s] = (uint8_t *) malloc(128 << nb);
	initialize_sector(d, h, t, s, nb, 0xF6);
}

void set_sector_id_2m(int d, int t, int h, int s, int sid, int nb)
{
	if ((fdd[d].IMGTYPE == IMGT_PEF) && (fdd[d].IDTYPE != 1))  return;

	fdd[d].scid[h][t][s][0]=t;
	fdd[d].scid[h][t][s][1]=h+128;
	fdd[d].scid[h][t][s][2]=sid;
	fdd[d].scid[h][t][s][3]=nb;

	if (fdd[d].IMGTYPE == IMGT_PEF)  return;

	if ((fdd[d].scid[h][t][s][4] & 0xf) == 3)
	{
		fdd[d].scid[h][t][s][4]=0xbf;
		if (!s)  fdd[d].scid[h][t][s][4]|=0x40;
	}

	fdd[d].disc[h][t][s] = (uint8_t *) malloc(128 << nb);
	initialize_sector(d, h, t, s, nb, 0xF6);
}

void read_raw_sectors(FILE *f, int d, uint8_t st, uint8_t nt, uint8_t sh, uint8_t nh, uint8_t ss2, uint8_t ns, int nb, uint8_t si)
{
	unsigned int h,t,t2,s,b;
#ifndef RELEASE_BUILD
	pclog("Read raw sectors: floppy is%s special\n", (ISSPECIAL) ? "" : " not");
#endif
	for (t=st;t<(st+nt);t++)
	{
		for (h=sh;h<(sh+nh);h++)
		{
			for (s=ss2;s<(ss2+ns);s++)
			{
				if ((ISSPECIAL && !(t & 1)) || (!(ISSPECIAL)))
				{
					t2 = t;
					// if (ISSPECIAL)  t2 >>= 1;
					if (si)  set_sector_id(d, t2, h, s, s + 1, nb);
					for (b=0;b<(128 << nb);b++)
					{
						if (feof(f))
						{
							fdd[d].disc[h][t][s][b]=0xF6;
						}
						else
						{
							fdd[d].disc[h][t][s][b]=getc(f);
						}
					}
				}
			}
		}
		// pclog("Finished reading track %u\n", t);
	}
}

/* Raw, FDI, PEF without any kind of sector info */
void read_normal_floppy(FILE *f, int d)
{
	unsigned int h,t,s,b;

	if (ISSPECIAL)  fdd[d].TRACKS <<= 1;

	for (h = 0; h < fdd[d].SIDES; h++)
	{
		for (t = 0; t < fdd[d].TRACKS; t++)
		{
			for (s = 0; s < fdd[d].SECTORS; s++)
			{
				fdd[d].disc[h][t][s] = fdd[d].trackbufs[h][t] + (s * fdd[d].BPS);
			}
		}
	}
	for (t=0;t<fdd[d].TRACKS;t++)
	{
		fdd[d].spt[t] = fdd[d].SECTORS;
	}
	read_raw_sectors(f, d, 0, fdd[d].TRACKS, 0, fdd[d].SIDES, 0, fdd[d].spt[0], fdd[d].BPSCODE, 1);
}

/* PEF with sector states */
void pef_set_spt(int d)
{
	unsigned int h,t,s,b;

	for (t=1;t<fdd[d].TRACKS;t++)
	{
		fdd[d].spt[t] = fdd[d].ph.spt;
	}
}

/* PEF with sector ID's */
void pef_read_advanced_floppy(FILE *f, int d)
{
	unsigned int h,t,s,b;

	for (s = 0; s < 21930; s++)
	{
		if (fdd[d].sequential_sectors_index[s][3] >= 8)  break;
		read_raw_sectors(f, d, fdd[d].sequential_sectors_index[s][1], 1, fdd[d].sequential_sectors_index[s][0], 1, fdd[d].sequential_sectors_index[s][2], 1, fdd[d].sequential_sectors_index[s][3], 0);
	}
}

void add_to_map(uint8_t *arr, uint8_t p1, uint8_t p2, uint8_t p3)
{
	arr[0] = p1;
	arr[1] = p2;
	arr[2] = p3;
}

int xdf_maps_initialized = 0;

void initialize_xdf_maps()
{
	// XDF 5.25" 2HD
	add_to_map(xdf_track0[0], 9, 17, 2);
	xdf_spt[0] = 3;
	add_to_map(xdf_map[0][0], 0, 0, 3);
	add_to_map(xdf_map[0][1], 0, 2, 6);
	add_to_map(xdf_map[0][2], 1, 0, 2);
	add_to_map(xdf_map[0][3], 0, 1, 2);
	add_to_map(xdf_map[0][4], 1, 2, 6);
	add_to_map(xdf_map[0][5], 1, 1, 3);

	// XDF 3.5" 2HD
	add_to_map(xdf_track0[1], 11, 19, 4);
	xdf_spt[1] = 4;
	add_to_map(xdf_map[1][0], 0, 0, 3);
	add_to_map(xdf_map[1][1], 0, 2, 4);
	add_to_map(xdf_map[1][2], 1, 3, 6);
	add_to_map(xdf_map[1][3], 0, 1, 2);
	add_to_map(xdf_map[1][4], 1, 1, 2);
	add_to_map(xdf_map[1][5], 0, 3, 6);
	add_to_map(xdf_map[1][6], 1, 0, 4);
	add_to_map(xdf_map[1][7], 1, 2, 3);

	// XDF 3.5" 2ED
	add_to_map(xdf_track0[2], 22, 37, 9);
	xdf_spt[2] = 4;
	add_to_map(xdf_map[2][0], 0, 0, 3);
	add_to_map(xdf_map[2][1], 0, 1, 4);
	add_to_map(xdf_map[2][2], 0, 2, 5);
	add_to_map(xdf_map[2][3], 0, 3, 7);
	add_to_map(xdf_map[2][4], 1, 0, 3);
	add_to_map(xdf_map[2][5], 1, 1, 4);
	add_to_map(xdf_map[2][6], 1, 2, 5);
	add_to_map(xdf_map[2][7], 1, 3, 7);

	// XXDF 3.5" 2HD
	add_to_map(xdf_track0[3], 12, 20, 4);
	xdf_spt[3] = 2;
	add_to_map(xdf_map[3][0], 0, 0, 5);
	add_to_map(xdf_map[3][1], 1, 1, 6);
	add_to_map(xdf_map[3][2], 0, 1, 6);
	add_to_map(xdf_map[3][3], 1, 0, 5);

	// XXDF 3.5" 2ED
	add_to_map(xdf_track0[4], 21, 39, 9);
	xdf_spt[4] = 2;
	add_to_map(xdf_map[4][0], 0, 0, 6);
	add_to_map(xdf_map[4][1], 1, 1, 7);
	add_to_map(xdf_map[4][2], 0, 1, 7);
	add_to_map(xdf_map[4][3], 1, 0, 6);

	xdf_maps_initialized = 1;
}

void read_xdf_track0(FILE *f, int d, int sfat, int se, int sg)
{
	unsigned int s;

#ifndef RELEASE_BUILD
	pclog("read_xdf_track0(%lu, %lu, %lu)\n", sfat, se, sg);
#endif

	for (s=0;s<(se+sg);s++)
	{
		fdd[d].disc[0][0][s] = fdd[d].trackbufs[0][0] + (s * 512);
		fdd[d].disc[1][0][s] = fdd[d].trackbufs[1][0] + (s * 512);
	}

	// Track 0
	for (s=0;s<sfat;s++)
	{
		set_sector_id(d, 0, 0, s, s + 129, 2);
	}
	set_sector_id(d, 0, 1, 0, 129, 2);
	for (s=0;s<7;s++)
	{
		set_sector_id(d, 0, 0, s + sfat, s + 1, 2);
	}

	read_raw_sectors(f, d, 0, 1, 0, 1, 0, sfat, 2, 0);
	read_raw_sectors(f, d, 0, 1, 1, 1, 0, 1, 2, 0);
	read_raw_sectors(f, d, 0, 1, 0, 1, sfat, 7, 2, 0);

	fseek(f, sg * 512, SEEK_CUR);

	// Set their sector ID's accordingly
	for (s=1;s<se;s++)
	{
		set_sector_id(d, 0, 1, s, s + 129, 2);
	}
	// Read 18 sectors from the second sector on Head 1
	read_raw_sectors(f, d, 0, 1, 1, 1, 1, se-1, 2, 0);

	// Mark sector ID accordingly
	set_sector_id(d, 0, 0, se-1, 8, 2);
	// This is for what will appear as 8th regular sector on Head 0
	read_raw_sectors(f, d, 0, 1, 0, 1, se-1, 1, 2, 0);

	fseek(f, sg * 512, SEEK_CUR);

	fdd[d].spt[0] = se;
}

void read_xdf(FILE *f, int d, int xdft)
{
	unsigned int h,t,t2,s,b,s2,p0,p1;

	// Track 0
	read_xdf_track0(f, d, xdf_track0[xdft][0], xdf_track0[xdft][1], xdf_track0[xdft][2]);

	if (ISSPECIAL)  fdd[d].TRACKS <<= 1;

	// Now, Tracks 1+
	for (t=1;t<fdd[d].TRACKS;t++)
	{
		if ((ISSPECIAL && !(t & 1)) || (!(ISSPECIAL)))
		{
			t2 = t;
			if (ISSPECIAL)  t2 >>= 1;

#ifndef RELEASE_BUILD
			pclog("Reading track %u...\n", t);
#endif
			for(s2=0;s2<(xdf_spt[xdft]*2);s2++)
			{
#ifndef RELEASE_BUILD
				pclog("xdf_map[%lu][%lu] = {%lu, %lu, %lu}\n", xdft, s2, xdf_map[xdft][s2][0], xdf_map[xdft][s2][1], xdf_map[xdft][s2][2]);
#endif
				set_sector_id(d, t2, xdf_map[xdft][s2][0], xdf_map[xdft][s2][1], xdf_map[xdft][s2][2] + 128, xdf_map[xdft][s2][2]);
			}
			/* Doing it twice so sector ID's are known when reading. */
			p0 = p1 = 0;
			for(s2=0;s2<(xdf_spt[xdft]);s2++)
			{
				fdd[d].disc[0][t][s2] = fdd[d].trackbufs[0][t] + p0;
				p0 += (128 << fdd[d].scid[0][t][s2][3]);
				fdd[d].disc[1][t][s2] = fdd[d].trackbufs[1][t] + p1;
				p1 += (128 << fdd[d].scid[1][t][s2][3]);
			}
			for(s2=0;s2<(xdf_spt[xdft]*2);s2++)
			{
				read_raw_sectors(f, d, t, 1, xdf_map[xdft][s2][0], 1, xdf_map[xdft][s2][1], 1, xdf_map[xdft][s2][2], 0);
			}
			fdd[d].spt[t] = xdf_spt[xdft];
		}
	}

	// Saving not implemented yet, so write-protect in order to prevent changes and corruption
	fdd[d].WP = 1;
}

void read_2m_track0(FILE *f, int d, int sfat, int se, int sg)
{
	unsigned int s;

	// Track 0
	for (s=0;s<sfat;s++)
	{
		set_sector_id_2m(d, 0, 0, s, s + 1, 2);
	}
	set_sector_id_2m(d, 0, 1, 0, 1, 2);
	for (s=0;s<7;s++)
	{
		set_sector_id(d, 0, 0, s + sfat, s + 1, 2);
	}

	read_raw_sectors(f, d, 0, 1, 0, 1, 0, sfat, 2, 0);
	read_raw_sectors(f, d, 0, 1, 1, 1, 0, 1, 2, 0);
	read_raw_sectors(f, d, 0, 1, 0, 1, sfat, 7, 2, 0);

	fseek(f, sg * 512, SEEK_CUR);

	// Set their sector ID's accordingly
	for (s=1;s<se;s++)
	{
		set_sector_id_2m(d, 0, 1, s, s + 1, 2);
	}
	// Read 18 sectors from the second sector on Head 1
	read_raw_sectors(f, d, 0, 1, 1, 1, 1, se-1, 2, 0);

	// Mark sector ID accordingly
	set_sector_id(d, 0, 0, se-1, 8, 2);
	// This is for what will appear as 8th regular sector on Head 0
	read_raw_sectors(f, d, 0, 1, 0, 1, se-1, 1, 2, 0);

	fseek(f, sg * 512, SEEK_CUR);

	fdd[d].spt[0] = se;
}

void read_2m(FILE *f, int d, int xdft)
{
	unsigned int h,t,t2,s,b,s2;

	// Track 0
	read_2m_track0(f, d, xdf_track0[xdft][0], xdf_track0[xdft][1], xdf_track0[xdft][2]);

	// Now, Tracks 1+
	for (t=1;t<fdd[d].TRACKS;t++)
	{
		if ((ISSPECIAL && !(t & 1)) || (!(ISSPECIAL)))
		{
			t2 = t;
			if (ISSPECIAL)  t2 >>= 1;
#ifndef RELEASE_BUILD
			pclog("Reading track %u...\n", t);
#endif
			for(s2=1;s2<(xdf_spt[xdft]*2);s2++)
			{
				set_sector_id_2m(d, t2, xdf_map[xdft][s2][0], xdf_map[xdft][s2][1] - 1, xdf_map[xdft][s2][1], xdf_map[xdft][s2][2]);
				read_raw_sectors(f, d, t, 1, xdf_map[xdft][s2][0], 1, xdf_map[xdft][s2][1], 1, xdf_map[xdft][s2][2], 0);
			}
		}
	}

	// Saving not implemented yet, so write-protect in order to prevent changes and corruption
	fdd[d].WP = 1;
}

/* This is needed for correctly loading disk images with early FAT boot sectors (ie. DOS 1.x stuff) */
int guess_geometry(FILE *f, int d)
{
	int result = 0;

	if ((fdd[d].SIDES < 1) || (fdd[d].SIDES > 2) || (fdd[d].BPS < 128) || (fdd[d].BPS > 2048))
	{
		// Early boot sector lacking the required data
#ifndef RELEASE_BUILD
		printf("Early boot sector lacking the required data, trying to guess\n");
#endif
		fseek(f, 0, SEEK_END);
		// 256 bytes per sector goes here for the sake of those Corona Data Systems MS-DOS 1.x 320 kB images
		fdd[d].BPS = 512;
		fdd[d].BPSCODE = 2;
		fdd[d].TOTAL = ftell(f) / fdd[d].BPS;
		if ((fdd[d].TOTAL * fdd[d].BPS) <= 250000)
		{
			fdd[d].SIDES = 1;
		}
		else
		{
			// To account for the possibility of 360 kB 3.5" single-sided floppy
			if (((fdd[d].TOTAL * fdd[d].BPS) <= 500000) && (!fdd[d].BIGFLOPPY))
			{
				fdd[d].SIDES = 1;
			}
			else
			{
				fdd[d].SIDES = 2;
			}
		}
		tempdiv = fdd[d].TOTAL / fdd[d].SIDES;
		// Anything bigger than 500 kB is going to have 77+ tracks
		if ((fdd[d].TOTAL * fdd[d].BPS) <= 500000)
		{
			if ((tempdiv % 30) == 0)
			{
				fdd[d].TRACKS = 30;
				fdd[d].SECTORS = (uint8_t) (fdd[d].TOTAL / (fdd[d].TRACKS * fdd[d].SIDES));
				fdd[d].MID = 0xfc + (fdd[d].SIDES - 1);
				if (fdd[d].SECTORS == 8)  fdd[d].MID = 0xfe + (fdd[d].SIDES - 1);
				result = 1;
			}
			if ((tempdiv % 35) == 0)
			{
				fdd[d].TRACKS = 35;
				fdd[d].SECTORS = (uint8_t) (fdd[d].TOTAL / (fdd[d].TRACKS * fdd[d].SIDES));
				fdd[d].MID = 0xfc + (fdd[d].SIDES - 1);
				if (fdd[d].SECTORS == 8)  fdd[d].MID = 0xfe + (fdd[d].SIDES - 1);
				result = 1;
			}
			else
			{
				if ((tempdiv % 40) == 0)
				{
					fdd[d].TRACKS = 40;
					fdd[d].SECTORS = (uint8_t) (fdd[d].TOTAL / (fdd[d].TRACKS * fdd[d].SIDES));
					fdd[d].MID = 0xfc + (fdd[d].SIDES - 1);
					if (fdd[d].SECTORS == 8)  fdd[d].MID = 0xfe + (fdd[d].SIDES - 1);
					result = 1;
				}
				else if ((tempdiv % 41) == 0)
				{
					fdd[d].TRACKS = 41;
					fdd[d].SECTORS = (uint8_t) (fdd[d].TOTAL / (fdd[d].TRACKS * fdd[d].SIDES));
					fdd[d].MID = 0xfc + (fdd[d].SIDES - 1);
					if (fdd[d].SECTORS == 8)  fdd[d].MID = 0xfe + (fdd[d].SIDES - 1);
					result = 1;
				}
				else if ((tempdiv % 42) == 0)
				{
					fdd[d].TRACKS = 42;
					fdd[d].SECTORS = (uint8_t) (fdd[d].TOTAL / (fdd[d].TRACKS * fdd[d].SIDES));
					fdd[d].MID = 0xfc + (fdd[d].SIDES - 1);
					if (fdd[d].SECTORS == 8)  fdd[d].MID = 0xfe + (fdd[d].SIDES - 1);
					result = 1;
				}
				else
				{
					result = 0;
#ifndef RELEASE_BUILD
					printf("Guessing failed\n");
					pclog("Not 30, 35, 40, 41, or 42 tracks\n");
#endif
				}
			}			
		}
		else
		{
			if ((tempdiv % 80) == 0)
			{
				fdd[d].TRACKS = 80;
				fdd[d].SECTORS = (uint8_t) (fdd[d].TOTAL / (fdd[d].TRACKS * fdd[d].SIDES));
				fdd[d].MID = 0xf0;
				if (fdd[d].SECTORS <= 15)  fdd[d].MID = 0xf9;
				result = 1;
			}
			else if ((tempdiv % 60) == 0)
			{
				fdd[d].TRACKS = 60;
				fdd[d].SECTORS = (uint8_t) (fdd[d].TOTAL / (fdd[d].TRACKS * fdd[d].SIDES));
				fdd[d].MID = 0xfe;
				result = 1;
			}
			else if ((tempdiv % 70) == 0)
			{
				fdd[d].TRACKS = 70;
				fdd[d].SECTORS = (uint8_t) (fdd[d].TOTAL / (fdd[d].TRACKS * fdd[d].SIDES));
				fdd[d].MID = 0xfe;
				result = 1;
			}
			else if ((tempdiv % 77) == 0)
			{
				fdd[d].TRACKS = 77;
				fdd[d].SECTORS = (uint8_t) (fdd[d].TOTAL / (fdd[d].TRACKS * fdd[d].SIDES));
				fdd[d].MID = 0xfe;
				result = 1;
			}
			else
			{
				if ((tempdiv % 81) == 0)
				{
					fdd[d].TRACKS = 81;
					fdd[d].SECTORS = (uint8_t) (fdd[d].TOTAL / (fdd[d].TRACKS * fdd[d].SIDES));
					fdd[d].MID = 0xf0;
					if (fdd[d].SECTORS <= 15)  fdd[d].MID = 0xf9;
					result = 1;
				}
				else if ((tempdiv % 82) == 0)
				{
					fdd[d].TRACKS = 82;
					fdd[d].SECTORS = (uint8_t) (fdd[d].TOTAL / (fdd[d].TRACKS * fdd[d].SIDES));
					fdd[d].MID = 0xf0;
					if (fdd[d].SECTORS <= 15)  fdd[d].MID = 0xf9;
					result = 1;
				}
				else if ((tempdiv % 83) == 0)
				{
					fdd[d].TRACKS = 83;
					fdd[d].SECTORS = (uint8_t) (fdd[d].TOTAL / (fdd[d].TRACKS * fdd[d].SIDES));
					fdd[d].MID = 0xf0;
					if (fdd[d].SECTORS <= 15)  fdd[d].MID = 0xf9;
					result = 1;
				}
				else if ((tempdiv % 84) == 0)
				{
					fdd[d].TRACKS = 84;
					fdd[d].SECTORS = (uint8_t) (fdd[d].TOTAL / (fdd[d].TRACKS * fdd[d].SIDES));
					fdd[d].MID = 0xf0;
					if (fdd[d].SECTORS <= 15)  fdd[d].MID = 0xf9;
					result = 1;
				}
				// Some drives actually exist with 85-track support
				else if ((tempdiv % 85) == 0)
				{
					fdd[d].TRACKS = 85;
					fdd[d].SECTORS = (uint8_t) (fdd[d].TOTAL / (fdd[d].TRACKS * fdd[d].SIDES));
					fdd[d].MID = 0xf0;
					if (fdd[d].SECTORS <= 15)  fdd[d].MID = 0xf9;
					result = 1;
				}
				else if ((tempdiv % 86) == 0)
				{
					fdd[d].TRACKS = 86;
					fdd[d].SECTORS = (uint8_t) (fdd[d].TOTAL / (fdd[d].TRACKS * fdd[d].SIDES));
					fdd[d].MID = 0xf0;
					if (fdd[d].SECTORS <= 15)  fdd[d].MID = 0xf9;
					result = 1;
				}
				else
				{
					result = 0;
#ifndef RELEASE_BUILD
					printf("Guessing failed\n");
					pclog("Not 77, 80, 81, 82, 83, 84, or 85 tracks\n");
#endif
				}
			}			
		}
#ifndef RELEASE_BUILD
       		printf("After guessing: Drive %c: has %i sectors, %i tracks, %i bytes per sector, %i shift, %i total sectors, %i sides, and class %i, and is %i bytes long\n",'A'+d,fdd[d].SECTORS,fdd[d].TRACKS,fdd[d].BPS,fdd[d].BPSCODE,fdd[d].TOTAL, fdd[d].SIDES, fdd[d].CLASS, ftell(f));
#endif
		fdd[d].XDF = 0;
	}

	return result;
}

void set_xdf(int d)
{
	fdd[d].XDF = 0;
	if ((fdd[d].SECTORS > 18) && (fdd[d].CLASS == CLASS_1600))  fdd[d].XDF = 1;
	if ((fdd[d].SECTORS > 21) && (fdd[d].CLASS == CLASS_2000))  fdd[d].XDF = 2;
	if ((fdd[d].SECTORS > 42) && (fdd[d].CLASS == CLASS_4000))  fdd[d].XDF = 3;
	if ((fdd[d].SECTORS > 23) && (fdd[d].CLASS == CLASS_2000))  fdd[d].XDF = 4;
	if ((fdd[d].SECTORS > 46) && (fdd[d].CLASS == CLASS_4000))  fdd[d].XDF = 5;
}

void set_class_and_xdf(int d)
{
	fdd[d].CLASS = get_class(fdd[d].TOTAL * fdd[d].BPS, fdd[d].SIDES, fdd[d].MID);
	set_xdf(d);
}

void read_sector_state(FILE *f, int d, int h, int t, int s)
{
	int b;
	fdd[d].sstat[h][t][s] = fgetc(f);
}

void read_normal_scid(FILE *f)
{
	int b;
	for(b = 0; b < 5; b++)
	{
		current_scid[b] = fgetc(f);
	}
}

void read_multiplexed_scid(FILE *f)
{
	int b;
	for(b = 0; b < 4; b++)
	{
		current_scid[b] = fgetc(f);
	}
	current_scid[4] = current_scid[3];
	current_scid[3] = get_n_from_hn(current_scid[1]);
	current_scid[1] = get_h_from_hn(current_scid[1]);
}

void read_sstates(FILE *f, int d)
{
	int h,t,s;

	for (h = 0; h < fdd[d].SIDES; h++)
	{
		for (t = 0; t < 1; t++)
		{
			for (s = 0; s < fdd[d].SECTORS; s++)
			{
				read_sector_state(f, d, h, t, s);
			}
		}
		for (t = 1; t < fdd[d].TRACKS; t++)
		{
			for (s = 0; s < fdd[d].ph.res0; s++)
			{
				read_sector_state(f, d, h, t, s);
			}
		}
	}
}

void read_scids(FILE *f, int d)
{
	int h,t,s,b;
	uint8_t shead[2] = {0, 0};
	uint16_t stotal = 0;

	h = 0;
	for (t = 0; t < fdd[d].TRACKS; t++)
	{
		do
		{
			if (fdd[d].IDTYPE == 2)
			{
				read_multiplexed_scid(f);
			}
			else
			{
				read_normal_scid(f);
			}

			// if (current_scid[3] == 8)  goto on_invalid_scid;

			// Now, determine the head
			h = current_scid[1] & 1;

			// Read current sector ID
			for (b = 0; b < 5; b++)
			{
				fdd[d].scid[h][t][shead[h]][b] = current_scid[b];
			}

			fdd[d].sequential_sectors_index[stotal][0] = h;
			fdd[d].sequential_sectors_index[stotal][1] = t;
			fdd[d].sequential_sectors_index[stotal][2] = shead[h];
			fdd[d].sequential_sectors_index[stotal][3] = current_scid[3];

			stotal++;

			shead[h]++;
		}
		while (current_scid[3] < 8);

		fdd[d].spt[t] = shead[0];
	}
}

void read_sector_info(FILE *f, int d)
{
	int h,t,s;

	if (fdd[d].IDTYPE == 1)
	{
		read_sstates(f, d);
	}
	else
	{
		read_scids(f, d);
	}
}

void pef_read_header(FILE *f, int d)
{
	fdd[d].ph.magic0 = fgetc(f);
	fdd[d].ph.magic1 = fgetc(f);
	fdd[d].ph.magic2 = fgetc(f);
	fdd[d].ph.magic3 = fgetc(f);
	fdd[d].ph.sides_bps = fgetc(f);
	fdd[d].ph.tracks = fgetc(f);
	fdd[d].ph.spt = fgetc(f);
	fdd[d].ph.mid = fgetc(f);
	fdd[d].ph.params0 = fgetc(f);
	fdd[d].ph.params1 = fgetc(f);
	fdd[d].ph.params2 = fgetc(f);
	fdd[d].ph.res0 = fgetc(f);
	fdd[d].ph.res1 = fgetc(f);
	fdd[d].ph.res2 = fgetc(f);
	fdd[d].ph.res3 = fgetc(f);
	fdd[d].ph.res4 = fgetc(f);
}

void pef_set_info(int d)
{
	fdd[d].SIDES = fdd[d].ph.sides_bps >> 7;
	fdd[d].TRACKS = fdd[d].ph.tracks;
	fdd[d].SECTORS = fdd[d].ph.spt;
	fdd[d].BPSCODE = fdd[d].ph.sides_bps & 7;
	fdd[d].BPS = 128 << fdd[d].BPSCODE;
	fdd[d].MID = fdd[d].ph.mid;
	fdd[d].TOTAL = fdd[d].SIDES * fdd[d].TRACKS * fdd[d].SECTORS;
	
	if (fdd[d].ph.params2 & 1)
	{
		fdd[d].CLASS = fdd[d].ph.params2 >> 5;
		fdd[d].XDF = (fdd[d].ph.params2 >> 1) & 0xf;
	}
	else
	{
		set_class_and_xdf(d);
	}
	if (fdd[d].ph.params1 & 1)
	{
		if (fdd[d].ph.params1 & 2)
		{
			fdd[d].IDTYPE = 1;
		}
		else
		{
			if (fdd[d].ph.params1 & 4)
			{
				if (fdd[d].ph.params1 & 8)
				{
					fdd[d].IDTYPE = 2;
				}
				else
				{
					fdd[d].IDTYPE = 1;
				}
			}
		}
		if (!(fdd[d].ph.params1 & 16))
		{
			fdd[d].RAWOFFS = -1;
		}
	}
	else
	{
		fdd[d].IDTYPE = 1;
	}
	fdd[d].WP = (((fdd[d].ph.params0 & 3) == 3) ? 1 : 0);
}

int pef_open_ext_raw_image(FILE **f, int d)
{
	char *temp;
	int result = 0;
	int i;

	for (i = 0; i < 4095; i++)
	{
		*(fdd[d].ph_raw_image_file + i) = 0;
	}

	fdd[d].ph_raw_image_temp[255] = 0;
	fread(fdd[d].ph_raw_image_temp, 255, 1, *f);

	temp = strrchr(fdd[d].image_file, '\\');
	if (temp == NULL)
	{
		strcpy(fdd[d].ph_raw_image_file, fdd[d].ph_raw_image_temp);
		goto open_ext_raw_image;
	}

	temp++;
	if (*temp == 0)  return result;

	temp--;
	*temp = 0;
	strcpy(fdd[d].ph_raw_image_file, fdd[d].image_file);
	*temp = '\\';

	temp = strrchr(fdd[d].ph_raw_image_file, '\\');
	temp++;
	for (i = 0; i < strlen(fdd[d].ph_raw_image_temp); i++)
	{
		*(temp + i) = *(fdd[d].ph_raw_image_temp + i);
	}

open_ext_raw_image:
	fclose(*f);
	*f = fopen(fdd[d].ph_raw_image_file, "rb");
	if (*f == NULL)  return result;

	result = 1;
	return result;
}

void fdi_read_header(FILE *f, int d)
{
	fread(&(fdd[d].fh.fdi_data_0), 1, 4, f); 
	fread(&(fdd[d].fh.fdi_data_1), 1, 4, f); 
	fread(&(fdd[d].fh.rawoffs), 1, 4, f); 
	fread(&(fdd[d].fh.rawsize), 1, 4, f); 
	fread(&(fdd[d].fh.bps), 1, 4, f); 
	fread(&(fdd[d].fh.spt), 1, 4, f); 
	fread(&(fdd[d].fh.sides), 1, 4, f); 
	fread(&(fdd[d].fh.tracks), 1, 4, f); 
}

void fdi_set_info(FILE *f, int d)
{
	uint32_t co = 0;

	fdd[d].SIDES = fdd[d].fh.sides;
	fdd[d].TRACKS = fdd[d].fh.tracks;
	fdd[d].SECTORS = (uint8_t) fdd[d].fh.spt;
	fdd[d].TOTAL = fdd[d].fh.rawsize;
	fdd[d].BPS = fdd[d].fh.bps;
	fdd[d].TOTAL /= fdd[d].BPS;
	fdd[d].BPSCODE = getshift(fdd[d].BPS);
	fdd[d].RAWOFFS = fdd[d].fh.rawoffs;

	co = ftell(f);

	fseek(f, fdd[d].RAWOFFS + 0x15, SEEK_SET);
	fdd[d].MID = getc(f);

	fseek(f, co, SEEK_SET);

	set_class_and_xdf(d);
}

int raw_set_info(FILE *f, int d)
{
	int gg = 1;

	fdd[d].BPS = 0;
	fdd[d].TOTAL = 0;
	fdd[d].SECTORS = 0;
	fdd[d].SIDES = 0;
	fdd[d].MID = 0;
	fseek(f,0xB,SEEK_SET);
	fread(&(fdd[d].BPS), 1, 2, f);
	fdd[d].BPSCODE = getshift(fdd[d].BPS);
	fseek(f,0x13,SEEK_SET);
	fread(&(fdd[d].TOTAL), 1, 2, f);
	fseek(f,0x15,SEEK_SET);
	fdd[d].MID = fgetc(f);
	fseek(f,0x18,SEEK_SET);
	fread(&(fdd[d].SECTORS), 1, 1, f);
	fseek(f,0x1A,SEEK_SET);
	fread(&(fdd[d].SIDES), 1, 1, f);
	fseek(f,0x26,SEEK_SET);
	if ((fdd[d].SIDES < 1) || (fdd[d].SIDES > 2) || (fdd[d].BPS < 128) || (fdd[d].BPS > 2048))
	{
		gg = guess_geometry(f, d);
#ifndef RELEASE_BUILD
		pclog("Guessing result: %u\n", gg);
#endif
		fdd[d].XDF = 0;
	}
	else
	{
		fdd[d].TRACKS = (uint8_t) (fdd[d].TOTAL / (fdd[d].SECTORS * fdd[d].SIDES));
		gg = 1;
	}
	set_class_and_xdf(d);
	fdd[d].RAWOFFS = 0;
	return gg;
}

int raw_image_fn_buffers_initialized = 0;

void init_raw_image_fn_buffers()
{
	uint8_t i = 0;
	for (i = 0; i < 1; i++)
	{
		if (fdd[i].image_file == NULL)  fdd[i].image_file = (char *) malloc(4095);
		if (fdd[i].ph_raw_image_temp == NULL)  fdd[i].ph_raw_image_temp = (char *) malloc(256);
		if (fdd[i].ph_raw_image_file == NULL)  fdd[i].ph_raw_image_file = (char *) malloc(4096);
	}

	int raw_image_fn_buffers_initialized = 1;
}

int is_2m(FILE *f, int d)
{
	uint32_t co = ftell(f);
	uint32_t magic = 0;
	fseek(f, fdd[d].RAWOFFS + 3, SEEK_SET);
	fread(&magic, 1, 4, f);
	fseek(f, co, SEEK_SET);
	switch(magic)
	{
		// 2M-S
		case 0x532D4D32:
			return 1;
			break;
		default:
			return 0;
			break;
	}
}

/* This is used for XDF and 2M formatting: consolidates all sector ID's > 128 bytes after one another.
   It's used to emulate the trick used on real floppies where 128-byte sectors are formatted and arbitrary
   sector sizes written. */
int is_nonzero_sector_size(uint8_t size)
{
	return ((size > 0) && (size < 8));
}

void find_next_big_scid(int st, int *c, int *h, int *r, int *n, int *sn)
{
	int s;

	*h = 255;
	for (s = st; s < 255; s++)
	{
		if (is_nonzero_sector_size(fdd[vfdd[fdc.drive]].scid[fdc.head[fdc.drive]][fdc.track[fdc.drive]][s][3]))
		{
			*c = (int) fdd[vfdd[fdc.drive]].scid[fdc.head[fdc.drive]][fdc.track[fdc.drive]][s][0];
			*h = (int) fdd[vfdd[fdc.drive]].scid[fdc.head[fdc.drive]][fdc.track[fdc.drive]][s][1];
			*r = (int) fdd[vfdd[fdc.drive]].scid[fdc.head[fdc.drive]][fdc.track[fdc.drive]][s][2];
			*n = (int) fdd[vfdd[fdc.drive]].scid[fdc.head[fdc.drive]][fdc.track[fdc.drive]][s][3];
			*sn = s;
			return;
		}
	}
}

void clear_sectors()
{
	int s = 0;
	int i = 0;

	for (s = 0; s < 255; s++)
	{
		for (i = 0; i < 4; i++)
		{
			fdd[vfdd[fdc.drive]].scid[fdc.head[fdc.drive]][fdc.track[fdc.drive]][s][i] = 0xff;
		}
		fdd[vfdd[fdc.drive]].scid[fdc.head[fdc.drive]][fdc.track[fdc.drive]][s][4] = 3;
	}
}

void consolidate_special_sectors()
{
	int lastsector = 0;
	int spt = 0;
	int last_scid[4] = {0, 0, 0, 0};
	int temp_smap[255][4];
	int s = 0;
	int sn = 0;
	int i = 0;
	int ssize = 0;

	s = 0;
	while (s <= 255)
	{
		/* find_next_big_scid(lastsector, &(last_scid[0]), &(last_scid[1]), &(last_scid[2]), &(last_scid[3]), &sn);
		if (h < 2)
		{
			// Such a sector has been found
			// Write the sector ID to ours
			for (i = 0; i < 4; i++)
			{
				temp_smap[s][i] = last_scid[i];
			}
			temp_smap[s][4] = 0xbf;
			if (s == 0)  temp_smap[s][4] |= 0x40;
			lastsector = sn + 1;
		} */
		ssize = fdd[vfdd[fdc.drive]].scid[fdc.head[fdc.drive]][fdc.track[fdc.drive]][s][3];
		if (ssize <= 7)
		{
			ssize = 1 << ssize;
			for (i = 0; i < 4; i++)
			{
				temp_smap[s][i] = fdd[vfdd[fdc.drive]].scid[fdc.head[fdc.drive]][fdc.track[fdc.drive]][s][i];
			}
			temp_smap[s][4] = 0xbf;
			if (s == 0)  temp_smap[s][4] |= 0x40;
			s += ssize;
		}
		else
		{
			s++;
		}
	}

	clear_sectors();

	for (s = 0; s < 255; s++)
	{
		for (i = 0; i < 4; i++)
		{
			fdd[vfdd[fdc.drive]].scid[fdc.head[fdc.drive]][fdc.track[fdc.drive]][s][i] = temp_smap[s][i];
		}
	}
}

int img_type(FILE *f, int d)
{
	uint32_t mag = 0;

	fseek(f, 0, SEEK_SET);

	fread(&mag, 4, 1, f);

	switch(mag)
	{
		case 0:
			return IMGT_FDI;
			break;
		case 0x6D654350:
			return IMGT_PEF;
			break;
		default:
			return IMGT_RAW;
			break;
	}
}

void read_raw_or_fdi(FILE *f, int d)
{
	fseek(f, fdd[d].RAWOFFS, SEEK_SET);
	if (fdd[d].XDF)
	{
		read_xdf(f, d, fdd[d].XDF - 1);
	}
	else
	{
		read_normal_floppy(f, d);
	}
}

void clear_sector_states(int d)
{
	unsigned int h,t,s,b;

	for (h = 0; h < 2; h++)
	{
		for (t = 0; t < 86; t++)
		{
			for (s = 0; s < 255; s++)
			{
				fdd[d].scid[h][t][s][4] = 3;
			}
		}
	}

	for (s = 0; s < 21930; s++)
	{
		// memset(fdd[d].sequential_sectors_index[s], 255, 4);
	}
}

int floppy_drive_enabled[2] = {0, 0};

int allocated = 0;

uint8_t is_48tpi(int d)
{
	return (fdd[d].BIGFLOPPY && !fdd[d].DENSITY && !fdd[d].THREEMODE);
}

int is_48tpi_old = 0;

void configure_from_int(int d, int val)
{
	if (val == 16)
	{
		fdd[d].floppy_drive_enabled = 0;
		return;
	}
	else
	{
		fdd[d].floppy_drive_enabled = 1;
	}

	fdd[d].BIGFLOPPY = ((val) & 8) >> 3;
	fdd[d].DENSITY = ((val) & 6) >> 1;
	fdd[d].THREEMODE = ((val) & 1);
}

void reconfigure_from_int(int d, int val)
{
	is_48tpi_old = is_48tpi(d);
	savedisc(d);
	fdd[d].track = 0;
	fdd[d].trk0 = 1;
	configure_from_int(d, val);
	if ((is_48tpi(d) ^ is_48tpi_old) && (!fdd[d].driveempty) && (fdd[d].CLASS < CLASS_800) && (fdd[d].CLASS != -1))
	{
		freesectors(d);
		fdd[d].discmodified = 0;
		loaddisc(d, discfns[d]);
	}
}

int int_from_config(int d)
{
	if (!fdd[d].floppy_drive_enabled)
	{
		return 16;
	}

	int temp = 0;
	temp |= fdd[d].BIGFLOPPY;
	temp <<= 2;
	temp |= fdd[d].DENSITY;
	temp <<= 1;
	temp |= fdd[d].THREEMODE;
	return temp;
}

void floppy_load_image(int d, char *fn)
{
        FILE *f;
	int i = 0;

	if (strlen(fn) == 0)
	{
#ifndef RELEASE_BUILD
		pclog("%c: No file specified, aborting load...\n", 0x41 + d);
#endif
		ejectdisc(d);
		fdd[d].WP = 1;
		return;
	}

	if (!xdf_maps_initialized)  initialize_xdf_maps();
	if (!raw_image_fn_buffers_initialized)  init_raw_image_fn_buffers();

	if (!fdd[d].floppy_drive_enabled)
	{
#ifndef RELEASE_BUILD
		pclog("Drive %c: is disabled, stopping file load\n", 0x41 + d);
#endif
		goto drive_disabled;
	}

	clear_sector_states(d);

	freesectors(d);

	fdc.deldata = 0;

	fdd[d].IMGTYPE = IMGT_NONE;

	f = fopen(fn,"rb");
	if (f == NULL)
	{
#ifndef RELEASE_BUILD
		pclog("File is null\n");
#endif
		goto drive_disabled;
fli_on_error:
#ifndef RELEASE_BUILD
		pclog("An error during loading raw image, ejecting image\n");
#endif
drive_disabled:
		ejectdisc(d);
		fdd[d].WP = 1;
		fclose(f);
		return;
	}

	fdd[d].driveempty = 0;

	fdd[d].IMGTYPE = img_type(f, d);

	switch(fdd[d].IMGTYPE)
	{
		case IMGT_FDI:
			fdi_read_header(f, d);
			fdi_set_info(f, d);
			fseek(f, fdd[d].RAWOFFS, SEEK_SET);
			if (fdd[d].SECTORS == 0)
			{
#ifndef RELEASE_BUILD
				pclog("No sectors, aborting\n");
#endif
				goto drive_disabled;
			}
			read_raw_or_fdi(f, d);
			fdd[d].WP = 0;
			break;
		case IMGT_PEF:
			pef_read_header(f, d);
			pef_set_info(d);
			read_sector_info(f, d);
			if ((fdd[d].ph.params1 & 0x11) == 0x11)
			{
				i = pef_open_ext_raw_image(&f, d);
				if (!i)
				{
					fclose(f);
					goto fli_on_error;
				}
			}
			if (fdd[d].IDTYPE == 1)
			{
				if (fdd[d].SECTORS == 0)
				{
#ifndef RELEASE_BUILD
					pclog("No sectors, aborting\n");
#endif
					goto drive_disabled;
				}
				read_raw_or_fdi(f, d);
				pef_set_spt(d);
			}
			else
			{
				pef_read_advanced_floppy(f, d);
			}
			break;
		default:
			i = raw_set_info(f, d);
			if (!i)
			{
				fclose(f);
				goto fli_on_error;
			}
			if (fdd[d].SECTORS == 0)
			{
#ifndef RELEASE_BUILD
				pclog("No sectors, aborting\n");
#endif
				goto drive_disabled;
			}
			read_raw_or_fdi(f, d);
			if (!fdd[d].XDF)  fdd[d].WP = 0;
			break;
	}
#ifndef RELEASE_BUILD
        printf("Drive %c: %i sect./track, %i tracks (step %i), %i B/sect., %i shift, %i total sectors, %i sides, class %i, and image type %i\n",'A'+d,fdd[d].SECTORS,fdd[d].TRACKS,((ISSPECIAL) ? 2 : 1),fdd[d].BPS,fdd[d].BPSCODE,fdd[d].TOTAL, fdd[d].SIDES, fdd[d].CLASS, fdd[d].IMGTYPE);
#endif
	/* For time being, to test reading, after that, saving will be implemented. */
	// WP[d] = 1;

        fclose(f);
	// free(f);
        fdd[d].discmodified=0;
        strcpy(discfns[d],fn);
        fdd[d].discchanged=1;
	fdc.format_started[vfdd[d]] = 0;
	fdc.track[vfdd[d]] = 0;
	fdc.head[vfdd[d]] = 0;
	fdc.sector[vfdd[d]] = 1;
	fdc.pos[vfdd[d]] = 0;
	fdc.gotdata[vfdd[d]] = 0;
}

#define COLORBOOK_CASE	((d == 1) && ((romset == ROM_COLORBOOK) || !drv2en))
#define NO_DRIVE	(COLORBOOK_CASE || !fdd[d].floppy_drive_enabled)

void fdd_seek(int d, uint8_t n, uint8_t dir)
{
	int i = 0;
	int move = -1;
	int max = 85;
	if (dir)  move = 1;
	if (is_48tpi(d))  max = 42;

	if (NO_DRIVE)  fdd[d].trk0 = 0;

	if (!n)  return;

	if (!dir && !fdd[d].track)  return;
	if (dir && (fdd[d].track == max))  return;

	for (i = 0; i < n; i++)
	{
		fdd[d].track += move;
		if (!fdd[d].track)
		{
			if (!(NO_DRIVE))  fdd[d].trk0 = 1;
			break;
		}
		else
		{
			fdd[d].trk0 = 0;
			if (fdd[d].track == max)
			{
				break;
			}
		}
	}
}

void fdd_init()
{
	int i = 0;

	for (i = 0; i < 2; i++)
	{
		fdd[i].FDIDATA = 0;
		fdd[i].MID = 0;
		fdd[i].TRACKS = 80;
		fdd[i].SECTORS = 9;
		fdd[i].SIDES = 2;
		fdd[i].BPS = 512;
		fdd[i].BPSCODE = 2;
		fdd[i].WP = 0;
		fdd[i].HSIZE = 0;
		fdd[i].MFM = 0;
		fdd[i].THREEMODE = 1;
		fdd[i].BIGFLOPPY = 0;
		fdd[i].DENSITY = 2;
		fdd[i].TOTAL = 65535;
		fdd[i].IMGTYPE = IMGT_NONE;
		fdd[i].RAWOFFS = 0;
		fdd[i].sstates = 0;
		fdd[i].XDF = 0;
		fdd[i].CLASS = 0;
		fdd[i].LITE = 0;
		fdd[i].IDTYPE = 0;
		fdd[i].rws = 0;
		fdd[i].sectors_formatted = 0;
		fdd[i].discmodified = 0;
		fdd[i].image_file = NULL;
		fdd[i].track = 0;
		fdd[i].trk0 = 1;
	}
}
