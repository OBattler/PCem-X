#include <stdio.h>
#include <string.h>

#include "ibm.h"

#include "dma.h"
#include "io.h"
#include "pic.h"
#include "timer.h"

/*FDC*/
typedef struct FDC
{
        uint8_t dor,stat,command,dat,st0,st1,st2;
        int head[2],track[256],sector[2],drive,lastdrive;
        int pos[2];
        uint8_t params[256];
        uint8_t res[256];
        int pnum,ptot;
        int rate;
        uint8_t specify[256];
        int eot[256];
        int lock;
        int perp;
        uint8_t config, pretrk;
        int abort;
	int rconfig;
	int ccr;
	uint8_t cr[16][272];
	uint8_t ring[2][4];
	int iocycle;
	int ldev;
	int dsr;
	int densel;
	int deldata;
	int oldrate;
	int tdr;
	int format_started[2];
	int relative;
	int direction;
	int dma;
	int isseek;
	int iscmd7;
	int wrongrate;
	int fillbyte[2];
	int fdmaread[2];
	int fifo;
	int tfifo;
	uint8_t fifobuf[16];
	int fifobufpos;
	uint8_t scanbuf[2][1024];
	int scanpos[2];
	int gotdata[2];
        
        int pcjr;
        
        int watchdog_timer;
        int watchdog_count;
} FDC;

static FDC fdc;

void fdc_poll();
int timetolive;
int TRACKS[2] = {80, 80};
int SECTORS[2]={9,9};
int SIDES[2]={2,2};
int BPS[2]={512,512};
int BPSCODE[2]={2,2};
char MAGIC[4]={0,0,0,0};
int WP[2]={0,0};
int HSIZE[2]={0, 0};
int MFM[2]={1,1};
int THREEMODE[2]={1,1};
int BIGFLOPPY[2]={0,0};		// 0 = 3.5", 1 = 5.25"
// 0 = low, 1 = high, 2 = extended
int DENSITY[2]={2,2};
int TOTAL[2]={65535,65535};
// So far: 0 = none, 1 = raw, 2 = PCem image, 3 = FDI
int IMGTYPE[2]={0,0};
int RAWOFFS[2]={0,0};
// 0 = Intel 82078, 1 = SMSC FDC37N869, 2 = SMSC FDC37M81X, 3 = ITE IT8712F, 4 = SMSC LPC47B272, 5 = SMSC FDC37N958FR, 6 = NEC µPD765A/B
int fdcmodel = 0;
//#define SECTORS 9
int output;
int lastbyte=0;
int sstates[2]={0,0};
int OLDSECTORS[2]={9,9};
int OLDBPS[2]={512,512};
int OLDBPSCODE[2]={2,2};
int OLDSIDES[2]={2,2};
int olddiscrate[2];
int vectorworkaround=0;

/*	Preliminary map of the header of PCem image format:

	0x00000000	[dword]		Magic ("PCem")
	0x00000004	[byte]		Sides
	0x00000005	[byte]		Tracks
	0x00000006	[byte]		Sectors per Track
	0x00000007	[byte]		Bytes per Sector (0 = 128)
	0x00000008	[byte]		Write protected? (0 = no, 1 = yes)
	0x0000000A-F	[7 bytes]	Reserved
	0x00000010	[H*T*SPT]	Sector states
	0x00008000	[H*T*SPT*BPS]	Raw image data

	Sector status flag:
	Bits 6, 7 - only for first sector of each track: 11 = Data, 10 = Deleted data, 0x = Invalid;
	Bit 5 - only for first sector of each track: 1 = ID, 0 = No ID;
	Bit 2 - Data field status: 1 = OK, 0 = Data Error;
	Bit 1 - ID field status: 1 = Ok, 0 = Data Error.

	Padded so header is exactly 32768 bytes.


	Supported floppy drive configurations:

	5.25"?		Density		3-Mode		Description
	-----------------------------------------------------------
	1		0		0		5.25" 360 kB DD drive
	1		0		1		5.25" 720 kB QD drive
	1		1		0		5.25" 1.2 MB HD drive, only 360 rpm
	1		1		1		5.25" 1.2 MB HD drive, 300 and 360 rpm
	1		2		0		5.25" 2.4 MB HD drive, only 360 rpm
	1		2		1		5.25" 2.4 MB HD drive, 300 and 360 rpm
	1		3		Any		Invalid
	0		0		Any		3.5" 720 kB DD drive
	0		1		0		3.5" 720 kB/1.44 MB HD drive
	0		1		1		3.5" 720 kB/1.25 MB/1.44 MB 3-mode HD drive
	0		2		Any		3.5" 720 kB/1.44 MB/2.88 MB ED drive
	0		3		0		Invalid
	0		3		1		None

	Bit field:
	7	6	5	4	3	2	1	0
					5.25	Dens.0	Dens.1	3-Mode

	The combo box will contain: 0, 2, 3, 4, 8, 9, 10, 11


	Floppy rates (automatically assigned for all formats):

	5.25" 180K SD, 360K DD, 720K QD - 300 prm, 125 kbps FM/250 kbps MFM (rate 2)
	360K DD, 720K QD - 360 prm, 300 kbps MFM (rate 1)
	5.25" 1.2M HD - 360 rpm, 500 kbps MFM (rate 0)
	5.25" 2.4M ED - 360 rpm?, 1 Mbps? MFM (rate 6)
	3.5" 720k - 300 rpm, 250 kbps MFM (rate 2)
	3.5" 1.2M - 300 rpm, 500 kbps MFM (rate 0, using 4 to handle it correctly) (CR[5] bits 3, 4 must be 1)
	3.5" 1.25M - 300 rpm, 500 kbps MFM (rate 0, using 4 to handle it correctly) (CR[5] bits 3, 4 must be 1)
	3.5" 1.44M - 300 rpm, 500 kbps MFM (rate 0) (CR[5] bits 3, 4 must be 0)
	3.5" 2.88M - 300 rpm, 1 Mbps MFM (rate 3)

	Note that the 1.2M and 1.25M (ie. 3-mode) 5.25" floppies are not supported on 720k or 2.88 MB drives:
	If the BIOS sees the drive is not 1.44 MB, it won't bother doing the Mode 3 check at all.

	Also 5.25" 1.25M floppies are not supported by the FDC driver (but more testing is needed).
	Well, the size per se is supported, but 1024-byte sectors seem not to be.

	Basically: 5.25" single density are only valid for rate 1;
	5.25" high density are only valid for rate 0 - assign rate 4 for special treatment on 3.5";
	5.25" double density are valid for both rate 1 and 2;
	3.5" double density are valid for both rate 1 and 2;
	3.5" high density are only valid for rate 0 - rate 5 assigned to > 1.6 MB images to make sure they are not accepted as valid on 5.25";
	3.5" extended density are only valid for rate 3.	*/

uint8_t disc[2][2][255][50][1024];
uint8_t sstat[2][2][255][50];
uint8_t disc_3f7;

int discchanged[2];
int discmodified[2];
int discrate[2];

uint8_t OLD_BPS = 0;
uint8_t OLD_SPC = 0;
uint8_t OLD_C = 0;
uint8_t OLD_H = 0;
uint8_t OLD_R = 0;
uint8_t OLD_N = 0;
uint8_t sectors_formatted[2] = {0, 0};

uint8_t flag = 0;
int curoffset = 0;

int tempdiv = 0;

void configure_from_int(int d, int val)
{
	BIGFLOPPY[d] = ((val) & 8) >> 3;
	DENSITY[d] = ((val) & 6) >> 1;
	THREEMODE[d] = ((val) & 1);
}

int int_from_config(int d)
{
	int temp = 0;
	temp |= BIGFLOPPY[d];
	temp <<= 2;
	temp |= DENSITY[d];
	temp <<= 1;
	temp |= THREEMODE[d];
	return temp;
}

int getshift(int val)
{
	int i = 0;
	for (i = 0; i <= 7; i++)
	{
		if ((128 << i) == val)  return i;
	}
	return 8;
}

int setdiscrate(int d)
{
	discrate[d] = 2;

	if (TOTAL[d] * BPS[d] > (1000 * 1024))  discrate[d] = 4;
	if (TOTAL[d] * BPS[d] >= 1474560)  discrate[d] = 0;
	if (TOTAL[d] * BPS[d] > (1600 * 1024))  discrate[d] = 5;	// Not valid for 5.25"

	if (TOTAL[d] * BPS[d] > (2000 * 1024))  discrate[d] = 6;	// 5.25" 2.4 MB
	if (TOTAL[d] * BPS[d] > (3200 * 1024))  discrate[d] = 3;

	if (TOTAL[d] * BPS[d] > (4000 * 1024))  discrate[d] = 7;	// Invalid

	if ((TOTAL[d] * BPS[d] <= (500 * 1024)) && (TOTAL[d] * BPS[d] > (250 * 1024)) && (SIDES[d] == 1))  discrate[d] = 8;

	if (TOTAL[d] == 0)  discrate[d] = 7;

	if (BPS[d] > 16384)  discrate[d] = 7;

	if (TRACKS[d] > 255)  discrate[d] = 7;

	if (SECTORS[d] > 50)  discrate[d] = 7;

	if ((SIDES[d] != 1) && (SIDES[d] != 2))  discrate[d] = 7;

	if ((BPS[d] < 512) && (discrate[d] & 3 != 2))  discrate[d] = 7;
	if ((BPS[d] > 512) && (discrate[d] & 3 == 2))  discrate[d] = 7;

	if ((BPS[d] > 512) && BIGFLOPPY[d])  discrate[d] = 7;

	fdc.track[d] = 0;

	if (((discrate[d] == 3) || (discrate[d] == 5)) && BIGFLOPPY[d])  discrate[d] = 7;
	// 360 kB DD single-sided 3.5" floppies exist
	// if ((SIDES[d] == 1) && !BIGFLOPPY[d])  discrate[d] = 7;
	// WTF, rejecting 1-side floppies on a 360k/720k 5.25" drive?! Definitely not...
	if ((SIDES[d] == 1) && BIGFLOPPY[d] && !THREEMODE[d] && DENSITY[d])  discrate[d] = 7;
	if ((TRACKS[d] < 70) && !BIGFLOPPY[d])  discrate[d] = 7;
	if ((TRACKS[d] >= 70) && BIGFLOPPY[d] && !DENSITY[d] && !THREEMODE[d])  discrate[d] = 7;

	return;
}

int samediskclass(int d, int c, int s, int n)
{
	int b = 128 << n;
	int total = c * s * SIDES[d];

	int rate = 2;

	pclog("samediskclass(%02X, %02X, %02X, %02X)\n", d, c, s, n);

	if (total * b > (1000 * 1024))  rate = 4;
	if (total * b >= 1474560)  rate = 0;
	if (total * b > (1600 * 1024))  rate = 5;	// Not valid for 5.25"

	if (total * b > (2000 * 1024))  rate = 6;	// 5.25" 2.4 MB
	if (total * b > (3200 * 1024))  rate = 3;

	if (total * b > (4000 * 1024))  rate = 7;	// Invalid

	if ((total * b <= (500 * 1024)) && (total * b > (250 * 1024)) && (SIDES[d] == 1))  rate = 8;

	if (total == 0)  rate = 7;

	if (n > 7)  rate = 7;

	if (c > 255)  rate = 7;

	if (s > 50)  rate = 7;

	if ((SIDES[d] != 1) && (SIDES[d] != 2))  rate = 7;

	if ((b < 512) && (rate != 2))  rate = 7;
	if ((b > 512) && (rate == 2))  rate = 7;

	if ((b > 512) && BIGFLOPPY[d])  rate = 7;

	// On 5.25", rates 0 and 4 should be treated as equal
	// On 5.25", rates 5 and 6 should be treated as equal
	// On 3.5", rates 0 and 5 should be treated as equal
	if (rate != discrate[d])
	{
		if (BIGFLOPPY[d])
		{
			if ((rate == 4) && (discrate[d] == 0))
			{
				rate = 0;
			}
			else if ((rate == 0) && (discrate[d] == 4))
			{
				rate = 0;
			}
			if ((rate == 6) && (discrate[d] == 5))
			{
				rate = 5;
			}
			else if ((rate == 5) && (discrate[d] == 6))
			{
				rate = 5;
			}
			else
			{
				pclog("Diffrate %02X %02X\n", rate, discrate[d]);
				rate = 7;
			}
		}
		else
		{
			if ((rate == 5) && (discrate[d] == 0))
			{
				rate = 0;
			}
			else if ((rate == 0) && (discrate[d] == 5))
			{
				rate = 0;
			}
			else
			{
				pclog("Diffrate %02X %02X\n", rate, discrate[d]);
				rate = 7;
			}
		}
	}

	if (discrate[d] == 7)  rate = 7;

	if (rate == 7)  return 0;
	return 1;
}

void drivetypeid(int d)
{
	int b = d << 1;
	int t = 0;
	if (BIGFLOPPY[d] && (DENSITY[d] == 0))  t = 1;
	if (DENSITY[d] == 0)  t = 3;
	if (discrate[d] == 0)  t = 2;
	if (discrate[d] >= 3)  t = 2;
	if (discrate[d] == 7)  t = 3;
	if (driveempty[d])  t = 3;
	t <<= b;
	if ((fdcmodel == 2) || (fdcmodel == 4) || (fdcmodel == 5))
	{
		if (!b)  fdc.cr[0][0xF2] &= 0xFC;
		if (b)  fdc.cr[0][0xF2] &= 0xF3;
		fdc.cr[0][0xF2] |= t;
		fdc.cr[0][0xF4 + d] &= 0xFC;
		fdc.cr[0][0xF4 + d] |= (t >> b);
	}
	else if (fdcmodel == 1)
	{
		if (!b)  fdc.cr[0][6] &= 0xFC;
		if (b)  fdc.cr[0][6] &= 0xF3;
		fdc.cr[0][6] |= t;
	}
}

void initsectors(int d)
{
	int t = 0;
	int h = 0;
	int s = 0;
	int b = 0;

        for (t=0;t<255;t++)
        {
                for (h=0;h<2;h++)
                {
                        for (s=0;s<50;s++)
                        {
                                for (b=0;b<1024;b++)
                                {
					disc[d][h][t][s][b]=0xF6;
                                }
                        }
                }
        }
}

void defaultsstates(int d)
{
	int t = 0;
	int h = 0;
	int s = 0;
	if (discrate[d] != 7)
	{
       		for (t=0;t<TRACKS[d];t++)
       		{
               		for (h=0;h<SIDES[d];h++)
               		{
                       		for (s=0;s<SECTORS[d];s++)
                       		{
                                       	// sstat[d][h][t][s]=3;
                                       	sstat[d][h][t][s]=0xE3;
					// if(s == 0)  sstat[d][h][t][s]|=0xE0;
                       		}
               		}
       		}
	}
}

void resetscanbuf(int d)
{
	int i = 0;
	for (i = 0; i < 1024; i++)
	{
		fdc.scanbuf[d][i] = 0;
	}
}

void ejectdisc(int d)
{
	initsectors(d);
        discfns[d][0]=0;
        SECTORS[d]=9; SIDES[d]=1;
        driveempty[d]=1; // discrate[d]=4;
	if (!BIGFLOPPY[d])
	{
		if (!DENSITY[d])
		{
			discrate[d]=2;
		}
		else
		{
			discrate[d]=0;
		}
	}
	else
	{
		if (DENSITY[d] == 0)
		{
			discrate[d]=2;
		}
		else if (DENSITY[d] == 1)
		{
			discrate[d]=0;
		}
		else
		{
			discrate[d]=3;
		}
	}
	drivetypeid(d);
	fdc.format_started[0] = 0;
	fdc.format_started[1] = 0;
	fdc.track[d] = 0;
	fdc.head[d] = 0;
	fdc.sector[d] = 1;
	fdc.pos[d] = 0;
	fdc.deldata = 0;
	resetscanbuf(d);
	fdc.scanpos[d] = 0;
	fdc.gotdata[d] = 0;
}

void loaddisc(int d, char *fn)
{
        FILE *f=fopen(fn,"rb");
        int h,t,s,b;
	fdc.format_started[d] = 0;
        if (!f || ((DENSITY[d] == 3) && !BIGFLOPPY[d] && THREEMODE[d]))
        {
		ejectdisc(d);
                return;
        }
	fdc.deldata = 0;
	initsectors(d);
        driveempty[d]=0;
        SIDES[d]=2;
	// Bytes per sector
	BPS[d] = 0;
	pclog("Sector size: %08X\n", fdc.params[4]);

        fseek(f,0,SEEK_SET);
	pclog("Is it magic?\n");
	MAGIC[0] = getc(f);
	MAGIC[1] = getc(f);
	MAGIC[2] = getc(f);
	MAGIC[3] = getc(f);
	pclog("Ha ha ha ha, it's magic!\n");
	HSIZE[d] = 0;
	if ((MAGIC[0] == 'P') && (MAGIC[1] == 'C') && (MAGIC[2] == 'e') && (MAGIC[3] == 'm'))
	{
		pclog("Magic bytes found, PCem-format image!\n");
		IMGTYPE[d] = 2;
		HSIZE[d] = 32768;
		SIDES[d] = getc(f);
		TRACKS[d] = getc(f);
		SECTORS[d] = getc(f);
		TOTAL[d] = TRACKS[d] * SIDES[d] * SECTORS[d];
		BPS[d] = getc(f);
		BPSCODE[d] = BPS[d];
		BPS[d] = 128 << BPS[d];
		WP[d] = getc(f);
		setdiscrate(d);
		if (discrate[d] != 7)
		{
			fseek(f,16,SEEK_SET);
        		for (t=0;t<TRACKS[d];t++)
        		{
                		for (h=0;h<SIDES[d];h++)
                		{
                        		for (s=0;s<SECTORS[d];s++)
                        		{
                                        	sstat[d][h][t][s]=getc(f);
						// if(s != 0)  sstat[d][h][t][s]&=3;
                        		}
                		}
        		}
			RAWOFFS[d] = 32768;
		}
	}
	else if ((MAGIC[0] == 0) && (MAGIC[1] == 0) && (MAGIC[2] == 0) && (MAGIC[3] == 0))
	{
		// FDI
		pclog("FDI image!\n");
		IMGTYPE[d] = 3;
		HSIZE[d] = 32768;
		fseek(f, 0xC, SEEK_SET);
		fread(&RAWOFFS[d], 4, 1, f);
		fseek(f, 0x10, SEEK_SET);
		BPS[d] = getc(f);
		BPSCODE[d] = getshift(BPS[d]);
		fseek(f, 0x14, SEEK_SET);
		SECTORS[d] = getc(f);
		fseek(f, 0x18, SEEK_SET);
		SIDES[d] = getc(f);
		fseek(f, 0x1C, SEEK_SET);
		TRACKS[d] = getc(f);
		TOTAL[d] = TRACKS[d] * SIDES[d] * SECTORS[d];
		WP[d] = 0;
		setdiscrate(d);
		defaultsstates(d);
	}
	else
	{
		IMGTYPE[d] = 0;
		BPS[d] = 0;
		TOTAL[d] = 0;
		SECTORS[d] = 0;
		SIDES[d] = 0;
		fseek(f,0xB,SEEK_SET);
		fread(&(BPS[d]), 1, 2, f);
		BPSCODE[d] = getshift(BPS[d]);
		fseek(f,0x13,SEEK_SET);
		fread(&(TOTAL[d]), 1, 2, f);
		fseek(f,0x18,SEEK_SET);
		fread(&(SECTORS[d]), 1, 2, f);
		fseek(f,0x1A,SEEK_SET);
		fread(&(SIDES[d]), 1, 2, f);
		if ((SIDES[d] < 1) || (SIDES[d] > 2) || (BPS[d] < 128) || (BPS[d] > 2048))
		{
			// Early boot sector lacking the required data
			printf("Early boot sector lacking the required data, trying to guess\n");
			fseek(f, 0, SEEK_END);
			TOTAL[d] = ftell(f) / 512;
			BPS[d] = 512;
			BPSCODE[d] = 2;
			if ((TOTAL[d] * BPS[d]) < (250*1024))
			{
				SIDES[d] = 1;
			}
			else
			{
				// To account for the possibility of 360 kB 3.5" single-sided floppy
				if (((TOTAL[d] * BPS[d]) < (500*1024)) && (!BIGFLOPPY[d]))
				{
					SIDES[d] = 1;
				}
				else
				{
					SIDES[d] = 2;
				}
			}
			tempdiv = TOTAL[d] / SIDES[d];
			// Anything bigger than 500 kB is going to have 77+ tracks
			if ((TOTAL[d] * BPS[d]) < (500*1024))
			{
				if ((tempdiv % 35) == 0)
				{
					TRACKS[d] = 35;
					SECTORS[d] = TOTAL[d] / (TRACKS[d] * SIDES[d]);
				}
				else
				{
					if ((tempdiv % 40) == 0)
					{
						TRACKS[d] = 40;
						SECTORS[d] = TOTAL[d] / (TRACKS[d] * SIDES[d]);
					}
					else
					{
						fclose(f);
						ejectdisc(d);
						pclog("Not either 35 nor 40 tracks\n");
						return;
					}
				}			
			}
			else
			{
				if ((tempdiv % 77) == 0)
				{
					TRACKS[d] = 77;
					SECTORS[d] = TOTAL[d] / (TRACKS[d] * SIDES[d]);
				}
				else
				{
					if ((tempdiv % 80) == 0)
					{
						TRACKS[d] = 80;
						SECTORS[d] = TOTAL[d] / (TRACKS[d] * SIDES[d]);
					}
					else if ((tempdiv % 81) == 0)
					{
						TRACKS[d] = 81;
						SECTORS[d] = TOTAL[d] / (TRACKS[d] * SIDES[d]);
					}
					else if ((tempdiv % 82) == 0)
					{
						TRACKS[d] = 82;
						SECTORS[d] = TOTAL[d] / (TRACKS[d] * SIDES[d]);
					}
					else if ((tempdiv % 83) == 0)
					{
						TRACKS[d] = 83;
						SECTORS[d] = TOTAL[d] / (TRACKS[d] * SIDES[d]);
					}
					else
					{
						printf("Guessing failed\n");
						fclose(f);
						ejectdisc(d);
						pclog("Not 77, 80, 81, 82, or 83 tracks\n");
						return;
					}
				}			
			}
        		printf("After guessing: Drive %c: has %i sectors, %i tracks, %i bytes per sector, %i shift, %i total sectors, %i sides, and rate %i, and is %i bytes long\n",'A'+d,SECTORS[d],TRACKS[d],BPS[d],BPSCODE[d],TOTAL[d], SIDES[d], discrate[d], ftell(f));
		}
		else
		{
			TRACKS[d] = TOTAL[d] / (SECTORS[d] * SIDES[d]);
		}
        	fseek(f,-1,SEEK_END);
		HSIZE[d] = 0;
		WP[d] = 0;
		RAWOFFS[d] = 0;
		setdiscrate(d);
		defaultsstates(d);
	}
        if (discrate[d] == 7)
        {
		fclose(f);
		ejectdisc(d);
                return;
        }
	fseek(f, RAWOFFS[d], SEEK_SET);
        // printf("Drive %c: has %i sectors and %i sides and is %i bytes long\n",'A'+d,SECTORS[0],SIDES[0],ftell(f));
        printf("Drive %c: has %i sectors, %i tracks, %i bytes per sector, %i shift, %i total sectors, %i sides, and rate %i, and is %i bytes long\n",'A'+d,SECTORS[d],TRACKS[d],BPS[d],BPSCODE[d],TOTAL[d], SIDES[d], discrate[d], ftell(f));
        for (t=0;t<TRACKS[d];t++)
        {
                for (h=0;h<SIDES[d];h++)
                {
                        for (s=0;s<SECTORS[d];s++)
                        {
                                for (b=0;b<BPS[d];b++)
                                {
                                        if (feof(f))
					{
						disc[d][h][t][s][b]=0xF6;
					}
					else
					{
						disc[d][h][t][s][b]=getc(f);
					}
                                }
                        }
                }
        }
        fclose(f);
        discmodified[d]=0;
        strcpy(discfns[d],fn);
        discchanged[d]=1;
	drivetypeid(0);
	drivetypeid(1);
	fdc.format_started[d] = 0;
	fdc.track[d] = 0;
	fdc.head[d] = 0;
	fdc.sector[d] = 1;
	fdc.pos[d] = 0;
	resetscanbuf(d);
	fdc.scanpos[d] = 0;
	fdc.gotdata[d] = 0;
}

void savedisc(int d)
{
        FILE *f;
        int h,t,s,b;
	int dw;
        if (!discmodified[d]) return;
        f=fopen(discfns[d],"wb");
        if (!f) return;
        // printf("Save disc %c: %s %i %i\n",'A'+d,discfns[0],SIDES[0],SECTORS[0]);
	if(IMGTYPE[d] == 2)
	{
		putc('P',f);
		putc('C',f);
		putc('e',f);
		putc('m',f);
		putc((uint8_t) SIDES[d],f);
		putc((uint8_t) TRACKS[d],f);
		putc((uint8_t) SECTORS[d],f);
		putc((uint8_t) (BPS[d] >> 7),f);
		putc((uint8_t) WP[d],f);
		putc(0,f);
		putc(0,f);
		putc(0,f);
		putc(0,f);
		putc(0,f);
		putc(0,f);
		putc(0,f);
		curoffset = 16;
        	for (t=0;t<TRACKS[d];t++)
        	{
                	for (h=0;h<SIDES[d];h++)
                	{
                        	for (s=0;s<SECTORS[d];s++)
                        	{
                                        putc(sstat[d][h][t][s],f);
					curoffset++;
                        	}
                	}
        	}
		for(t = curoffset; t <= 32767; t++)
		{
			putc(0,f);
		}
	}
	else if (IMGTYPE[d] == 3)
	{
		dw = 0;
		fwrite(&dw, 1, 4, f);
		dw = RAWOFFS[d];
		fwrite(&dw, 1, 4, f);
		dw = TRACKS[d] * SIDES[d] * SECTORS[d] * BPS[d];
		fwrite(&dw, 1, 4, f);
		dw = BPS[d];
		fwrite(&dw, 1, 4, f);
		dw = SECTORS[d];
		fwrite(&dw, 1, 4, f);
		dw = SIDES[d];
		fwrite(&dw, 1, 4, f);
		dw = TRACKS[d];
		for(t = 0x20; t < RAWOFFS[d]; t++)
		{
			putc(0, f);
		}
	}
        for (t=0;t<TRACKS[d];t++)
        {
                for (h=0;h<SIDES[d];h++)
                {
                        for (s=0;s<SECTORS[d];s++)
                        {
                                for (b=0;b<BPS[d];b++)
                                {
                                        putc(disc[d][h][t][s][b],f);
                                }
                        }
                }
        }
        fclose(f);
}

int mode3_enabled()
{
	int i = 0;
	if (fdcmodel == 3)
	{
		i = (fdc.cr[0][0xF4] >> 6) | (fdc.cr[0][0xF5] >> 7);
		return (i == 3);
	}
	else if ((fdcmodel == 2) || (fdcmodel == 4) || (fdcmodel == 5))
	{
		// Optional workaround to allow the usage of the vector.co.jp 95/98 drivers.
		if (vectorworkaround)
		{
			return ((fdc.cr[0][0xF1] & 0xC) == 8);
		}
		else
		{
			return ((fdc.cr[0][0xF1] & 0xC) == 0xC);
		}
	}
	else if (fdcmodel == 1)
	{
		return ((fdc.cr[0][5] & 0x18) == 0x18);
	}
	else
	{
		return 0;
	}
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
	fdc.rconfig = 0;
	fdc.ccr = 0;

	for (i = 0; i < 4; i++)
	{
		fdc.ring[0][i] = 0;
		fdc.ring[1][i] = 0;
	}

	fdc.iocycle = 0;

	for (i = 0; i < 272; i++)
	{
		fdc.cr[0][i] = 0;
		fdc.cr[1][i] = 0;
		fdc.cr[2][i] = 0;
		fdc.cr[3][i] = 0;
		fdc.cr[4][i] = 0;
		fdc.cr[5][i] = 0;
		fdc.cr[6][i] = 0;
		fdc.cr[7][i] = 0;
		fdc.cr[8][i] = 0;
		fdc.cr[9][i] = 0;
		fdc.cr[10][i] = 0;
		fdc.cr[11][i] = 0;
		fdc.cr[12][i] = 0;
	}

	if (fdcmodel == 5)
	{
		// Global configuration registers
		fdc.cr[8][3] = 3;
		fdc.cr[8][0x20] = 9;
		fdc.cr[8][0x24] = 4;

		// Logical device 0 configuration registers (FDD)
		fdc.cr[0][0x30] = 1;
		fdc.cr[0][0x60] = 3;
		fdc.cr[0][0x61] = 0xF0;
		fdc.cr[0][0x70] = 6;
		fdc.cr[0][0x74] = 2;
		fdc.cr[0][0xF0] = 0xE;
		fdc.cr[0][0xF2] = 0xFF;

		// Logical device 3 configuration registers (Parallel Port)
		fdc.cr[3][0x30] = 1;
		fdc.cr[3][0x60] = 3;
		fdc.cr[3][0x61] = 0x78;
		fdc.cr[3][0x70] = 7;
		fdc.cr[3][0x74] = 4;
		fdc.cr[3][0xF0] = 0x3C;

		// Logical device 4 configuration registers (Serial Port 1)
		fdc.cr[4][0x30] = 1;
		fdc.cr[4][0x60] = 3;
		fdc.cr[4][0x61] = 0xF8;
		fdc.cr[4][0x70] = 4;

		// Logical device 5 configuration registers (Serial Port 2)
		fdc.cr[5][0x30] = 1;
		fdc.cr[5][0x60] = 2;
		fdc.cr[5][0x61] = 0xF8;
		fdc.cr[5][0x70] = 3;
		fdc.cr[5][0x74] = 4;
		fdc.cr[5][0xF1] = 2;
		fdc.cr[5][0xF2] = 3;

		// Logical device 6 configuration registers (RTC)
		fdc.cr[6][0x30] = 1;
		fdc.cr[6][0x70] = 2;

		// Logical device 7 configuration registers (Keyboard)
		fdc.cr[7][0x30] = 1;
		fdc.cr[7][0x70] = 1;

		if (THREEMODE[0] & !BIGFLOPPY[0])  fdc.cr[0][0xF4] |= 8;
		if (THREEMODE[1] & !BIGFLOPPY[1])  fdc.cr[0][0xF5] |= 8;
	}
	else if (fdcmodel == 4)
	{
		// Global configuration registers
		fdc.cr[12][0x20] = 0x51;
		fdc.cr[12][0x24] = 4;
		// CAUTION: We have to return 0x4E if read from that
		fdc.cr[12][0x26] = 0x2E;

		// Logical device 0 configuration registers (FDD)
		fdc.cr[0][0x30] = 1;
		fdc.cr[0][0x60] = 3;
		fdc.cr[0][0x61] = 0xF0;
		fdc.cr[0][0x70] = 6;
		fdc.cr[0][0x74] = 2;
		fdc.cr[0][0xF0] = 0xE;
		fdc.cr[0][0xF2] = 0xFF;
		fdc.cr[0][0x100] = 0xE;
		fdc.cr[0][0x102] = 0xFF;

		// Logical device 3 configuration registers (Parallel Port)
		fdc.cr[3][0x30] = 1;
		fdc.cr[3][0x60] = 3;
		fdc.cr[3][0x61] = 0x78;
		fdc.cr[3][0x70] = 7;
		fdc.cr[3][0x74] = 4;
		fdc.cr[3][0xF0] = 0x3C;

		// Logical device 4 configuration registers (Serial Port 1)
		fdc.cr[4][0x30] = 1;
		fdc.cr[4][0x60] = 3;
		fdc.cr[4][0x61] = 0xF8;
		fdc.cr[4][0x70] = 4;

		// Logical device 5 configuration registers (Serial Port 2)
		fdc.cr[5][0x30] = 1;
		fdc.cr[5][0x60] = 2;
		fdc.cr[5][0x61] = 0xF8;
		fdc.cr[5][0x70] = 3;
		fdc.cr[5][0xF1] = 2;
		fdc.cr[5][0xF2] = 3;

		// Logical device 7 configuration registers (Keyboard)
		fdc.cr[7][0x30] = 1;
		fdc.cr[7][0x70] = 1;
		fdc.cr[7][0x71] = 0xC;

		// Logical device 9 configuration registers (Game Port)
		fdc.cr[9][0x30] = 1;
		fdc.cr[9][0x60] = 2;
		fdc.cr[9][0x61] = 1;

		// Logical device B configuration registers (MPU-401)
		fdc.cr[11][0x30] = 1;
		fdc.cr[11][0x60] = 3;
		fdc.cr[11][0x61] = 0x30;
		fdc.cr[11][0x70] = 9;

		if (THREEMODE[0] & !BIGFLOPPY[0])  fdc.cr[0][0xF4] |= 8;
		if (THREEMODE[1] & !BIGFLOPPY[1])  fdc.cr[0][0xF5] |= 8;
	}
	else if (fdcmodel == 3)
	{
		// Global Configuration Registers
		fdc.cr[11][0x20] = 0x87;
		fdc.cr[11][0x21] = 0x12;
		fdc.cr[11][0x22] = 7;
		fdc.cr[11][0x25] = 1;
		fdc.cr[11][0x28] = 0x40;

		// FDC Configuration Registers
		fdc.cr[0][0x30] = 1;
		fdc.cr[0][0x60] = 3;
		fdc.cr[0][0x61] = 0xF0;
		fdc.cr[0][0x70] = 6;
		fdc.cr[0][0x74] = 2;

		// Serial Port 1 Configuration Registers
		fdc.cr[1][0x30] = 1;
		fdc.cr[1][0x60] = 3;
		fdc.cr[1][0x61] = 0xF8;
		fdc.cr[1][0x70] = 3;
		fdc.cr[1][0xF1] = 0x50;
		fdc.cr[1][0xF3] = 0x7F;

		// Serial Port 2 Configuration Registers
		fdc.cr[2][0x30] = 1;
		fdc.cr[2][0x60] = 2;
		fdc.cr[2][0x61] = 0xF8;
		fdc.cr[2][0x70] = 2;
		fdc.cr[2][0xF1] = 0x50;
		fdc.cr[2][0xF3] = 0x7F;

		// Parallel Port Configuration Registers
		fdc.cr[3][0x30] = 1;
		fdc.cr[3][0x60] = 3;
		fdc.cr[3][0x61] = 0x78;
		fdc.cr[3][0x62] = 7;
		fdc.cr[3][0x63] = 0x78;
		fdc.cr[3][0x70] = 7;
		fdc.cr[3][0x74] = 3;
		fdc.cr[3][0xF0] = 3;

		// Environment Controller Configuration Registers
		fdc.cr[4][0x60] = 2;
		fdc.cr[4][0x61] = 0x90;
		fdc.cr[4][0x62] = 2;
		fdc.cr[4][0x63] = 0x30;
		fdc.cr[4][0x70] = 0xA;

		// KBC(Keyboard) Configuration Registers
		fdc.cr[5][0x30] = 1;
		fdc.cr[5][0x61] = 0x60;
		fdc.cr[5][0x63] = 0x64;
		fdc.cr[5][0x70] = 1;
		fdc.cr[5][0x71] = 2;

		// KBC(Mouse) Configuration Registers
		fdc.cr[6][0x30] = 1;
		fdc.cr[6][0x70] = 0xC;
		fdc.cr[6][0x71] = 2;

		// GPIO Configuration Registers
		fdc.cr[7][0xC0] = 1;
		fdc.cr[7][0xC3] = 0x40;
		fdc.cr[7][0xC8] = 1;
		fdc.cr[7][0xCB] = 0x40;

		// MIDI Port Configuration Registers
		fdc.cr[8][0x30] = 1;
		fdc.cr[8][0x60] = 3;
		fdc.cr[8][0x61] = 0x30;
		fdc.cr[8][0x70] = 9;

		// Game Port Configuration Registers
		fdc.cr[9][0x60] = 2;
		fdc.cr[9][0x61] = 1;

		// Consumer IR Configuration Registers
		fdc.cr[10][0x60] = 3;
		fdc.cr[10][0x61] = 0x10;
		fdc.cr[10][0x70] = 0xB;

		if (THREEMODE[0] & !BIGFLOPPY[0])  fdc.cr[0][0xF1] |= 1;
		if (THREEMODE[1] & !BIGFLOPPY[1])  fdc.cr[0][0xF1] |= 4;
	}
	else if (fdcmodel == 2)
	{
		// Global configuration registers
		fdc.cr[9][3] = 3;
		fdc.cr[9][0x20] = 0x4D;
		fdc.cr[9][0x24] = 4;

		// Logical device 0 configuration registers (FDD)
		fdc.cr[0][0x30] = 1;
		fdc.cr[0][0x60] = 3;
		fdc.cr[0][0x61] = 0xF0;
		fdc.cr[0][0x70] = 6;
		fdc.cr[0][0x74] = 2;
		fdc.cr[0][0xF0] = 0xE;
		fdc.cr[0][0xF2] = 0xFF;

		// Logical device 3 configuration registers (Parallel Port)
		fdc.cr[3][0x30] = 1;
		fdc.cr[3][0x60] = 3;
		fdc.cr[3][0x61] = 0x78;
		fdc.cr[3][0x70] = 7;
		fdc.cr[3][0x74] = 4;
		fdc.cr[3][0xF0] = 0x3C;

		// Logical device 4 configuration registers (Serial Port 1)
		fdc.cr[4][0x30] = 1;
		fdc.cr[4][0x60] = 3;
		fdc.cr[4][0x61] = 0xF8;
		fdc.cr[4][0x70] = 4;

		// Logical device 5 configuration registers (Serial Port 2)
		fdc.cr[5][0x30] = 1;
		fdc.cr[5][0x60] = 2;
		fdc.cr[5][0x61] = 0xF8;
		fdc.cr[5][0x70] = 3;
		fdc.cr[5][0xF1] = 2;
		fdc.cr[5][0xF2] = 3;

		// Logical device 7 configuration registers (Keyboard)
		fdc.cr[7][0x30] = 1;
		fdc.cr[7][0x70] = 1;

		// Logical device 8 configuration registers (Aux I/O)
		fdc.cr[8][0xC0] = 2;
		fdc.cr[8][0xC1] = 1;

		if (THREEMODE[0] & !BIGFLOPPY[0])  fdc.cr[0][0xF4] |= 8;
		if (THREEMODE[1] & !BIGFLOPPY[1])  fdc.cr[0][0xF5] |= 8;
	}
	else if (fdcmodel == 1)
	{
		fdc.cr[0][0] = 0x28;
		fdc.cr[0][1] = 0x9C;
		fdc.cr[0][2] = 0x88;
		fdc.cr[0][3] = 0x70;
		fdc.cr[0][6] = 0xFF;
		fdc.cr[0][0xB] = 0;
		fdc.cr[0][0xC] = 2;
		fdc.cr[0][0xD] = 0x29;
		fdc.cr[0][0x11] = 0x80;
		fdc.cr[0][0x12] = 0xF0;
		fdc.cr[0][0x13] = 3;
		fdc.cr[0][0x17] = 3;
		fdc.cr[0][0x1E] = 0x80;
		fdc.cr[0][0x20] = 0x3C;
		fdc.cr[0][0x2C] = 0xF;
		fdc.cr[0][0x2D] = 3;

		if (THREEMODE[0] & !BIGFLOPPY[0])  fdc.cr[0][0xB] |= 1;
		if (THREEMODE[1] & !BIGFLOPPY[1])  fdc.cr[0][0xB] |= 4;
	}
	fdc.dsr = 2;
	fdc.densel = 0;
	fdc.st1 = 0;
	fdc.st2 = 0;
	fdc.dor |= 0xF8;
	fdc.dor &= 0xFC;
	drivetypeid(0);
	drivetypeid(1);
	fdc.format_started[0] = 0;
	fdc.format_started[1] = 0;
	fdc.dma = 1;
	fdc.tdr = 0;
	fdc.deldata = 0;
	fdc.fifo = 0;
	fdc.tfifo = 1;
	resetscanbuf(0);
	resetscanbuf(1);
	fdc.scanpos[0] = 0;
	fdc.scanpos[1] = 0;
	fdc.gotdata[0] = 0;
	fdc.gotdata[1] = 0;
}

int discint;
void fdc_reset()
{
        fdc.stat=0x80;
        fdc.pnum=fdc.ptot=0;
        fdc.st0=0xC0;
        fdc.lock = 0;
        fdc.head[0] = 0;
        fdc.head[1] = 0;
        fdc.abort = 0;
	fdc.pos[0] = 0;
	fdc.pos[1] = 0;
	// config_default();
	// picint(0xc0);
        if (!AT)
           fdc.rate=2;
//        pclog("Reset FDC\n");
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
	pclog("Wrong rate %i %i\n",fdc.rate,discrate[fdc.drive]);
	discint=dint;
	if (dint==0xFE)  return 2;
	disctime = 1024 * (1 << TIMER_SHIFT);
	return 1;
}

int fdc_cfail()
{
	if (BIGFLOPPY[fdc.drive])
	{
		fdc_fail(0xFE);
	}
	else
	{
		fdc_fail(0xFF);
	}
	return 1;
}

// Rates 0 and 5 should require mode3_enabled being 0 on a 3-mode drive
int fdc_checkrate()
{
	int x = 0;

	if (discrate[fdc.drive] == 7)
	{
		x += fdc_fail(0xFE);		// If rate of inserted disk is invalid, assume drive is empty
		return 0;
	}

	if (SIDES[fdc.drive] > 2)  x += fdc_fail(0xFE);
	if (TRACKS[fdc.drive] > 90)  x += fdc_fail(0xFE);
	if (SECTORS[fdc.drive] > 50)  x += fdc_fail(0xFE);

	if (TOTAL[fdc.drive] * BPS[fdc.drive] > (4000 * 1024))  x += fdc_fail(0xFE);

	if (discint == 0xFE)  return 0;

	if (fdc.rate == 0)
	{
		if (discrate[fdc.drive] == 0)
		{
			// High-density single-side disks are not valid
			if ((SIDES[fdc.drive] == 1) || (DENSITY[fdc.drive] == 0))  x += fdc_fail(0xFF);
			if (!BIGFLOPPY[fdc.drive] && mode3_enabled())  x += fdc_fail(0xFF);
		}
		else if (discrate[fdc.drive] == 1)
		{
			x += fdc_cfail(0);
		}
		else if (discrate[fdc.drive] == 2)
		{
			x += fdc_fail(0xFF);
		}
		else if (discrate[fdc.drive] == 3)
		{
			x += fdc_cfail();
		}
		else if (discrate[fdc.drive] == 4)
		{
			// Fail if drive is low density
			if (DENSITY[fdc.drive] == 0)  x += fdc_fail(0xFF);
			// Here we need to check bits 4,3 of cr[5], ie. DENSEL
			// But only if we're not on a 5.25" drive
			if (!BIGFLOPPY[fdc.drive])
			{
				if (!(mode3_enabled()) || !THREEMODE[fdc.drive])  x += fdc_fail(0xFF);
				if (!BIGFLOPPY[fdc.drive] && (DENSITY[fdc.drive] == 2))  x += fdc_fail(0xFF);
			}
		}
		else if (discrate[fdc.drive] == 5)
		{
			// Make sure we do not accept 3.5" HD floppy images > 1.6 MB as valid if drive is a 5.25"
			if (BIGFLOPPY[fdc.drive])
			{
				x += fdc_fail(0xFE);
			}
			else
			{
				if ((DENSITY[fdc.drive] == 0) || mode3_enabled())  x += fdc_fail(0xFF);
			}
		}
		else if (discrate[fdc.drive] == 6)
		{
			x += fdc_fail(0xFF);
		}
		else if (discrate[fdc.drive] == 8)
		{
			x += fdc_fail(0xFF);
		}
	}
	else if (fdc.rate == 1)
	{
		if (!BIGFLOPPY[fdc.drive] && !THREEMODE[fdc.drive])
		{
			x += fdc_fail(0xFF);
		}
		if (BIGFLOPPY[fdc.drive] && !DENSITY[fdc.drive])
		{
			x += fdc_fail(0xFF);
		}
		if (discrate[fdc.drive] == 0)
		{
			x += fdc_fail(0xFF);
		}
		else if (discrate[fdc.drive] == 1)
		{
			// Only on 5.25" drives and double-sided disks, also not valid for low-density 5.25" drives
			if (!BIGFLOPPY[fdc.drive] || (SIDES[fdc.drive] == 1) || (DENSITY[fdc.drive] == 0))  x += fdc_fail(0xFF);
		}
		else if (discrate[fdc.drive] == 2)
		{
			// Only on 5.25" drives and double-sided disks, also not valid for low-density 5.25" drives
			if (!BIGFLOPPY[fdc.drive] || (SIDES[fdc.drive] == 1) || (DENSITY[fdc.drive] == 0))  x += fdc_fail(0xFF);
		}
		else if (discrate[fdc.drive] == 3)
		{
			x += fdc_fail(0xFF);
		}
		else if (discrate[fdc.drive] == 4)
		{
			x += fdc_fail(0xFF);
		}
		else if (discrate[fdc.drive] == 5)
		{
			x += fdc_fail(0xFF);
		}
		else if (discrate[fdc.drive] == 6)
		{
			x += fdc_fail(0xFF);
		}
		else if (discrate[fdc.drive] == 8)
		{
			x += fdc_fail(0xFF);
		}
	}
	else if (fdc.rate == 2)
	{
		// For 5.25" drives, density denotes rate 2 (125/250 kbps) is disabled
		if (BIGFLOPPY[fdc.drive] && DENSITY[fdc.drive] && !THREEMODE[fdc.drive])
		{
			x += fdc_fail(0xFF);
		}
		if (discrate[fdc.drive] == 0)
		{
			x += fdc_fail(0xFF);
		}
		else if (discrate[fdc.drive] == 1)
		{
			// On 3.5" drives, 1-side disks are not valid
			if (!BIGFLOPPY[fdc.drive] && (SIDES[fdc.drive] == 1))  x += fdc_fail(0xFF);
		}
		else if (discrate[fdc.drive] == 2)
		{
			// On 3.5" drives, 1-side disks are not valid
			if (!BIGFLOPPY[fdc.drive] && (SIDES[fdc.drive] == 1))  x += fdc_fail(0xFF);
		}
		else if (discrate[fdc.drive] == 3)
		{
			x += fdc_cfail();
		}
		else if (discrate[fdc.drive] == 4)
		{
			x += fdc_fail(0xFF);
		}
		else if (discrate[fdc.drive] == 5)
		{
			x += fdc_cfail();
		}
		else if (discrate[fdc.drive] == 6)
		{
			x += fdc_fail(0xFF);
		}
		else if (discrate[fdc.drive] == 8)
		{
			// 3.5" 1-side 360 kB single-sided disk
			if (BIGFLOPPY)  x += fdc_fail(0xFF);
		}
	}
	else if (fdc.rate == 3)
	{
		if (DENSITY[fdc.drive] < 2)  x += fdc_fail(0xFF);
		if (SIDES[fdc.drive] == 1)  x += fdc_fail(0xFE);

		if (discrate[fdc.drive] == 0)
		{
			x += fdc_fail(0xFF);
		}
		else if (discrate[fdc.drive] == 1)
		{
			x += fdc_fail(0xFE);
		}
		else if (discrate[fdc.drive] == 2)
		{
			x += fdc_fail(0xFF);
		}
		else if (discrate[fdc.drive] == 3)
		{
			if (BIGFLOPPY)  x += fdc_fail(0xFE);
		}
		else if (discrate[fdc.drive] == 4)
		{
			// Always fail these at 1 Mbps
			x += fdc_fail(0xFF);
		}
		else if (discrate[fdc.drive] == 5)
		{
			x += fdc_fail(0xFF);
		}
		else if (discrate[fdc.drive] == 6)
		{
			// Extended-density single-side disks are not valid
			if ((SIDES[fdc.drive] == 1) || (DENSITY[fdc.drive] == 0))  x += fdc_fail(0xFF);
		}
		else if (discrate[fdc.drive] == 8)
		{
			x += fdc_fail(0xFF);
		}
	}
	else
	{
		x += fdc_fail(0xFF);
	}
	if (BIGFLOPPY[fdc.drive] && (BPS[fdc.drive] > 512))  x += fdc_fail(0xFE);
	if (!BIGFLOPPY[fdc.drive] && (BPS[fdc.drive] < 512))  x += fdc_fail(0xFE);

	// Don't allow too small floppies on 3.5" drives
	if (!BIGFLOPPY[fdc.drive] && (TRACKS[fdc.drive] < 70))  x += fdc_fail(0xFE);

	if (driveempty[fdc.drive])  x += fdc_fail(0xFE);

	if (x)  return 0;
	return 1;
}

int fdc_checkparams()
{
	int x = 0;
	// Basically, if with current track number we don't reach above the maximum size for our floppy class
	x = samediskclass(fdc.drive, fdc.params[5] + 1, fdc.params[2], fdc.params[1]);
	pclog("SDC: %02X\n", x);
	if (!x)  fdc_fail(0x101);
	return x;
}

// bits 6,7 - only for first sector of each track: 11 = data, 10 = deleted data, 0x = invalid
// bits 5 - only for first sector of each track: 1 = id, 0 = nothing
// bit 2 - data status: 1 = ok, 0 = data error
// bit 1 - id status: 1 = ok, 0 = data error
int sector_state(int read)
{
	uint8_t state = sstat[fdc.drive][fdc.head[fdc.drive]][fdc.track[fdc.drive]][fdc.sector[fdc.drive]-1];
	printf("Drive %c: Sector (T%d H%d S%d) state: %02X\n", 0x41 + fdc.drive, fdc.track[fdc.drive], fdc.head[fdc.drive], fdc.sector[fdc.drive], state);

	if (read != 3)
	{
		if (discint != 13)
		{
			if ((fdc.params[4] != BPSCODE[fdc.drive]) && (fdc.params[4] != 0))
			{
				fdc.st1 |= 4;
				return 1;
			}
		}
		else
		{
			if (fdc.params[1] != BPSCODE[fdc.drive])
			{
				fdc.st1 |= 4;
				return 1;
			}
		}
	}

	if (state == 0xE3)  return 0;

	if (read)
	{
		if (!(state & 2))
		{
			fdc.st1 |= 0x20;
			fdc.st2 |= 0x20;
		}

		if (!(state & 1))  fdc.st1 |= 0x20;

		if (!(state & 0x80))  fdc.st1 |= 1;
		if (!(state & 0x20))  fdc.st1 |= 1;
	}
	if (read == 3)
	{
		if (fdc.sector[fdc.drive] == 1)
		{
			if (!(state & 1))  fdc.st1 |= 4;
		}
		if (!fdc.deldata && ((state & 0xC0) == 0x80))  fdc.st2 |= 0x40;
		if (fdc.deldata && ((state & 0xC0) == 0xC0))  fdc.st2 |= 0x40;
	}
	if (!(state & 0x80))  fdc.st2 |= 1;
	if ((fdc.st1 == 0) && (fdc.st2 == 0))  return 0;
	fatal("Sector state bad!\n");
	return 1;
}

int fdc_format()
{
	if (OLD_C < 0)
	{
		OLD_C = fdc.params[5];
		OLD_H = fdc.params[6];
		OLD_R = fdc.params[7];
	}
	pclog("ROK!\n");
	fdc.pos[fdc.drive] = 0;
	for(fdc.pos[fdc.drive] = 0; fdc.pos[fdc.drive] < BPS[fdc.drive]; fdc.pos[fdc.drive]++)
	{
		disc[fdc.drive][fdc.head[fdc.drive]][fdc.track[fdc.drive]][fdc.sector[fdc.drive] - 1][fdc.pos[fdc.drive]] = fdc.fillbyte[fdc.drive];
	}
	sectors_formatted[fdc.drive]++;
	if (fdc.sector[fdc.drive] < SECTORS[fdc.drive])  fdc.sector[fdc.drive]++;
	sstates[fdc.drive] += sector_state(0);
	pclog("FDCFMT: %02X %02X %02X %02X | %02X %02X %02X\n", fdc.params[5], fdc.params[6], fdc.params[7], fdc.params[8], sstates[fdc.drive], fdc.st1, fdc.st2);
	return 0;
}

int is_high_port(uint16_t addr)
{
	if ((addr & 0x60) == 0x40)
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

int is_high_config()
{
	if ((fdc.rconfig & 0x40) == 0x40)
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

void add_to_ring(uint8_t iring, uint8_t val)
{
	int i = 0;
	for (i = 0; i < 3; i++)
	{
		fdc.ring[iring][i] = fdc.ring[iring][i + 1];
	}
	fdc.ring[iring][3] = val;

}

uint32_t ringint(uint8_t iring)
{
	return *((uint32_t *) fdc.ring[iring]);
}

void intring(uint8_t iring, uint32_t val)
{
	*((uint32_t *) fdc.ring[iring]) = val;
}

void ite_config_write(uint16_t addr, uint8_t val, void *priv)
{
	uint32_t reqkey = 0x55550187;
	uint8_t iring = 0;
	uint8_t imax = 11;
	if (fdcmodel == 4)  imax = 12;
        printf("Write ITE Config %04X %02X %04X:%04X %02X\n",addr,val,cs>>4,pc,val);
	if (is_high_port(addr))
	{
		reqkey = 0xAA550187;
		iring = 1;
	}
	// Block write on incorrect port
	if ((fdc.rconfig && (is_high_port(addr) != is_high_config())))  return;
	printf("Writing on the correct port\n");
	if (!fdc.rconfig)  printf("Not in configuration mode\n");
	switch (addr&1)
	{
		case 0: /*Index*/
			if (fdcmodel == 4)
			{
				if (!fdc.rconfig)
				{
					if (val == 0x55)
					{
						printf("Setting config state on\n");
						fdc.rconfig = 1;
					}
				}
				else
				{
					if (val == 0xAA)
					{
						printf("Setting config state off\n");
						fdc.rconfig = 0;
					}
					else
					{
						fdc.ccr = val;
					}
				}
				break;
			}
			if (!fdc.rconfig)
			{
				add_to_ring(iring, val);
				// if (ringint(iring) == reqkey)
				printf("Ring 0x%01XE = %08X\n", 2 << iring, ringint(iring));
				if (((ringint(iring) & 0xFFFF0000) == 0x87870000) || (ringint(iring) == reqkey))
				{
					printf("Entry passed, setting config state on\n");
					fdc.rconfig = 1;
					if (is_high_port(addr))  fdc.rconfig |= 0x40;
					fdc.cr[11][2] = 0;
				}
			}
			else
			{
				fdc.ccr = val;
			}
			break;
		case 1: /*Data*/
			if (fdc.rconfig)
			{
				if (fdc.ccr < 48)
				{
					// Block writes to device ID register
					if ((fdc.ccr != 0x20) && (fdc.ccr != 0x24))  fdc.cr[imax][fdc.ccr] = val;
				}
				else
				{
					fdc.cr[fdc.cr[imax][7]][fdc.ccr] = val;
				}
				if ((fdc.cr[imax][2] & 2) && (fdcmodel == 3))
				{
					fdc.rconfig = 0;
					intring(iring, 0);
					printf("Config state is now off\n");
				}
			}
			break;
	}
	return;
}

void fifo_buf_write(int val)
{
	if (fdc.fifobufpos < fdc.tfifo)
	{
		fdc.fifobuf[fdc.fifobufpos++] = val;
		if (fdc.fifobufpos == fdc.tfifo)  fdc.fifobufpos = 0;
	}
}

int fifo_buf_read()
{
	int temp = 0;
	if (fdc.fifobufpos < fdc.tfifo)
	{
		temp = fdc.fifobuf[fdc.fifobufpos++];
		if (fdc.fifobufpos == fdc.tfifo)  fdc.fifobufpos = 0;
	}
	return temp;
}

int paramstogo=0;
void fdc_write(uint16_t addr, uint8_t val, void *priv)
{
	int imax = 9;
	if (fdcmodel == 4)  imax = 12;
	if (fdcmodel == 5)  imax = 8;
//        printf("Write FDC %04X %02X %04X:%04X %i %02X %i rate=%i\n",addr,val,cs>>4,pc,ins,fdc.st0,ins,fdc.rate);
	printf("OUT 0x%04X, %02X\n", addr, val);
        switch (addr&7)
        {
		case 0: /*Configuration*/
			// pclog("Writing configuration status value: %02X, old %02X\n", val, fdc.ccr);
			if (!fdcmodel)  return;
			// if ((fdcmodel > 2) && (fdcmodel != 5))  return;
			if (fdcmodel == 3)  return;
			if (fdcmodel == 6)  return;
			switch(val)
			{
				case 0x55:
					if (!fdc.rconfig) fdc.ccr = 0xFF;
					if ((fdcmodel == 2) || (fdcmodel == 4) || (fdcmodel == 5))  fdc.cr[imax][7] = imax;
					fdc.rconfig = 1;
					return;
				case 0xAA:
					fdc.rconfig = 0;
					fdc.ccr = 0xFF;
					if ((fdcmodel == 2) || (fdcmodel == 4) || (fdcmodel == 5))  fdc.cr[imax][7] = imax;
					return;
				default:
					if ((fdcmodel == 2) || (fdcmodel == 4) || (fdcmodel == 5))
					{
						fdc.ccr = val;
					}
					else
					{
						if (fdc.rconfig)  if (val < 48)  fdc.ccr = val;
					}
					return;
			}
			break;
                case 1: /*Change configuration*/
			if (!fdcmodel)  return;
			// if ((fdcmodel > 2) && (fdcmodel != 5))  return;
			if (fdcmodel == 3)  return;
			if (fdcmodel == 6)  return;
			// pclog("Writing value of CR%02X: %02X, old %02X\n", fdc.ccr, val, fdc.cr[5]);
			// if (fdc.ccr == 5)  pclog("Setting CR5 to %02X, old %02X\n", val, fdc.cr[5]);
			if (fdcmodel == 1)  if (fdc.rconfig && (fdc.ccr < 48))  if (fdc.ccr != 0xD)  fdc.cr[0][fdc.ccr] = val;
			if ((fdcmodel == 2) || (fdcmodel == 4) || (fdcmodel == 5))
			{
				if (fdc.rconfig && (fdc.cr[imax][7] <= 9) && (fdc.ccr >= 48))
				{
					fdc.cr[fdc.cr[imax][7]][fdc.ccr] = val;
				}
				else
				{
					if (fdc.rconfig && (fdc.ccr < 48))
					{
						if ((fdc.ccr != 0x20) && (fdc.ccr != 0x24))  fdc.cr[imax][fdc.ccr] = val;
					}
				}
			}
			return;
                case 2: /*DOR*/
                // if (val == 0xD && (cs >> 4) == 0xFC81600 && ins > 769619936) output = 3;
//                printf("DOR was %02X\n",fdc.dor);
                if (fdc.pcjr)
                {
			if (!(val&0x80) && !(fdc.dor&0x80))  return;		
                        if ((fdc.dor & 0x40) && !(val & 0x40))
                        {
                                fdc.watchdog_timer = 1000 * TIMER_USEC;
                                fdc.watchdog_count = 1000;
                                picintc(1 << 6);
//                                pclog("watchdog set %i %i\n", fdc.watchdog_timer, TIMER_USEC);
                        }
                        if ((val & 0x80) && !(fdc.dor & 0x80))
                        {
        			timer_process();
                                disctime = 128 * (1 << TIMER_SHIFT);
                                timer_update_outstanding();
                                discint=-1;
                                fdc_reset();
                        }
                }
                else
                {
			//  If in RESET state, return
			if (!(val&4) && !(fdc.dor&4))  return;
                        if (val&4)
                        {
                                fdc.stat=0x80;
                                fdc.pnum=fdc.ptot=0;
                        }
                        if ((val&4) && !(fdc.dor&4))
                        {
        			timer_process();
                                disctime = 128 * (1 << TIMER_SHIFT);
                                timer_update_outstanding();
                                discint=-1;
				printf("Resetting the FDC\n");
                                fdc_reset();
                        }
                }
                fdc.dor=val;
		// if(!(fdc.pcjr))  fdc.dor |= 0x30;
		fdc.dor |= 0x30;
//                printf("DOR now %02X\n",val);
                return;
		case 3:
		if (!(fdc.dor&4))  return;
		if (!(fdc.dor&0x80) && fdc.pcjr)  return;		
		fdc.tdr=val&3;
		return;
                case 4:
		if (!(fdc.dor&4))  return;
		if (!(fdc.dor&0x80) && fdc.pcjr)  return;		
                if (val & 0x80)
                {
			if (!fdc.pcjr)  fdc.dor &= 0xFB;
			if (fdc.pcjr)  fdc.dor &= 0x7F;
			timer_process();
                        disctime = 128 * (1 << TIMER_SHIFT);
                        timer_update_outstanding();
                        discint=-1;
                        fdc_reset();
			if (!fdc.pcjr) fdc.dor |= 4;
			if (fdc.pcjr) fdc.dor |= 0x80;
                }
		if (val & 0x40)
		{
			timer_process();
			disctime = 128 * (1 << TIMER_SHIFT);
			timer_update_outstanding();
			discint=-1;
			fdc_reset();
		}
		// if (!AT)
		// {
                	fdc.rate=val&3;
		// }
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
//                pclog("Write command reg %i %i\n",fdc.pnum, fdc.ptot);
                if (fdc.pnum==fdc.ptot)
                {
                        fdc.command=val;
			fdc.densel=0;
                        switch (fdc.command&0x1F)
                        {
				case 5:
				case 9:
				case 0xD:
				case 6:
				case 0xC:
				case 2:
				case 0x16:
				case 0xA:
					fdc.densel = (val & 0x40) >> 6;
					break;
				default:
					break;
			}
//                        printf("Starting FDC command %02X\n",fdc.command);
			sstates[0] = 0;
			sstates[1] = 0;
			fdc.deldata = 0;
			fdc.res[4] = 0;
			fdc.res[5] = 0;
			fdc.res[6] = 0;
                        switch (fdc.command&0x1F)
                        {
                                case 2: /*Read track*/
//                                printf("Read track!\n");
                                fdc.pnum=0;
                                fdc.ptot=8;
                                fdc.stat=0x90;
                                // fdc.pos[fdc.drive]=0;
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
//                                printf("Write data!\n");
                                fdc.pnum=0;
                                fdc.ptot=8;
                                fdc.stat=0x90;
                                // fdc.pos[fdc.drive]=0;
                                readflash=1;
                                break;
				case 12: /*Read deleted data*/
				fdc.deldata = 1;
				case 0x16: /*Verify data*/
                                case 6: /*Read data*/
				if (((fdc.command&0x1F) == 0x16) && ((fdcmodel == 0) || (fdcmodel == 6)))  goto bad_command;
				if ((fdc.command&0x1F) != 12)  fdc.deldata = 0;
                                fullspeed();
                                fdc.pnum=0;
                                fdc.ptot=8;
                                fdc.stat=0x90;
                                // fdc.pos[fdc.drive]=0;
                                readflash=1;
                                break;
                                case 7: /*Recalibrate*/
                                fdc.pnum=0;
                                fdc.ptot=1;
                                fdc.stat=0x90;
                                break;
                                case 8: /*Sense interrupt status*/
                                printf("Sense interrupt status %i\n",fdc.drive);
                                fdc.lastdrive = fdc.drive;
//                                fdc.stat = 0x10 | (fdc.stat & 0xf);
//                                disctime=1024;
                                discint = 8;
                                // fdc.pos[fdc.drive] = 0;
                                fdc_poll();
                                break;
                                case 10: /*Read sector ID*/
                                fdc.pnum=0;
                                fdc.ptot=1;
                                fdc.stat=0x90;
                                // fdc.pos[fdc.drive]=0;
                                break;
                                case 13: /*Format*/
                                fdc.pnum=0;
                                fdc.ptot=5;
                                fdc.stat=0x90;
                                // fdc.pos[fdc.drive]=0;
                                readflash=1;
                                break;
                                case 15: /*Seek*/
				fdc.relative=fdc.command & 0x80;
				fdc.direction=fdc.command & 0x40;
				if (((fdc.command&0xC0) != 0) && ((fdcmodel == 0) || (fdcmodel == 6)))
				{
					fdc.relative=0;
					fdc.direction=0;
				}
                                fdc.pnum=0;
                                fdc.ptot=2;
                                fdc.stat=0x90;
                                break;
				case 0x11: /* Scan Equal */
				case 0x19: /* Scan Low or Equal */
				case 0x1D: /* Scan High or Equal */
				if (((fdc.command&0x1F) == 0x16) && ((fdcmodel != 3) && (fdcmodel != 6)))  goto bad_command;
                                fdc.pnum=0;
                                fdc.ptot=8;
                                fdc.stat=0x90;
                                // fdc.pos[fdc.drive]=0;
                                readflash=1;
				break;
                                case 0x0e: /*Dump registers*/
				if (fdcmodel == 6)  goto bad_command;
                                fdc.lastdrive = fdc.drive;
                                discint = 0x0e;
                                // fdc.pos[fdc.drive] = 0;
                                fdc_poll();
                                break;
                                case 0x10: /*Get version*/
                                fdc.lastdrive = fdc.drive;
                                discint = 0x10;
                                // fdc.pos[fdc.drive] = 0;
                                fdc_poll();
                                break;
                                case 0x12: /*Set perpendicular mode*/
                                fdc.pnum=0;
                                fdc.ptot=1;
                                fdc.stat=0x90;
                                // fdc.pos[fdc.drive]=0;
                                break;
                                case 0x13: /*Configure*/
                                fdc.pnum=0;
                                fdc.ptot=3;
                                fdc.stat=0x90;
                                // fdc.pos[fdc.drive]=0;
                                break;
                                case 0x14: /*Unlock*/
                                case 0x94: /*Lock*/
                                fdc.lastdrive = fdc.drive;
                                discint = fdc.command;
                                // fdc.pos[fdc.drive] = 0;
                                fdc_poll();
                                break;

                                case 0x18:
                                fdc.stat = 0x10;
                                discint  = 0xfc;
                                fdc_poll();
                                break;

                                default:
bad_command:
                                pclog("Bad FDC command %02X\n",val);
//                                dumpregs();
//                                exit(-1);
                                fdc.stat=0x10;
                                discint=0xfc;
                                timer_process();
                                disctime = 200 * (1 << TIMER_SHIFT);
                                timer_update_outstanding();
                                break;
                        }
                }
                else
                {
                        fdc.params[fdc.pnum++]=val;
                        if (fdc.pnum==fdc.ptot)
                        {
//                                pclog("Got all params\n");
                                fdc.stat=0x30;
                                discint=fdc.command&0x1F;
				printf("Processing command %02X!\n", discint);
                                timer_process();
                                disctime = 1024 * (1 << TIMER_SHIFT);
				if ((discint!=9) && (discint!=12))  fdc.deldata = 0;
				if ((discint!=8) && (discint!=0x12) && (discint!=0x14) && (discint!=0x94) && (discint!=0xE) && (discint!=0x13) && (discint!=3) && (discint!= 0x10) && (discint<=0x16))
				{
					// This is so we make sure fdc.drive isn't changed on commands that don't change it
					pclog("FDC Drive set to: %02X\n", fdc.drive);
                                	fdc.drive=fdc.params[0]&1;
					fdc.pos[fdc.drive] = 0;
                                	/* if (fdcmodel != 3)  fdc.drive=fdc.params[0]&1;
                                	if (fdcmodel == 3)
					{
						if ((fdc.params[0] & 3) == 1)  fdc.drive = 0;
						if ((fdc.params[0] & 3) == 2)  fdc.drive = 1;
					} */
				}
                                /* if (discint==7 || discint==15 || discint==10)
                                {
					fdc.head[fdc.drive]=(fdc.params[0]&4) >> 2;
				} */
                                if (discint==2 || discint==5 || discint==6 || discint==9 || discint==12 || discint==0x16)
                                {
                                        fdc.track[fdc.drive]=fdc.params[1];
                                        fdc.head[fdc.drive]=fdc.params[2];
                                        fdc.sector[fdc.drive]=fdc.params[3];
                                        fdc.eot[fdc.drive] = fdc.params[5];
                                        if (!fdc.params[5])
                                        {
                                                fdc.params[5]=fdc.sector[fdc.drive];
                                        }
                                        if (fdc.params[5]>SECTORS[fdc.drive]) fdc.params[5]=SECTORS[fdc.drive];
                                        if (driveempty[fdc.drive])
                                        {
                                                pclog("Drive empty\n");
                                                discint=0xFE;
                                        }
                                }
                                if (discint==2 || discint==5 || discint==6 || discint==9 || discint==10 || discint==12 || discint==13 || discint==0x16)
                                {
					pclog("Verifying rate\n");
                                        pclog("Rate %i %i %i\n",fdc.rate,discrate[fdc.drive],driveempty[fdc.drive]);
					fdc_checkrate();
					if ((discint == 13) && (!fdc.format_started[fdc.drive]))
					{
						if ((fdc.params[1] > 2) && BIGFLOPPY[fdc.drive])
						{
							pclog("More than 512 bytes per sector!\n");
							discint=0x100;
                                              		disctime = 1024 * (1 << TIMER_SHIFT);
						}
						if ((fdc.params[1] < 2) && !BIGFLOPPY[fdc.drive])
						{
							pclog("Less than 512 bytes per sector!\n");
                                               		discint=0x100;
                                               		disctime = 1024 * (1 << TIMER_SHIFT);
						}
						if (!fdc.densel && !BIGFLOPPY[fdc.drive])
						{
							pclog("35SD\n");
                                               		discint=0x100;
                                               		disctime = 1024 * (1 << TIMER_SHIFT);
						}
						if (!fdc.densel && (TRACKS[fdc.drive] >= 70))
						{
							pclog("77SD\n");
                                               		discint=0x100;
                                               		disctime = 1024 * (1 << TIMER_SHIFT);
						}
						if (!fdc.densel && (SIDES[fdc.drive] == 2))
						{
							pclog("DSFM\n");
                                               		discint=0x100;
                                               		disctime = 1024 * (1 << TIMER_SHIFT);
						}
						if (fdc.track[fdc.drive] < 0)  fdc_fail(0x101);
						if (fdc.track[fdc.drive] >= TRACKS[fdc.drive])
						{
							if (!samediskclass(fdc.drive, fdc.track[fdc.drive] + 1, fdc.params[2], fdc.params[1]))
							{
								fdc_fail(0x101);
							}
							else
							{
								if (discint == 13)  TRACKS[fdc.drive]++;
							}
						}
					}
					if (discint < 0xFC)  pclog("Rate is OK\n");
					if (discint >= 0xFC)  pclog("Rate is wrong\n");
                                }
                                if (discint == 7 || discint == 0xf)
                                {
                                        fdc.stat = 1 << fdc.drive;
//                                        disctime = 8000000;
                                }
                                if (discint == 0xf || discint == 10)
                                {
                                        fdc.head[fdc.drive] = (fdc.params[0] & 4) ? 1 : 0;
                                }
                                if (discint == 5 && (fdc.pcjr || !fdc.dma))
                                        fdc.stat = 0xb0;
                                if (discint == 9 && (fdc.pcjr || !fdc.dma))
                                        fdc.stat = 0xb0;
                                timer_update_outstanding();
//                                if (discint==5) fdc.pos[fdc.drive]=BPS[fdc.drive];
                        }
                }
                return;
                case 7:
                        if (!AT) return;
                fdc.rate=val&3;
                disc_3f7=val;
                return;
        }
//        printf("Write FDC %04X %02X\n",addr,val);
//        dumpregs();
//        exit(-1);
}

int isos2mode()
{
	if (!fdcmodel)  return 0;
	if (fdcmodel == 3)  return 0;
	if ((fdcmodel == 2) || (fdcmodel == 4) || (fdcmodel == 5))
	{
		return (fdc.cr[0][0xF0] & 1);
	}
	else
	{
		return (fdc.cr[0][3] & 2);
	}
}

int getdrvtype()
{
	if ((fdcmodel == 2) || (fdcmodel == 4) || (fdcmodel == 5))
	{
		return (fdc.cr[0][0xF2]);
	}
	else
	{
		return (fdc.cr[0][6]);
	}
}

int getbootdrive()
{
	if ((fdcmodel == 2) || (fdcmodel == 4) || (fdcmodel == 5))
	{
		return 0;
	}
	else
	{
		return (fdc.cr[0][7] & 3);
	}
}

uint8_t ite_config_read(uint16_t addr, void *priv)
{
	uint8_t iring = 0;
	int temp = 0xFF;
	uint8_t imax = 11;
	if (fdcmodel == 4)  imax = 12;

	if (is_high_port(addr))
	{
		iring = 1;
	}
	// Block read from incorrect port
	if ((fdc.rconfig && (is_high_port(addr) != is_high_config())))  return 0xFF;
	printf("Reading from the correct port\n");
	if (!fdc.rconfig)  printf("Not in configuration mode\n");
	switch (addr&1)
	{
		case 0: /*Index*/
			if (fdcmodel == 4)
			{
				if (!fdc.rconfig)
				{
					temp = 0xFF;
				}
				else
				{
					temp = fdc.ccr;
				}
			}
			else
			{
				if (!fdc.rconfig)
				{
					temp = fdc.ring[iring][3];
				}
				else
				{
					temp = fdc.ccr;
				}
			}
			break;
		case 1: /*Data*/
			if (fdc.rconfig)
			{
				if (fdc.ccr < 48)
				{
					temp = fdc.cr[imax][fdc.ccr];
				}
				else
				{
					temp = fdc.cr[fdc.cr[imax][7]][fdc.ccr];
				}
			}
			else
			{
				temp = 0xFF;
			}
			break;
	}
        printf("Read ITE Config %04X %04X:%04X %04X %02X=%02X\n",addr,cs>>4,pc,BX,fdc.ccr,temp);
	return temp;
}

uint8_t fdc_read(uint16_t addr, void *priv)
{
        uint8_t temp;
	int imax = 9;
	if (fdcmodel == 4)  imax = 12;
	if (fdcmodel == 5)  imax = 8;
	printf("IN 0x%04X\n", addr);
//        /*if (addr!=0x3f4) */printf("Read FDC %04X %04X:%04X %04X %i %02X %i ",addr,cs>>4,pc,BX,fdc.pos[fdc.drive],fdc.st0,ins);
        switch (addr&7)
        {
		case 0: /*Configuration, index, and status register A*/
		if (!fdcmodel)  return 0xFF;
		// if ((fdcmodel > 2) && (fdcmodel != 5))  return 0xFF;
		if (fdcmodel == 3)  return 0xFF;
		if (fdcmodel == 6)  return 0xFF;
		if (fdc.rconfig)
		{
			return fdc.ccr;
		}
		else
		{
			return 0xFF;
		}
		return;
		case 1:	/*Data, and status register B*/
		if (!fdcmodel)  return 0x50;
		// if ((fdcmodel > 2) && (fdcmodel != 5))  return 0x50;		
		if (fdcmodel == 3)  return 0x50;
		if (fdcmodel == 6)  return 0x50;
		if ((fdcmodel == 2) || (fdcmodel == 4) || (fdcmodel == 5))
		{
			if (fdc.rconfig && (fdc.cr[imax][7] <= imax) && (fdc.ccr >= 48))  return fdc.cr[fdc.cr[imax][7]][fdc.ccr];
			if (fdc.rconfig && (fdc.ccr < 48))  return fdc.cr[imax][fdc.ccr];
			if (!fdc.rconfig)  return 0xFF;
		}
		else if (fdcmodel == 1)
		{
			if (fdc.rconfig && (fdc.ccr < 48))  return fdc.cr[0][fdc.ccr];
			if (!fdc.rconfig)  return 0xFF;
		}
		return 0;
		case 2:
		temp = fdc.dor;
		if (!AT)  temp = 0xFF;
		break;
                case 3:
		if (!AT)
		{
			temp = 0x20;
		}
		else
		{
			temp = fdc.tdr & 3;

			if (isos2mode())
			{
				temp |= ((getdrvtype() >> ((fdc.dor & 3) << 1)) & 3) << 4;
				temp |= (getbootdrive() << 2);
			}
			else
			{
				temp |= 0xFC;
			}
		}
                break;
                case 4: /*Status*/
		if(!fdc.pcjr)  fdc.dor |= 4;
		if(fdc.pcjr)  fdc.dor |= 0x80;
		temp=fdc.stat;
                break;
                case 5: /*Data*/
                fdc.stat&=~0x80;
                if ((fdc.stat & 0xf0) == 0xf0)
                {
                        // temp = fdc.dat;
			temp = fifo_buf_read();
			if (fdc.fifobufpos != 0)  fdc.stat |= 0x80;
                        break;
                }
                if (paramstogo)
                {
                        paramstogo--;
                        temp=fdc.res[10 - paramstogo];
//                        printf("Read param %i %02X\n",6-paramstogo,temp);
                        if (!paramstogo)
                        {
                                fdc.stat=0x80;
//                                fdc.st0=0;
                        }
                        else
                        {
                                fdc.stat|=0xC0;
//                                fdc_poll();
                        }
                }
                else
                {
                        if (lastbyte)
                           fdc.stat=0x80;
                        lastbyte=0;
                        temp=fdc.dat;
                }
                if (discint==0xA) 
		{
			timer_process();
			disctime = 1024 * (1 << TIMER_SHIFT);
			timer_update_outstanding();
		}
                fdc.stat &= 0xf0;
                break;
                case 7: /*Disk change*/
                if (fdc.dor & (0x10 << (fdc.dor & 1)))
                   temp = (discchanged[fdc.dor & 1] || driveempty[fdc.dor & 1])?0x80:0;
                else
                   temp = 0;
                if (AMSTRADIO)  /*PC2086/3086 seem to reverse this bit*/
                   temp ^= 0x80;
//                printf("- DC %i %02X %02X %i %i - ",fdc.dor & 1, fdc.dor, 0x10 << (fdc.dor & 1), discchanged[fdc.dor & 1], driveempty[fdc.dor & 1]);
//                discchanged[fdc.dor&1]=0;
                break;
                default:
                        temp=0xFF;
//                printf("Bad read FDC %04X\n",addr);
//                dumpregs();
//                exit(-1);
        }
//        /*if (addr!=0x3f4) */printf("%02X rate=%i\n",temp,fdc.rate);
        return temp;
}

static int fdc_reset_stat = 0;

int return_sstate()
{
	return sstat[fdc.drive][fdc.head[fdc.drive]][fdc.track[fdc.drive]][fdc.sector[fdc.drive] - 1];
}

int sds_match()
{
	int d = 0x40;
	int e = 0;
	if (fdc.deldata)  d = 0;
	e = (return_sstate()) & 0x40;
	return (d == e);
}

void fdc_readwrite(int mode)
{
	int i = 0;
	int quantity = 1;
	int mt = (fdc.command & 0x80);
	int mfm = (fdc.command & 0x40);
	int sk = (fdc.command & 0x20);
	int ec = (fdc.params[0] & 0x80);
	int maxs = 0;
	int sc = 0;
	int temp = 0;

	if (mode != 2)  ec = 0;

	if (mode == 0)  sk = 0;

	if (ec)  maxs = fdc.params[7];
	if (ec && !maxs)  maxs = 256;

	if (fdc.fifo)  quantity = fdc.tfifo;

	switch(mode)
	{
		case 0:
			if (fdc.deldata)
			{
				pclog("Write Deleted Data...\n");
			}
			else
			{
				pclog("Write Data...\n");
			}
			break;
		case 1:
			if (fdc.deldata)
			{
				pclog("Read Deleted Data...\n");
			}
			else
			{
				pclog("Read Data...\n");
			}
			break;
		case 2:
			pclog("Verify...\n");
			break;
		default:
			pclog("Unknown...\n");
			break;
	}

	if ((mode == 0) && WP[fdc.drive])
	{
		pclog("Disk is write protected!\n");
		disctime=0;
		discint=-2;
		fdc.stat=0xd0;		// We do need to allow to transfer our parameters to the host!
		fdc.st0=(fdc.head[fdc.drive]?4:0)|fdc.drive;
		fdc.st0 |= 0x20;
		fdc.st0|=0x40;
		fdc_int();
		fdc.res[4]=(fdc.head[fdc.drive]?4:0)|fdc.drive;
		fdc.res[4] |= 0x20;
		fdc.res[4] |= 0x40;
		fdc.st0=fdc.res[4];
		fdc.res[5]=2;
		fdc.res[6]=0;
		goto rw_result_phase_after_statuses;
	}
	if (mode == 0)
	{
		discmodified[fdc.drive] = 1;
	}

	if (!fdc.pos[fdc.drive])
	{
		// printf("Reading sector %i track %i side %i drive %i %02X to %05X %04X\n",fdc.sector[fdc.drive],fdc.track[fdc.drive],fdc.head[fdc.drive],fdc.drive,fdc.params[5],(dma.ac[2]+(dma.page[2]<<16))&rammask, dma.cc[2]);
	}

	if (fdc.pos[fdc.drive]<BPS[fdc.drive])
	{
		printf("%08X: ", (((fdc.track[fdc.drive]*SIDES[fdc.drive]*SECTORS[fdc.drive])+(fdc.head[fdc.drive]*SECTORS[fdc.drive])+(fdc.sector[fdc.drive]-1))*BPS[fdc.drive])+fdc.pos[fdc.drive]);
		for(i = 0; i < quantity; i++)
		{
			// if (fdc.abort)  goto rw_break;
			if (mode == 1)
			{
				if (!sk || sds_match())  fdc.dat = disc[fdc.drive][fdc.head[fdc.drive]][fdc.track[fdc.drive]][fdc.sector[fdc.drive] - 1][fdc.pos[fdc.drive]];
				printf("%02X ", fdc.dat);

				if (!sk)
				{
					if (!(sds_match()))  fdc.st2 |= 0x40;
				}
			}
			// printf("Read disc %i %i %i %i %02X\n",fdc.head[fdc.drive],fdc.track[fdc.drive],fdc.sector[fdc.drive],fdc.pos[fdc.drive],fdc.dat);
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
								fdc.abort = 1;
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
							fdc.abort = 1;
						if (temp == DMA_NODATA)
							fdc.abort = 1;
					}
				}
			}
			if (mode == 0)
			{
				if (temp != DMA_NODATA)
				{
					pclog("ND\n");
					disc[fdc.drive][fdc.head[fdc.drive]][fdc.track[fdc.drive]][fdc.sector[fdc.drive] - 1][fdc.pos[fdc.drive]] = fdc.dat;
				}
				printf("%02X ", fdc.dat);

				// Normal state is 0x43 actually
				if (fdc.deldata)
				{
					sstat[fdc.drive][fdc.head[fdc.drive]][fdc.track[fdc.drive]][fdc.sector[fdc.drive]-1] &= 0xBF;
				}
				else
				{
					sstat[fdc.drive][fdc.head[fdc.drive]][fdc.track[fdc.drive]][fdc.sector[fdc.drive]-1] |= 0x40;
				}
			}
                       	timer_process();
			disctime = 256 * (1 << TIMER_SHIFT);
                       	timer_update_outstanding();
			fdc.pos[fdc.drive]++;
			if(fdc.pos[fdc.drive] >= BPS[fdc.drive])
			{
				// We've gone beyond the sector
				fdc.pos[fdc.drive] -= BPS[fdc.drive];
				if (!sk || sds_match())  sstates[fdc.drive] += sector_state(mode);
				fdc.sector[fdc.drive]++;
				sc++;
				if ((fdc.sector[fdc.drive] > fdc.params[5]) && !ec)
				{
					// Sector is bigger than the limit
					fdc.sector[fdc.drive] = 1;
					if (mt)
					{
						// Multi-track mode
						pclog("\n[DC] Multi-track mode, switching heads\n");
						fdc.head[fdc.drive] ^= 1;
						if ((fdc.head[fdc.drive] == 0) || (SIDES[fdc.drive] == 1))
						{
							pclog("End of side %u reached, can go no further\n", SIDES[fdc.drive] - 1);
							fdc.pos[fdc.drive] = BPS[fdc.drive];
							fdc.abort = 1;
						}
					}
					else
					{
						pclog("\n[DC] Single-track mode\n");
						fdc.pos[fdc.drive] = BPS[fdc.drive];
						fdc.abort = 1;
					}
				}
				if ((fdc.sector[fdc.drive] > SECTORS[fdc.drive]) && ec)
				{
					// Sector is bigger than the limit
					fdc.sector[fdc.drive] = 1;
					// if (mt)
					// {
						// Multi-track mode
						pclog("\n[EC] Multi-track mode, switching heads\n");
						fdc.head[fdc.drive] ^= 1;
						if ((fdc.head[fdc.drive] == 0) || (SIDES[fdc.drive] == 1))
						{
							pclog("Verify with EC, increasing track number\n", SIDES[fdc.drive] - 1);
							fdc.track[fdc.drive]++;
							if (SIDES[fdc.drive] == 1)  fdc.head[fdc.drive] = 0;
							if (fdc.track[fdc.drive] >= TRACKS[fdc.drive])
							{
								pclog("Reached the end of the disk\n");
								fdc.track[fdc.drive] = TRACKS[fdc.drive];
								fdc.pos[fdc.drive] = BPS[fdc.drive];
								fdc.abort = 1;
							}
						}
					/* }
					else
					{
						pclog("\n[EC] Single-track mode\n");
						fdc.pos[fdc.drive] = BPS[fdc.drive];
						fdc.abort = 1;
					} */
				}
				if ((sc > maxs) && ec)
				{
					pclog("\n[EC] Verified all the sectors!\n");
					fdc.pos[fdc.drive] = BPS[fdc.drive];
					fdc.abort = 1;
				}
				if (fdc.abort)
				{
					goto rw_result_phase;
				}
			}
rw_break:
			printf("");
		}
		printf("OK\n", fdc.dat);
		return;
	}
	else
	{
rw_result_phase:
//		printf("End of command - params to go! %i %i %i\n", fdc.track[fdc.drive], fdc.head[fdc.drive], fdc.sector[fdc.drive]);
		fdc.abort = 0;
		disctime=0;
		discint=-2;
		fdc_int();
//		pclog("RD\n");
		fdc.stat=0xd0;
		fdc.res[4]=(fdc.head[fdc.drive]?4:0)|fdc.drive;
		if (sstates[fdc.drive])  fdc.res[4] |= 0x40;
		fdc.res[4] |= 0x20;
		fdc.st0=fdc.res[4];
		fdc.res[5]=fdc.st1;
		fdc.res[6]=fdc.st2;
rw_result_phase_after_statuses:
		fdc.st1 = 0;
		fdc.st2 = 0;
		fdc.res[7]=fdc.track[fdc.drive];
		fdc.res[8]=fdc.head[fdc.drive];
		fdc.res[9]=fdc.sector[fdc.drive];
		fdc.res[10]=fdc.params[4];
		// fdc.res[10] = BPSCODE[fdc.drive];
		if (mode == 0)  pclog("WD: ");
		if (mode == 1)  pclog("RD: ");
		if (mode == 2)  pclog("VD: ");
		pclog("%02X %02X %02X %02X %02X %02X %02X\n", fdc.res[4], fdc.res[5], fdc.res[6], fdc.res[7], fdc.res[8], fdc.res[9], fdc.res[10]);
		paramstogo=7;
		return;
		// Commented out because it's dead code that never gets called anyway
		/* switch (fdc.pos[fdc.drive]-BPS[fdc.drive])
		{
			case 0: case 1: case 2: fdc.dat=0; break;
			case 3: fdc.dat=fdc.track[fdc.drive]; break;
			case 4: fdc.dat=fdc.head[fdc.drive]; break;
			case 5: fdc.dat=fdc.sector[fdc.drive]; break;
			case 6: fdc.dat=BPSCODE[fdc.drive]; discint=-2; lastbyte=1; break;
		} */
	}
}

void scan_status(int d2, int d3)
{
	fdc.st2 &= 0xF3;
	fdc.st2 |= (d2 << 2);
	fdc.st2 |= (d3 << 3);
}

int check_d2()
{
	return ((fdc.st2 & 4) >> 2);
}

void scan_matrix(int scm)
{
	int r = memcmp(disc[fdc.drive][fdc.head[fdc.drive]][fdc.track[fdc.drive]][fdc.sector[fdc.drive] - 1], fdc.scanbuf[fdc.drive], fdc.scanpos[fdc.drive]);
	switch(scm)
	{
		case 0:
			if (r == 0)  scan_status(0, 1);
			if (r != 0)  scan_status(1, 0);
			break;
		case 1:
			if (r == 0)  scan_status(0, 1);
			if (r > 0)  scan_status(0, 0);
			if (r < 0)  scan_status(1, 0);
			break;
		case 2:
			if (r == 0)  scan_status(0, 1);
			if (r < 0)  scan_status(0, 0);
			if (r > 0)  scan_status(1, 0);
			break;
		default:
			scan_status(1, 0);
			break;
	}
}

void fdc_scan()
{
	int i = 0;
	int quantity = 1;
	int mt = (fdc.command & 0x80);
	int mfm = (fdc.command & 0x40);
	int sk = (fdc.command & 0x20);
	int maxs = 0;
	int sc = 0;
	int temp = 0;
	int scmode = 0;
	if ((fdc.command & 0x1F) == 0x19)  scmode = 1;
	if ((fdc.command & 0x1F) == 0x1D)  scmode = 2;

	if (fdc.fifo)  quantity = fdc.tfifo;

	if (discint == 0x11)
	{
		pclog("Scan Equal...\n");
	}
	else if (discint == 0x19)
	{
		pclog("Scan Low or Equal...\n");
	}
	else if (discint == 0x1D)
	{
		pclog("Scan High or Equal...\n");
	}
	else
	{
		pclog("Unknown Scan...\n");
	}

	if (!fdc.pos[fdc.drive])
	{
		// printf("Scanning sector %i track %i side %i drive %i %02X to %05X %04X\n",fdc.sector[fdc.drive],fdc.track[fdc.drive],fdc.head[fdc.drive],fdc.drive,fdc.params[5],(dma.ac[2]+(dma.page[2]<<16))&rammask, dma.cc[2]);
	}

	if (fdc.pos[fdc.drive]<BPS[fdc.drive])
	{
		printf("%08X: ", (((fdc.track[fdc.drive]*SIDES[fdc.drive]*SECTORS[fdc.drive])+(fdc.head[fdc.drive]*SECTORS[fdc.drive])+(fdc.sector[fdc.drive]-1))*BPS[fdc.drive])+fdc.pos[fdc.drive]);
		for(i = 0; i < quantity; i++)
		{
			// if (fdc.abort)  goto rw_break;
			if (!sk || sds_match())  fdc.dat = disc[fdc.drive][fdc.head[fdc.drive]][fdc.track[fdc.drive]][fdc.sector[fdc.drive] - 1][fdc.pos[fdc.drive]];
			printf("%02X ", fdc.dat);

			if (!sk)
			{
				if (!(sds_match()))  fdc.st2 |= 0x40;
			}
			// printf("Read disc %i %i %i %i %02X\n",fdc.head[fdc.drive],fdc.track[fdc.drive],fdc.sector[fdc.drive],fdc.pos[fdc.drive],fdc.dat);
			if (!fdc.gotdata[fdc.drive])
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
						fdc.gotdata[fdc.drive] = 1;
					if (temp == DMA_NODATA)
						fdc.gotdata[fdc.drive] = 1;
				}
				if (temp != DMA_NODATA)
				{
					pclog("ND\n");
					fdc.scanbuf[fdc.drive][fdc.scanpos[fdc.drive]++] = fdc.dat;
					printf("%02X ", fdc.dat);
				}
			}
			if (fdc.gotdata[fdc.drive])
			{
				scan_matrix(scmode);

				if (check_d2())  goto sc_result_phase;

                       		timer_process();
				disctime = 256 * (1 << TIMER_SHIFT);
                       		timer_update_outstanding();
				fdc.pos[fdc.drive]++;
				if(fdc.pos[fdc.drive] >= BPS[fdc.drive])
				{
					// We've gone beyond the sector
					fdc.pos[fdc.drive] -= BPS[fdc.drive];
					if (!sk || sds_match())  sstates[fdc.drive] += sector_state(1);
					fdc.sector[fdc.drive]++;
					sc++;
					if (fdc.sector[fdc.drive] > fdc.params[5])
					{
						// Sector is bigger than the limit
						fdc.sector[fdc.drive] = 1;
						if (mt)
						{
							// Multi-track mode
							pclog("\n[DC] Multi-track mode, switching heads\n");
							fdc.head[fdc.drive] ^= 1;
							if ((fdc.head[fdc.drive] == 0) || (SIDES[fdc.drive] == 1))
							{
								pclog("End of side %u reached, can go no further\n", SIDES[fdc.drive] - 1);
								fdc.pos[fdc.drive] = BPS[fdc.drive];
								fdc.abort = 1;
							}
						}
						else
						{
							pclog("\n[DC] Single-track mode\n");
							fdc.pos[fdc.drive] = BPS[fdc.drive];
							fdc.abort = 1;
						}
					}

					if (fdc.abort)  goto sc_result_phase;
				}
			}
sc_break:
			printf("");
		}
		printf("OK\n", fdc.dat);
		return;
	}
	else
	{
sc_result_phase:
//		printf("End of command - params to go! %i %i %i\n", fdc.track[fdc.drive], fdc.head[fdc.drive], fdc.sector[fdc.drive]);
		fdc.abort = 0;
		disctime=0;
		discint=-2;
		fdc_int();
//		pclog("RD\n");
		fdc.stat=0xd0;
		fdc.res[4]=(fdc.head[fdc.drive]?4:0)|fdc.drive;
		if (sstates[fdc.drive])  fdc.res[4] |= 0x40;
		fdc.res[4] |= 0x20;
		fdc.st0=fdc.res[4];
		fdc.res[5]=fdc.st1;
		fdc.res[6]=fdc.st2;
sc_result_phase_after_statuses:
		fdc.st1 = 0;
		fdc.st2 = 0;
		fdc.res[7]=fdc.track[fdc.drive];
		fdc.res[8]=fdc.head[fdc.drive];
		fdc.res[9]=fdc.sector[fdc.drive];
		fdc.res[10]=fdc.params[4];
		// fdc.res[10] = BPSCODE[fdc.drive];
		if (scmode == 0)  pclog("SE: ");
		if (scmode == 1)  pclog("SL: ");
		if (scmode == 2)  pclog("SH: ");
		pclog("%02X %02X %02X %02X %02X %02X %02X\n", fdc.res[4], fdc.res[5], fdc.res[6], fdc.res[7], fdc.res[8], fdc.res[9], fdc.res[10]);
		paramstogo=7;
		return;
	}
}

void fdc_poll()
{
        int temp;
	int a = 0;
	int n = 0;
        disctime = 0;
        pclog("fdc_poll %08X %i %02X\n", discint, fdc.drive, fdc.st0);
        switch (discint)
        {
                case -4: /*End of command with interrupt and no result phase*/
//                if (output) printf("EOC - interrupt!\n");
//pclog("EOC\n");
                fdc_int();
		fdc.stat = 0;
		return;
                case -3: /*End of command with interrupt*/
//                if (output) printf("EOC - interrupt!\n");
//pclog("EOC\n");
                fdc_int();
                case -2: /*End of command*/
		pclog("Result #4 is: %02X\n", fdc.res[4]);
                fdc.stat = (fdc.stat & 0xf) | 0x80;
                return;
                case -1: /*Reset*/
//pclog("Reset\n");
                fdc_int();
                fdc_reset_stat = 4;
                return;
                case 2: /*Read track*/
		printf("Read Track Side %i Track %i Sector %02X sector size %i end sector %02X %05X\n",fdc.head[fdc.drive],fdc.track[fdc.drive],fdc.sector[fdc.drive],fdc.params[4],fdc.params[5],(dma.page[2]<<16)+dma.ac[2]);
                if (!fdc.pos[fdc.drive])
                {
//                        printf("Read Track Side %i Track %i Sector %02X sector size %i end sector %02X %05X\n",fdc.head[fdc.drive],fdc.track[fdc.drive],fdc.sector[fdc.drive],fdc.params[4],fdc.params[5],(dma.page[2]<<16)+dma.ac[2]);
                }
                if (fdc.pos[fdc.drive]<BPS[fdc.drive])
                {
			if (!fdc.fifo)
			{
                        	fdc.dat=disc[fdc.drive][fdc.head[fdc.drive]][fdc.track[fdc.drive]][fdc.sector[fdc.drive]-1][fdc.pos[fdc.drive]];
//                        	pclog("Read %i %i %i %i %02X\n",fdc.head[fdc.drive],fdc.track[fdc.drive],fdc.sector[fdc.drive],fdc.pos[fdc.drive],fdc.dat);
                        	if (fdc.pcjr || !fdc.dma)
                                	fdc.stat = 0xf0;
                        	else
                                	dma_channel_write(2, fdc.dat);
                        	timer_process();
                        	disctime = 60 * (1 << TIMER_SHIFT);
                        	timer_update_outstanding();
			}
			else
			{
				for (a = 0; a < fdc.tfifo; a++)
				{
                        		fdc.dat=disc[fdc.drive][fdc.head[fdc.drive]][fdc.track[fdc.drive]][fdc.sector[fdc.drive]-1][fdc.pos[fdc.drive]+a];
//                        		pclog("Read %i %i %i %i %02X\n",fdc.head[fdc.drive],fdc.track[fdc.drive],fdc.sector[fdc.drive],fdc.pos[fdc.drive]+a,fdc.dat);
				}
                        	if (fdc.pcjr || !fdc.dma)
                                	fdc.stat = 0xf0;
                        	else
                                	dma_channel_write(2, fdc.dat);
                        	timer_process();
                        	disctime = 60 * (1 << TIMER_SHIFT);
                        	timer_update_outstanding();
			}
                }
                else
                {
                        disctime=0;
                        discint=-2;
//                        pclog("RT\n");
                        fdc_int();
                        fdc.stat=0xd0;
			fdc.res[4]=(fdc.head[fdc.drive]?4:0)|fdc.drive;
			if (sstates[fdc.drive])  fdc.res[4] |= 0x40;
			fdc.res[4] |= 0x20;
			fdc.st0=fdc.res[4];
                        fdc.res[5]=fdc.st1;
			fdc.res[6]=fdc.st2;
			pclog("st1: %02X, st2: %02X\n", fdc.st1, fdc.st2);
			fdc.st1 = 0;
			fdc.st2 = 0;
                        fdc.res[7]=fdc.track[fdc.drive];
                        fdc.res[8]=fdc.head[fdc.drive];
                        fdc.res[9]=fdc.sector[fdc.drive];
                        // fdc.res[10]=fdc.params[4];
			fdc.res[10] = BPSCODE[fdc.drive];
                        paramstogo=7;
                        return;
                }
		if (fdc.fifo)
		{
                	fdc.pos[fdc.drive]+=fdc.tfifo;
		}
		else
		{
                	fdc.pos[fdc.drive]++;
		}
		if (fdc.pos[fdc.drive]==BPS[fdc.drive])
		{
			sstates[fdc.drive] += sector_state(1);
		}
                if (fdc.pos[fdc.drive]==BPS[fdc.drive] && fdc.params[5]!=1)
                {
                        fdc.pos[fdc.drive]=0;
                        fdc.sector[fdc.drive]++;
                        if (fdc.sector[fdc.drive]==SECTORS[fdc.drive]+1)
                        {
                                fdc.sector[fdc.drive]=1;
                        }
                        fdc.params[5]--;
                }
                return;
                case 3: /*Specify*/
                fdc.stat=0x80;
                fdc.specify[0] = fdc.params[0];
                fdc.specify[1] = fdc.params[1];
		if (fdcmodel != 4)  fdc.dma = 1 - (fdc.specify[1] & 1);
		pclog("DMA: %02X, stat: %02X\n", fdc.dma, fdc.stat);
                return;
                case 4: /*Sense drive status*/
		fdc.res[10] = fdc.params[0] & 7;
		if (fdcmodel == 3)
		{
			if (SIDES[fdc.drive] == 2)  fdc.res[10] |= 8;
			if (!driveempty[fdc.drive])  fdc.res[10] |= 0x20;
		}
                if (fdcmodel != 3)  fdc.res[10] |= 0x28;
                if (fdc.track[fdc.drive] == 0) fdc.res[10] |= 0x10;
		if (WP[fdc.drive]) fdc.res[10] |= 0x40;
		// fdc.res[10] |= fdc.drive;
		pclog("SDS: %02X\n", fdc.res[10]);

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
                // fdc.track[fdc.drive]=0;
		fdc.track[fdc.drive] -= 79;
		if (fdc.track[fdc.drive] < 0)
		{
			fdc.track[fdc.drive] = 0;
		}
		if (fdc.track[fdc.drive] > 0)
		{
			fdc.st0 |= 0x10;
		}
		// fdc.sector[fdc.drive] = 1;
		// fdc.head[fdc.drive] = 0;
                if (!driveempty[fdc.dor & 1]) discchanged[fdc.dor & 1] = 0;
		fdc.st0=(fdc.head[fdc.drive]?4:0)|fdc.drive;
		fdc.st0 |= 0x20;
		// if (fdc.wrongrate)  fdc.st0 |= 0x40;
		// No result phase = no interrupt
                // discint=-3;
		discint=-3;
		fdc.stat = 0;
		// fdc_int();
                timer_process();
                disctime = 2048 * (1 << TIMER_SHIFT);
                timer_update_outstanding();
		pclog("Recalibrate drive %02X: %02X %02X\n", fdc.drive + 0x41, fdc.track[fdc.drive], fdc.st0);
//                printf("Recalibrate complete!\n");
		// This here is weird - why are we allowing the client to transfer data,
		// when we send none.
                // fdc.stat = 0x80 | (1 << fdc.drive);
                // fdc.stat = 0x30 | (1 << fdc.drive);
		// fdc.stat = (1 << fdc.drive);
                return;
                case 8: /*Sense interrupt status*/
                pclog("Sense interrupt status %i\n", fdc_reset_stat);
                
                fdc.dat = fdc.st0;

		// WTF is this
                if (fdc_reset_stat)
                {
			fdc.st0 &= 0xf8;
			fdc.st0 |= (4 - fdc_reset_stat);
			fdc.st0|=(fdc.head[4 - fdc_reset_stat]?4:0)|(4 - fdc_reset_stat);
                        fdc_reset_stat--;
                }

                fdc.stat    = (fdc.stat & 0xf) | 0xd0;
                fdc.res[9]  = fdc.st0;
                fdc.res[10] = fdc.track[fdc.drive];
		pclog("IS: %02X %02X\n", fdc.st0, fdc.track[fdc.drive]);
                if (!fdc_reset_stat) fdc.st0 = 0x80;
                // if (!fdc_reset_stat) fdc.st0 = 0xC0;

                paramstogo = 2;
                discint = 0;
                disctime = 0;
                return;
                case 10: /*Read sector ID*/
                disctime=0;
                discint=-2;
                fdc_int();
                fdc.stat=0xD0;
		fdc.res[4]=(fdc.head[fdc.drive]?4:0)|fdc.drive;
		sstates[fdc.drive] = sector_state(3);
		if (sstates[fdc.drive])  fdc.res[4] |= 0x40;
		fdc.res[4] |= 0x20;
		fdc.st0=fdc.res[4];
                fdc.res[5]=fdc.st1;
                fdc.res[6]=fdc.st2;
		pclog("ID: %02X %02X %02X ", fdc.res[4], fdc.st1, fdc.st2);
		fdc.st1=0;
		fdc.st2=0;
                fdc.res[7]=fdc.track[fdc.drive];
                fdc.res[8]=fdc.head[fdc.drive];
		// fdc.res[8]=(fdc.params[0] & 4) >> 2;
                fdc.res[9]=fdc.sector[fdc.drive];
		/* What params[4] when Read sector ID accepts no parameters... */
                // fdc.res[10]=fdc.params[4];
		fdc.res[10] = BPSCODE[fdc.drive];
		pclog("%02X %02X %02X %02X\n", fdc.res[7], fdc.res[8], fdc.res[9], fdc.res[10]);
                paramstogo=7;
                // fdc.sector[fdc.drive]++;
                // if (fdc.sector[fdc.drive]==SECTORS[fdc.drive]+1)
                /* if (fdc.sector[fdc.drive]>SECTORS[fdc.drive])
                   fdc.sector[fdc.drive]=1; */
                return;
		case 13: /*Format*/
		// pclog("Formatting!\n");
		// Bytes per sector
		// Sectors per cylinder (SPT = SPC / SIDES)
		// Gap 3
		// Filler bytes
		// C, H, R, N
		// Result: Status 0, 1, 2, Undefined x 4
		if (WP[fdc.drive])
		{
			// pclog("Write protected!\n");
                        disctime=0;
                        discint=-2;
			fdc.stat=0xd0;		// We do need to allow to transfer our parameters to the host!
			fdc.st0=(fdc.head[fdc.drive]?4:0)|fdc.drive;
			fdc.st0 |= 0x20;
			fdc.st0|=0x40;
                        fdc_int();
			fdc.res[4]=(fdc.head[fdc.drive]?4:0)|fdc.drive;
			fdc.res[4] |= 0x20;
			fdc.res[4] |= 0x40;
			fdc.st0=fdc.res[4];
                        fdc.st1=2;
                        fdc.st2=0;
			fdc.res[5]=fdc.st1;
			fdc.res[6]=fdc.st2;
			goto format_result_phase_after_statuses;
		}
		else
		{
                	discmodified[fdc.drive]=1;
		}
		printf("%c: Track: %02X\n", 0x41 + fdc.drive, fdc.track[fdc.drive]);
		if (!fdc.format_started[fdc.drive])
		{
			// pclog("FMTST [T%02X], PARMS: %02X %02X %02X %02X %02X\n", fdc.track[fdc.drive], fdc.params[0], fdc.params[1], fdc.params[2], fdc.params[3], fdc.params[4]);
			BPS[fdc.drive]=128 << fdc.params[1];
			BPSCODE[fdc.drive]=fdc.params[1];
			SECTORS[fdc.drive]=fdc.params[2];		
			if (fdc.command & 0x40)  SIDES[fdc.drive]=2;
			if (!(fdc.command & 0x40))  SIDES[fdc.drive]=1;
			// printf("%02X SPT, %02X H\n", SECTORS[fdc.drive], SIDES[fdc.drive]);
			fdc.eot[fdc.drive] = SECTORS[fdc.drive];
			// Yikes, this should be 1-based!
			fdc.sector[fdc.drive] = 1;
			fdc.pos[fdc.drive] = 0;
			fdc.head[fdc.drive] = (fdc.params[0] & 4) >> 2;
			sectors_formatted[fdc.drive] = 0;
			OLD_BPS = fdc.params[1];
			OLD_SPC = fdc.params[2];
			OLD_C = -1;
			fdc.fillbyte[fdc.drive] = fdc.params[4];
			fdc.format_started[fdc.drive] = 1;
			timer_process();
			disctime = 600 * (1 << TIMER_SHIFT);
			timer_update_outstanding();
			fdc.fdmaread[fdc.drive] = 0;
			sstates[fdc.drive] = 0;

			// pclog("FMTP0C\n");
			timer_process();
			disctime = 600 * (1 << TIMER_SHIFT);
			timer_update_outstanding();
			if ((fdc.pcjr || !fdc.dma) && (fdc.pos[fdc.drive] <= BPS[fdc.drive]))  fdc.stat = 0xb0;
			if ((!fdc.pcjr && fdc.dma) && (fdc.pos[fdc.drive] <= BPS[fdc.drive]))  fdc.stat = 0x90;
			return;
		}
		else
		{
			// pclog("FMT %c\n", 0x41 + fdc.drive);
			// After formatting every last sector, sectors_formatted[fdc.drive] will be > SECTORS[fdc.drive]
			if (sectors_formatted[fdc.drive] < SECTORS[fdc.drive])
			{
				if (fdc.fifo)
				{
					for (a = 0; a < fdc.tfifo; a++)
					{
						if (a < 4)
						{
							if (fdc.fdmaread[fdc.drive] == 0)  pclog("SST %02X %02X\n", sstates[fdc.drive], fdc.sector[fdc.drive]);
                        				if (fdc.pcjr || !fdc.dma)
                                				fdc.params[5 + a] = fdc.dat;
                        				else
                                				fdc.params[5 + a] = dma_channel_read(2);
						}
					}
				}
				else
				{
					if (fdc.fdmaread[fdc.drive] == 0)  pclog("SST %02X %02X\n", sstates[fdc.drive], fdc.sector[fdc.drive]);
                        		if (fdc.pcjr || !fdc.dma)
                                		if (fdc.fdmaread[fdc.drive] < 4)  fdc.params[5 + fdc.fdmaread[fdc.drive]] = fdc.dat;
                        		else
                                		if (fdc.fdmaread[fdc.drive] < 4)  fdc.params[5 + fdc.fdmaread[fdc.drive]] = dma_channel_read(2);
					// WTF?! No wonder the information was wrong...
					// if (fdc.fdmaread[fdc.drive] < 4)  fdc.params[5 + fdc.fdmaread[fdc.drive]] = fdc.dat;
				}

                        	if (fdc.params[5 + fdc.fdmaread[fdc.drive]] == DMA_NODATA)
                        	{
					// pclog("DMAND\n");
                                	discint=0xFD;
                                	timer_process();
                                	disctime = 50 * (1 << TIMER_SHIFT);
                                	timer_update_outstanding();
                                	return;
                        	}
				else
				{
					if (fdc.fifo)
					{
						fdc.fdmaread[fdc.drive]+=fdc.tfifo;
					}
					else
					{
						fdc.fdmaread[fdc.drive]++;
					}
					if (fdc.fdmaread[fdc.drive] < 4)
					{
						timer_process();
						disctime = 600 * (1 << TIMER_SHIFT);
                               			timer_update_outstanding();
                               			if ((fdc.pcjr || !fdc.dma) && fdc.fdmaread[fdc.drive] != 4)  fdc.stat = 0xb0;
                               			if ((!fdc.pcjr && fdc.dma) && fdc.fdmaread[fdc.drive] != 4)  fdc.stat = 0x90;
					}
					else
					{
						// pclog("FDMA %02X\n", fdc.params[8]);
						timer_process();
						disctime = 2048 * (1 << TIMER_SHIFT);
                               			timer_update_outstanding();
						fdc_format();
						if (sectors_formatted[fdc.drive] < SECTORS[fdc.drive])
						{
							// pclog("CFMT\n");
							timer_process();
							disctime = 2048 * (1 << TIMER_SHIFT);
                               				timer_update_outstanding();
							fdc.fdmaread[fdc.drive] = 0;
							if ((fdc.pcjr || !fdc.dma) && (sectors_formatted[fdc.drive] < SECTORS[fdc.drive]))  fdc.stat = 0xb0;
							if ((!fdc.pcjr && fdc.dma) && (sectors_formatted[fdc.drive] < SECTORS[fdc.drive]))  fdc.stat = 0x90;
						}
					}
				}
			}
			else
			{
				/* Format is finished! */
				// pclog("Format finished!\n");
                        	disctime=0;
                        	discint=-2;
//                        	pclog("FO\n");
                        	fdc_int();
                        	fdc.stat=0xd0;
				fdc.res[4]=(fdc.head[fdc.drive]?4:0)|fdc.drive;
				if (sstates[fdc.drive])  fdc.res[4] |= 0x40;
				fdc.res[4] |= 0x20;
				fdc.st0=fdc.res[4];
                        	fdc.res[5]=fdc.st1;
                        	fdc.res[6]=fdc.st2;
format_result_phase_after_statuses:
				pclog("F: %02X %02X %02X\n", fdc.res[4], fdc.st1, fdc.st2);
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
                return;
                case 15: /*Seek*/
                if (!fdc.relative)  fdc.track[fdc.drive]=fdc.params[1];
                if (!driveempty[fdc.dor & 1]) discchanged[fdc.dor & 1] = 0;
		fdc.head[fdc.drive] = (fdc.params[0] & 4) >> 2;
               	fdc.st0=0x20|fdc.drive|(fdc.head[fdc.drive]?4:0);
		if (fdc.relative)
		{
			if (fdc.direction)  fdc.track[fdc.drive] -= fdc.params[1];
			if (!fdc.direction)  fdc.track[fdc.drive] += fdc.params[1];
			if (fdc.track[fdc.drive] >= TRACKS[fdc.drive])
			{
				fdc.track[fdc.drive] = TRACKS[fdc.drive] - 1;
			}
			if (fdc.track[fdc.drive] < 0)
			{
				fdc.st0 |= 0x50;
				fdc.track[fdc.drive] += 256;
			}
			fdc.track[fdc.drive] &= 0xFF;
		}
                printf("Seeked to track %i %i\n",fdc.track[fdc.drive], fdc.drive);
		// fdc.sector[fdc.drive] = 1;
		// fdc.pos[fdc.drive] = 0;
		// No result phase = no interrupt
                // discint=-3;
		discint=-3;
		// fdc_int();
                timer_process();
		disctime = 2048 * (1 << TIMER_SHIFT);
                timer_update_outstanding();
                // fdc.stat = 0x80 | (1 << fdc.drive);
                // fdc.stat = 0x30 | (1 << fdc.drive);
		// fdc.stat = (1 << fdc.drive);
		fdc.stat = 0;
//                pclog("Stat %02X ST0 %02X\n", fdc.stat, fdc.st0);
                return;
		case 0x11:
		case 0x19:
		case 0x1D:
		fdc_scan();
		return;
                case 0x0e: /*Dump registers*/
                fdc.stat = (fdc.stat & 0xf) | 0xd0;
                fdc.res[3] = fdc.track[0];
                fdc.res[4] = fdc.track[1];
                fdc.res[5] = 0;
                fdc.res[6] = 0;
                fdc.res[7] = fdc.specify[0];
                fdc.res[8] = fdc.specify[1];
                fdc.res[9] = fdc.eot[fdc.drive];
                fdc.res[10] = (fdc.perp & 0x7f) | ((fdc.lock) ? 0x80 : 0);
                paramstogo=10;
                discint=0;
                disctime=0;
                return;

                case 0x10: /*Version*/
                fdc.stat = (fdc.stat & 0xf) | 0xd0;
		if (fdcmodel == 6)
		{
                	fdc.res[10] = 0x80;
		}
		else
		{
                	fdc.res[10] = 0x90;
		}
                paramstogo=1;
                discint=0;
                disctime=0;
                return;
                
                case 0x12:
                fdc.perp = fdc.params[0];
                fdc.stat = 0x80;
                disctime = 0;
//                fdc_int();
                return;
                case 0x13: /*Configure*/
                fdc.config = fdc.params[1];
                fdc.pretrk = fdc.params[2];
		if (fdc.params[1] & 0x20)  fdc.fifo = 0;
		if (!(fdc.params[1] & 0x20))  fdc.fifo = 1;
		fdc.tfifo = (fdc.params[1] & 0xF) + 1;
		pclog("FIFO is now %02X, FIFO threshold is %02X\n", fdc.fifo, fdc.tfifo);
                fdc.stat = 0x80;
                disctime = 0;
//                fdc_int();
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
                // pclog("Inv!\n");
                //fdc_int();
                fdc.stat = (fdc.stat & 0xf) | 0xd0;
//                fdc.stat|=0xC0;
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
                // pclog("DMA Aborted\n");
                disctime=0;
                discint=-2;
                fdc_int();
                fdc.stat=0xd0;
		fdc.res[4]=(fdc.head[fdc.drive]?4:0)|fdc.drive;
		fdc.st0=fdc.res[4];
                fdc.res[5]=0;
                fdc.res[6]=0;
                fdc.res[7]=fdc.track[fdc.drive];
                fdc.res[8]=fdc.head[fdc.drive];
                fdc.res[9]=fdc.sector[fdc.drive];
                fdc.res[10]=fdc.params[4];
                paramstogo=7;
                return;
                case 0xFE: /*Drive empty*/
                pclog("Drive empty\n");
                fdc.stat = 0x10;
                disctime = 0;
/*                disctime=0;
                discint=-2;
                fdc_int();
                fdc.stat=0xd0;
		fdc.res[4]=(fdc.head[fdc.drive]?4:0)|fdc.drive;
		fdc.res[4]|=0xC8;
		fdc.st0=fdc.res[4];
                fdc.res[5]=0;
                fdc.res[6]=0;
                fdc.res[7]=0;
                fdc.res[8]=0;
                fdc.res[9]=0;
                fdc.res[10]=0;
                paramstogo=7;*/
                return;
                case 0xFF: /*Wrong rate*/
                // pclog("Wrong rate\n");
		pclog("Wrong rate ex %i %i\n",fdc.rate,discrate[fdc.drive]);
                fdc.stat = 0x10;
               	disctime=0;
               	discint=-2;
               	fdc_int();
                fdc.stat=0xd0;
		fdc.res[4]=(fdc.head[fdc.drive]?4:0)|fdc.drive;
		fdc.res[4]|=0x40;
		fdc.st0=fdc.res[4];
               	fdc.res[5]=5;
               	fdc.res[6]=0;
               	fdc.res[7]=0;
               	fdc.res[8]=0;
               	fdc.res[9]=0;
               	fdc.res[10]=0;
		pclog("Wrong rate: %02X %02X %02X %02X %02X %02X %02X\n", fdc.res[4], fdc.res[5], fdc.res[6], fdc.res[7], fdc.res[8], fdc.res[9], fdc.res[10]);
               	paramstogo=7;
                return;

                case 0x100: /*Sector too big or no seek enabled*/
                pclog("Drive empty\n");
                fdc.stat = 0x10;
                disctime=0;
                discint=-2;
                fdc_int();
                fdc.stat=0xd0;
		fdc.res[4]=(fdc.head[fdc.drive]?4:0)|fdc.drive;
		fdc.res[4]|=0x40;
		fdc.st0=fdc.res[4];
                fdc.res[5]=0;
                fdc.res[6]=0;
                fdc.res[7]=0;
                fdc.res[8]=0;
                fdc.res[9]=0;
                fdc.res[10]=0;
                paramstogo=7;
                return;

                case 0x101: /*Track too big*/
                pclog("Drive empty\n");
                fdc.stat = 0x10;
                disctime=0;
                discint=-2;
                fdc_int();
                fdc.stat=0xd0;
		fdc.res[4]=(fdc.head[fdc.drive]?4:0)|fdc.drive;
		fdc.res[4]|=0x40;
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
		pclog("Unknown discint %08X issued\n", discint);
		return;
        }
        printf("Bad FDC disc int %i\n",discint);
//        dumpregs();
//        exit(-1);
}

void fdc_init()
{
	timer_add(fdc_poll, &disctime, &disctime, NULL);
	config_default();
}

void fdc_add()
{
        io_sethandler(0x03f0, 0x0006, fdc_read, NULL, NULL, fdc_write, NULL, NULL, NULL);
        io_sethandler(0x03f7, 0x0001, fdc_read, NULL, NULL, fdc_write, NULL, NULL, NULL);
	if((fdcmodel == 3) || (fdcmodel == 4))
	{
        	io_sethandler(0x002e, 0x0002, ite_config_read, NULL, NULL, ite_config_write, NULL, NULL, NULL);
        	io_sethandler(0x004e, 0x0002, ite_config_read, NULL, NULL, ite_config_write, NULL, NULL, NULL);
	}
        fdc.pcjr = 0;
}

void fdc_add_pcjr()
{
        io_sethandler(0x00f0, 0x0006, fdc_read, NULL, NULL, fdc_write, NULL, NULL, NULL);
	timer_add(fdc_watchdog_poll, &fdc.watchdog_timer, &fdc.watchdog_timer, &fdc);
        fdc.pcjr = 1;
}

void fdc_remove()
{
        io_removehandler(0x03f0, 0x0006, fdc_read, NULL, NULL, fdc_write, NULL, NULL, NULL);
        if (!fdc.pcjr)  io_removehandler(0x03f7, 0x0001, fdc_read, NULL, NULL, fdc_write, NULL, NULL, NULL);        
	if(!fdc.pcjr && ((fdcmodel == 3) || (fdcmodel == 4)))
	{
        	io_removehandler(0x002e, 0x0002, ite_config_read, NULL, NULL, ite_config_write, NULL, NULL, NULL);
        	io_removehandler(0x004e, 0x0002, ite_config_read, NULL, NULL, ite_config_write, NULL, NULL, NULL);
	}
}

void fdc_change(int val)
{
	fdc_remove();
	fdcmodel = val;
	fdc_add();
	fdc_init();
}

void fdc_change_pcjr(int val)
{
	fdc_remove();
	fdcmodel = val;
	fdc_add_pcjr();
	fdc_init();
}

int fdc_model()
{
	return fdcmodel;
}

void fdc_setmodel(int val)
{
	fdcmodel = val;
}