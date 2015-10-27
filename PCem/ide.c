/*RPCemu v0.6 by Tom Walker
  IDE emulation*/
//#define RPCEMU_IDE

#define IDE_TIME (5 * 100 * (1 << TIMER_SHIFT) * 3)

#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <sys/types.h>

#ifdef RPCEMU_IDE
        #include "rpcemu.h"
        #include "iomd.h"
        #include "arm.h"
#else
        #include "ibm.h"
        #include "io.h"
        #include "pic.h"
        #include "timer.h"
#endif
#include "scsi_cmds.h"
#include "ide.h"

/* Bits of 'atastat' */
#define ERR_STAT		0x01
#define DRQ_STAT		0x08 /* Data request */
#define DSC_STAT                0x10
#define SERVICE_STAT            0x10
#define READY_STAT		0x40
#define BUSY_STAT		0x80

/* Bits of 'error' */
#define ABRT_ERR		0x04 /* Command aborted */
#define MCR_ERR			0x08 /* Media change request */

/* ATA Commands */
#define WIN_SRST			0x08 /* ATAPI Device Reset */
#define WIN_RECAL			0x10
#define WIN_RESTORE			WIN_RECAL
#define WIN_READ			0x20 /* 28-Bit Read */
#define WIN_READ_NORETRY                0x21 /* 28-Bit Read - no retry*/
#define WIN_WRITE			0x30 /* 28-Bit Write */
#define WIN_WRITE_NORETRY		0x31 /* 28-Bit Write */
#define WIN_VERIFY			0x40 /* 28-Bit Verify */
#define WIN_FORMAT			0x50
#define WIN_SEEK			0x70
#define WIN_DRIVE_DIAGNOSTICS           0x90 /* Execute Drive Diagnostics */
#define WIN_SPECIFY			0x91 /* Initialize Drive Parameters */
#define WIN_PACKETCMD			0xA0 /* Send a packet command. */
#define WIN_PIDENTIFY			0xA1 /* Identify ATAPI device */
#define WIN_READ_MULTIPLE               0xC4
#define WIN_WRITE_MULTIPLE              0xC5
#define WIN_SET_MULTIPLE_MODE           0xC6
#define WIN_READ_DMA                    0xC8
#define WIN_WRITE_DMA                   0xCA
#define WIN_SETIDLE1			0xE3
#define WIN_IDENTIFY			0xEC /* Ask drive to identify itself */

/* ATAPI commands and sense/asc/ascq stuff is on scsi_cmds.h */
uint8_t atapi_cmd_table[0x100] = {
	[ 0x00 ] = CHECK_READY,		/* Test unit ready */
	[ 0x03 ] = ALLOW_UA,		/* Request sense */
	[ 0x08 ] = CHECK_READY,		/* Read (6) */
	[ 0x15 ] = 0,			/* Mode select (6) */
	[ 0x12 ] = ALLOW_UA,		/* Inquiry */
	[ 0x1A ] = 0,			/* Mode sense (6) */
	[ 0x1B ] = 0,			/* Start/stop or Load/unload */
	[ 0x1E ] = 0,			/* Allow medium removal */
	[ 0x25 ] = CHECK_READY,		/* Read capacity (10) */
	[ 0x28 ] = CHECK_READY,		/* Read (10) */
	[ 0x2B ] = CHECK_READY,		/* Seek (10) */
	[ 0x42 ] = CHECK_READY,		/* Read subchannel */
	[ 0x43 ] = CHECK_READY,		/* Read TOC */
	[ 0x44 ] = CHECK_READY,		/* Read header */
	[ 0x45 ] = CHECK_READY,		/* Play audio (10) */
	[ 0x46 ] = ALLOW_UA,		/* Get configuration */
	[ 0x47 ] = CHECK_READY,		/* Play audio (MSF) */
	[ 0x4A ] = ALLOW_UA,		/* Get event status notification */
	[ 0x4B ] = CHECK_READY,		/* Pause/resume */
	[ 0x4E ] = CHECK_READY,		/* Stop play/scan */
	[ 0x51 ] = CHECK_READY,		/* Read disc information */
	[ 0x55 ] = 0,			/* Mode select (10) */
	[ 0x5A ] = 0,			/* Mode sense (10) */
	[ 0xA5 ] = CHECK_READY,		/* Play audio (12) */
	[ 0xA8 ] = CHECK_READY,		/* Read (12)  */
	[ 0xAD ] = CHECK_READY,		/* Read DVD structure (NOT IMPLEMENTED YET) */
	[ 0xBB ] = 0,			/* Set CD speed */
	[ 0xBD ] = 0,			/* Mechanism status */
	[ 0xBE ] = 0,			/* Read CD */
	[ 0xBF ] = CHECK_READY, };	/* Send DVD structure (NOT IMPLEMENTED YET) */

struct
{
        int sensekey,asc,ascq;
} atapi_sense;

int atapi_command = 0;

/* Tell RISC OS that we have a 4x CD-ROM drive (600kb/sec data, 706kb/sec raw).
   Not that it means anything */
#define CDROM_SPEED	706

/** Evaluate to non-zero if the currently selected drive is an ATAPI device */
#define IDE_DRIVE_IS_CDROM(ide)  (ide->type == IDE_CDROM)
/*
\
	(!ide.drive)*/

ATAPI *atapi;

int cur_board;

enum
{
        IDE_NONE = 0,
        IDE_HDD,
        IDE_CDROM
};

typedef struct IDE
{
        int type;
        int board;
        uint8_t atastat;
        uint8_t error;
        int secount,sector,cylinder,head,drive,cylprecomp;
        uint8_t command;
        uint8_t fdisk;
        int pos;
        int packlen;
        int spt,hpc;
        int tracks;
        int packetstatus;
        int cdpos,cdlen;
        uint8_t asc;
        int discchanged;
        int reset;
        FILE *hdfile;
        uint16_t buffer[65536];
        int irqstat;
        int service;
        int lba;
        uint32_t lba_addr;
        int skip512;
        int blocksize, blockcount;
} IDE;

IDE ide_drives[4];

IDE *ext_ide;

char ide_fn[4][512];

int (*ide_bus_master_read_sector)(int channel, uint8_t *data);
int (*ide_bus_master_write_sector)(int channel, uint8_t *data);
void (*ide_bus_master_set_irq)(int channel);

static void callreadcd(IDE *ide);
static uint8_t atapi_check_ready(int ide_board);
static void atapicommand(int ide_board);

int idecallback[2] = {0, 0};

int cur_ide[2];

int ide_map[4] = {0, 0, 0, 0};

uint8_t getstat(IDE *ide) { return ide->atastat; }

static inline void ide_irq_raise(IDE *ide)
{
//        pclog("IDE_IRQ_RAISE\n");
	if (!(ide->fdisk&2)) {
#ifdef RPCEMU_IDE
		iomd.irqb.status |= IOMD_IRQB_IDE;
		updateirqs();
#else
                // if (ide->board && !ide->irqstat) pclog("IDE_IRQ_RAISE\n");
                picint((ide->board)?(1<<15):(1<<14));
                if (ide_bus_master_set_irq)
                        ide_bus_master_set_irq(ide->board);
#endif
	}
	ide->irqstat=1;
        ide->service=1;
}

static inline void ide_irq_lower(IDE *ide)
{
//        pclog("IDE_IRQ_LOWER\n");
//	if (ide.board == 0) {
#ifdef RPCEMU_IDE
		iomd.irqb.status &= ~IOMD_IRQB_IDE;
		updateirqs();
#else
                picintc((ide->board)?(1<<15):(1<<14));
#endif
//	}
	ide->irqstat=0;
}

void ide_irq_update(IDE *ide)
{
#ifdef RPCEMU_IDE
	if (ide->irqstat && !(iomd.irqb.status & IOMD_IRQB_IDE) && !(ide->fdisk & 2)) {
		iomd.irqb.status |= IOMD_IRQB_IDE;
		updateirqs();
        }
        else if (iomd.irqb.status & IOMD_IRQB_IDE)
        {
		iomd.irqb.status &= ~IOMD_IRQB_IDE;
		updateirqs();
        }
#else
	if (ide->irqstat && !((pic2.pend|pic2.ins)&0x40) && !(ide->fdisk & 2))
            picint((ide->board)?(1<<15):(1<<14));
        else if ((pic2.pend|pic2.ins)&0x40)
            picintc((ide->board)?(1<<15):(1<<14));
#endif
}
/**
 * Copy a string into a buffer, padding with spaces, and placing characters as
 * if they were packed into 16-bit values, stored little-endian.
 *
 * @param str Destination buffer
 * @param src Source string
 * @param len Length of destination buffer to fill in. Strings shorter than
 *            this length will be padded with spaces.
 */
static void
ide_padstr(char *str, const char *src, int len)
{
	int i, v;

	for (i = 0; i < len; i++) {
		if (*src != '\0') {
			v = *src++;
		} else {
			v = ' ';
		}
		str[i ^ 1] = v;
	}
}

/**
 * Copy a string into a buffer, padding with spaces. Does not add string
 * terminator.
 *
 * @param buf      Destination buffer
 * @param buf_size Size of destination buffer to fill in. Strings shorter than
 *                 this length will be padded with spaces.
 * @param src      Source string
 */
static void
ide_padstr8(uint8_t *buf, int buf_size, const char *src)
{
	int i;

	for (i = 0; i < buf_size; i++) {
		if (*src != '\0') {
			buf[i] = *src++;
		} else {
			buf[i] = ' ';
		}
	}
}

/**
 * Fill in ide->buffer with the output of the "IDENTIFY DEVICE" command
 */
static void ide_identify(IDE *ide)
{
	memset(ide->buffer, 0, 512);

	//ide->buffer[1] = 101; /* Cylinders */

#ifdef RPCEMU_IDE
	ide->buffer[1] = 65535; /* Cylinders */
	ide->buffer[3] = 16;  /* Heads */
	ide->buffer[6] = 63;  /* Sectors */
#else
	ide->buffer[0] = 0x095A;
	// pclog("Device %i/%i (%i) IDENTIFY\n", cur_ide[cur_board], cur_board, ide_drives[cur_ide[cur_board]].type);
	if (ide_drives[cur_ide[cur_board]].type == IDE_CDROM)
	{
		// pclog("Device %i is CD-ROM (%i)\n", cur_ide[cur_board], ide_drives[cur_ide[cur_board]].type);
		ide->buffer[0] = 0x099A;
	}

	if (ide_map[cur_ide[cur_board]] != -1)
	{
		ide->buffer[1] = hdc[ide_map[cur_ide[cur_board]]].tracks; /* Cylinders */
		ide->buffer[3] = hdc[ide_map[cur_ide[cur_board]]].hpc;  /* Heads */
		ide->buffer[6] = hdc[ide_map[cur_ide[cur_board]]].spt;  /* Sectors */
	}
	else
	{
		ide->buffer[1] = 0; /* Cylinders */
		ide->buffer[3] = 0;  /* Heads */
		ide->buffer[6] = 0;  /* Sectors */
	}
#endif
#ifdef RPCEMU_IDE
	ide_padstr((char *) (ide->buffer + 27), "RPCemuHD", 40); /* Model */
#else
	// pclog("wackek: cur_ide is %i\n", cur_ide[cur_board]);
	switch(ide_map[cur_ide[cur_board]])
	{
		case 0:
			ide_padstr((char *) (ide->buffer + 10), "K3048E9A", 20); /* Serial Number */
			ide_padstr((char *) (ide->buffer + 23), "B 12", 8); /* Firmware */
			ide_padstr((char *) (ide->buffer + 27), "90432D3", 40); /* Model */
			break;
		case 1:
			ide_padstr((char *) (ide->buffer + 10), "VH73P0C00T7A", 20); /* Serial Number */
			ide_padstr((char *) (ide->buffer + 23), "CA9-80B5", 8); /* Firmware */
			ide_padstr((char *) (ide->buffer + 27), "MPG3204AT", 40); /* Model */
			break;
		case 2:
			ide_padstr((char *) (ide->buffer + 10), "YKMD1234", 20); /* Serial Number */
			ide_padstr((char *) (ide->buffer + 23), "E182115", 8); /* Firmware */
			ide_padstr((char *) (ide->buffer + 27), "DTLA-307030", 40); /* Model */
			break;
		case 3:
			ide_padstr((char *) (ide->buffer + 10), "5FG0M7RZ", 20); /* Serial Number */
			ide_padstr((char *) (ide->buffer + 23), "3.60", 8); /* Firmware */
			ide_padstr((char *) (ide->buffer + 27), "ST320410A", 40); /* Model */
			break;
	}

	// ide_padstr((char *) (ide->buffer + 27), "PCemHD", 40); /* Model */
#endif
        ide->buffer[20] = 3;   /*Buffer type*/
        ide->buffer[21] = 512; /*Buffer size*/
        ide->buffer[47] = 16;  /*Max sectors on multiple transfer command*/
        ide->buffer[48] = 1;   /*Dword transfers supported*/
	ide->buffer[51] = 2 << 8; /*PIO timing mode*/
	ide->buffer[59] = ide->blocksize ? (ide->blocksize | 0x100) : 0;
	ide->buffer[49] = (1 << 9) | (1 << 8); /* LBA and DMA supported */
	ide->buffer[50] = 0x4000; /* Capabilities */
	ide->buffer[52] = 2 << 8; /*DMA timing mode*/
#ifdef RPCEMU_IDE
	ide->buffer[60] = (65535 * 16 * 63) & 0xFFFF; /* Total addressable sectors (LBA) */
	ide->buffer[61] = (65535 * 16 * 63) >> 16;
#else
	if (ide_map[cur_ide[cur_board]] != -1)
	{
		ide->buffer[60] = (hdc[ide_map[cur_ide[cur_board]]].tracks * hdc[ide_map[cur_ide[cur_board]]].hpc * hdc[ide_map[cur_ide[cur_board]]].spt) & 0xFFFF; /* Total addressable sectors (LBA) */
		ide->buffer[61] = (hdc[ide_map[cur_ide[cur_board]]].tracks * hdc[ide_map[cur_ide[cur_board]]].hpc * hdc[ide_map[cur_ide[cur_board]]].spt) >> 16;
	}
#endif
	ide->buffer[63] = 7; /*Multiword DMA*/
	ide->buffer[80] = 0xe; /*ATA-1 to ATA-3 supported*/
}

/**
 * Fill in ide->buffer with the output of the "IDENTIFY PACKET DEVICE" command
 */
static void ide_atapi_identify(IDE *ide)
{
	// pclog("ATAPI IDENTIFY issued\n");

	memset(ide->buffer, 0, 512);

	ide->buffer[0] = 0x8000 | (5<<8) | 0x80 | (2<<5); /* ATAPI device, CD-ROM drive, removable media, accelerated DRQ */
	// ide_padstr((char *) (ide->buffer + 10), "", 20); /* Serial Number */
	ide_padstr((char *) (ide->buffer + 23), "T01A", 8); /* Firmware */
#ifdef RPCEMU_IDE
	ide_padstr((char *) (ide->buffer + 27), "RPCemuCD", 40); /* Model */
#else
	ide_padstr((char *) (ide->buffer + 27), "CD-ROM SR244W   ", 40); /* Model */
#endif
	ide->buffer[49] = 0x200; /* LBA supported */
}

/**
 * Fill in ide->buffer with the output of the ATAPI "MODE SENSE" command
 *
 * @param pos Offset within the buffer to start filling in data
 *
 * @return Offset within the buffer after the end of the data
 */
static uint32_t ide_atapi_mode_sense(IDE *ide, uint32_t pos, uint8_t type)
{
	uint8_t *buf = (uint8_t *) ide->buffer;
//        pclog("ide_atapi_mode_sense %02X\n",type);
        if (type==GPMODE_ALL_PAGES || type==GPMODE_R_W_ERROR_PAGE)
        {
        	/* &01 - Read error recovery */
        	buf[pos++] = GPMODE_R_W_ERROR_PAGE;
        	buf[pos++] = 6; /* Page length */
        	buf[pos++] = 0; /* Error recovery parameters */
        	buf[pos++] = 3; /* Read retry count */
        	buf[pos++] = 0; /* Reserved */
        	buf[pos++] = 0; /* Reserved */
        	buf[pos++] = 0; /* Reserved */
        	buf[pos++] = 0; /* Reserved */
        }

        if (type==GPMODE_ALL_PAGES || type==GPMODE_CDROM_PAGE)
        {
        	/* &0D - CD-ROM Parameters */
        	buf[pos++] = GPMODE_CDROM_PAGE;
        	buf[pos++] = 6; /* Page length */
        	buf[pos++] = 0; /* Reserved */
        	buf[pos++] = 1; /* Inactivity time multiplier *NEEDED BY RISCOS* value is a guess */
        	buf[pos++] = 0; buf[pos++] = 60; /* MSF settings */
        	buf[pos++] = 0; buf[pos++] = 75; /* MSF settings */
        }

        if (type==GPMODE_ALL_PAGES || type==GPMODE_CDROM_AUDIO_PAGE)
        {
        	/* &0e - CD-ROM Audio Control Parameters */
        	buf[pos++] = GPMODE_CDROM_AUDIO_PAGE;
        	buf[pos++] = 0xE; /* Page length */
        	buf[pos++] = 4; /* Reserved */
        	buf[pos++] = 0; /* Reserved */
        	buf[pos++] = 0; /* Reserved */
        	buf[pos++] = 0; /* Reserved */
        	buf[pos++] = 0; buf[pos++] = 75; /* Logical audio block per second */
        	buf[pos++] = 1;    /* CDDA Output Port 0 Channel Selection */
        	buf[pos++] = 0xFF; /* CDDA Output Port 0 Volume */
        	buf[pos++] = 2;    /* CDDA Output Port 1 Channel Selection */
        	buf[pos++] = 0xFF; /* CDDA Output Port 1 Volume */
        	buf[pos++] = 0;    /* CDDA Output Port 2 Channel Selection */
        	buf[pos++] = 0;    /* CDDA Output Port 2 Volume */
        	buf[pos++] = 0;    /* CDDA Output Port 3 Channel Selection */
        	buf[pos++] = 0;    /* CDDA Output Port 3 Volume */
        }

        if (type==GPMODE_ALL_PAGES || type==GPMODE_CAPABILITIES_PAGE)
        {
//                pclog("Capabilities page\n");
               	/* &2A - CD-ROM capabilities and mechanical status */
        	buf[pos++] = GPMODE_CAPABILITIES_PAGE;
        	buf[pos++] = 0x12; /* Page length */
        	buf[pos++] = 0; buf[pos++] = 0; /* CD-R methods */
        	buf[pos++] = 1; /* Supports audio play, not multisession */
        	buf[pos++] = 0; /* Some other stuff not supported */
        	buf[pos++] = 0; /* Some other stuff not supported (lock state + eject) */
        	buf[pos++] = 0; /* Some other stuff not supported */
        	buf[pos++] = (uint8_t) (CDROM_SPEED >> 8);
        	buf[pos++] = (uint8_t) CDROM_SPEED; /* Maximum speed */
        	buf[pos++] = 0; buf[pos++] = 2; /* Number of audio levels - on and off only */
        	buf[pos++] = 0; buf[pos++] = 0; /* Buffer size - none */
        	buf[pos++] = (uint8_t) (CDROM_SPEED >> 8);
        	buf[pos++] = (uint8_t) CDROM_SPEED; /* Current speed */
        	buf[pos++] = 0; /* Reserved */
        	buf[pos++] = 0; /* Drive digital format */
        	buf[pos++] = 0; /* Reserved */
        	buf[pos++] = 0; /* Reserved */
        }

	return pos;
}

/*
 * Return the sector offset for the current register values
 */
static off64_t ide_get_sector(IDE *ide)
{
        if (ide->lba)
        {
                return (off64_t)ide->lba_addr + ide->skip512;
        }
        else
        {
        	int heads = ide->hpc;
        	int sectors = ide->spt;

        	return ((((off64_t) ide->cylinder * heads) + ide->head) *
        	          sectors) + (ide->sector - 1) + ide->skip512;
        }
}

/**
 * Move to the next sector using CHS addressing
 */
static void ide_next_sector(IDE *ide)
{
        if (ide->lba)
        {
                ide->lba_addr++;
        }
        else
        {
        	ide->sector++;
#if 0
		if ((ide->head == (ide->hpc - 1)) && (ide->cylinder == (ide->tracks - 1)))
		{
			/* Same logic as with floppies - obviously if we've reached the end of the track, there is no next sector. */
			ide->sector--;
			return;
		}
#endif
        	if (ide->sector == (ide->spt + 1)) {
        		ide->sector = 1;
        		ide->head++;
        		if (ide->head == ide->hpc) {
        			ide->head = 0;
        			ide->cylinder++;
                        }
		}
	}
}

#ifdef RPCEMU_IDE
static void loadhd(IDE *ide, int d, const char *fn)
{
	char pathname[512];

	append_filename(pathname, rpcemu_get_datadir(), fn, 512);

        rpclog("Loading %s\n",pathname);
	if (ide->hdfile == NULL) {
		/* Try to open existing hard disk image */
		ide->hdfile = fopen64(pathname, "rb+");
		if (ide->hdfile == NULL) {
			/* Failed to open existing hard disk image */
			if (errno == ENOENT) {
				/* Failed because it does not exist,
				   so try to create new file */
				ide->hdfile = fopen64(pathname, "wb+");
				if (ide->hdfile == NULL) {
					fatal("Cannot create file '%s': %s",
					      pathname, strerror(errno));
				}
			} else {
				/* Failed for another reason */
				fatal("Cannot open file '%s': %s",
				      pathname, strerror(errno));
			}
		}
	}

        fseek(ide->hdfile, 0xfc1, SEEK_SET);
        ide->spt = getc(ide->hdfile);
        ide->hpc = getc(ide->hdfile);
        ide->skip512 = 1;
//        rpclog("First check - spt %i hpc %i\n",ide.spt[0],ide.hpc[0]);
        if (!ide->spt || !ide->hpc)
        {
                fseek(ide->hdfile, 0xdc1, SEEK_SET);
                ide->spt = getc(ide->hdfile);
                ide->hpc = getc(ide->hdfile);
//                rpclog("Second check - spt %i hpc %i\n",ide.spt[0],ide.hpc[0]);
                ide->skip512 = 0;
                if (!ide->spt || !ide->hpc)
                {
                        ide->spt=63;
                        ide->hpc=16;
                        ide->skip512 = 1;
//        rpclog("Final check - spt %i hpc %i\n",ide.spt[0],ide.hpc[0]);
                }
        }

        ide->type = IDE_HDD;
        
        rpclog("%i %i %i\n",ide->spt,ide->hpc,ide->skip512);
}
#else
static void loadhd(IDE *ide, int d, const char *fn)
{
	if (ide->hdfile == NULL) {
		/* Try to open existing hard disk image */
		ide->hdfile = fopen64(fn, "rb+");
		if (ide->hdfile == NULL) {
			/* Failed to open existing hard disk image */
			if (errno == ENOENT) {
				/* Failed because it does not exist,
				   so try to create new file */
				ide->hdfile = fopen64(fn, "wb+");
				if (ide->hdfile == NULL) {
                                        ide->type = IDE_NONE;
/*					fatal("Cannot create file '%s': %s",
					      fn, strerror(errno));*/
					return;
				}
			} else {
				/* Failed for another reason */
                                ide->type = IDE_NONE;
/*				fatal("Cannot open file '%s': %s",
				      fn, strerror(errno));*/
				return;
			}
		}
	}

        ide->spt = hdc[d].spt;
        ide->hpc = hdc[d].hpc;
        ide->tracks = hdc[d].tracks;
        ide->type = IDE_HDD;
}
#endif

int first_time = 1;

void ide_set_signature(IDE *ide)
{
	ide->secount=1;
	ide->sector=1;
	ide->head=0;
	ide->cylinder=(IDE_DRIVE_IS_CDROM(ide) ? 0xEB14 : ((ide->type == IDE_HDD) ? 0 : 0xFFFF));
	// ide->cylinder=(IDE_DRIVE_IS_CDROM(ide) ? 0xEB14 : 0);
}

void ide_reset(IDE *ide)
{
	ide->atastat = (ide->type == IDE_CDROM) ? 0 : (READY_STAT | DSC_STAT);
	ide->error=1;
	ide_set_signature(ide);
	atapi->stop();
}

int soft_reset = 1;

#if 0
void resetcyl(void)
{
        int d;

        for (d = 0; d < 4; d++) {
		ide_irq_lower(&ide_drives[d]);
		ide_reset(&ide_drives[d]);
		ide_drives[d].packetstatus = 0xFF;
                ide_drives[d].service = 0;
                ide_drives[d].board = (d & 2) ? 1 : 0;
        }

        idecallback[0]=idecallback[1]=0;

        cur_ide[0] = 0;
        cur_ide[1] = 2;

	if (!first_time)  atapi->stop();

	soft_reset = 1;

	// pclog("x86 IDE reset completed\n");
}
#endif

void closeide(void)
{
        int d;

        for (d = 0; d < 4; d++) {
                if (ide_drives[d].hdfile != NULL) {
			fflush(ide_drives[d].hdfile);
                        fclose(ide_drives[d].hdfile);
                        ide_drives[d].hdfile = NULL;
                }
	}
}

void resetide(void)
{
        int d;
	int last_drive = 0;
	int hds_loaded = 0;
	int limit = 0;
	int limit2 = 0;
	int hds_count = 0;

	int i = 0;

        /* Close hard disk image files (if previously open) */
        for (d = 0; d < 4; d++) {
                ide_drives[d].type = IDE_NONE;
                if (ide_drives[d].hdfile != NULL) {
                        fclose(ide_drives[d].hdfile);
                        ide_drives[d].hdfile = NULL;
                }
                ide_drives[d].atastat = READY_STAT | DSC_STAT;
                ide_drives[d].service = 0;
                ide_drives[d].board = (d & 2) ? 1 : 0;
        }

	/* Count total hard drives. */
	for (i = 0; i < 4; i++)
	{
		loadhd(&ide_drives[i], i, ide_fn[i]);
		if (ide_drives[i].type == IDE_HDD)
		{
			hds_count++;
			fclose(ide_drives[d].hdfile);
			ide_drives[d].hdfile = NULL;
			ide_drives[d].type = IDE_NONE;
		}
	}

        idecallback[0]=idecallback[1]=0;
#ifdef RPCEMU_IDE
	loadhd(&ide_drives[0], 0, "hd4.hdf");
	if (!config.cdromenabled) {
		loadhd(&ide_drives[1], 1, "hd5.hdf");
	}
	else
           ide_drives[1].type = IDE_CDROM;
#else
	/* New IDE loading logic:
	   Keep loading hard disk images, until either all are loaded, ran out of slots, or CD-ROM is enabled and reached its slot.

	   Designed to allow up to 4 hard disks (up to 3 if CD-ROM is enabled), up to 1 CD-ROM, load all possible hard disks,
	   ensure there is no slave without master, and ensure at least one slot gets assigned to CD-ROM. */

#ifndef RELEASE_BUILD
	pclog("resetide(): maxide is %i\n", maxide);
#endif

	limit = maxide - 1;
	limit2 = ((cdrom_enabled) ? (limit - 1) : limit);

	ide_map[0] = -1;
	ide_map[1] = -1;
	ide_map[2] = -1;
	ide_map[3] = -1;

	ide_drives[0].type = IDE_NONE;
	ide_drives[1].type = IDE_NONE;
	ide_drives[2].type = IDE_NONE;
	ide_drives[3].type = IDE_NONE;

        loadhd(&ide_drives[last_drive], 0, ide_fn[0]);
	if (ide_drives[last_drive].type == IDE_HDD)
	{
		ide_map[last_drive] = 0;
		hds_loaded++;
		last_drive++;
	}
	// pclog("IDE Loader: Last drive is %i\n", last_drive);
        if (cdrom_enabled && (hds_loaded == limit))  goto load_cdrom;
        if (!cdrom_enabled && (hds_loaded == limit + 1))  goto done_loading;
	if (last_drive > limit2)  goto load_cdrom;

	/* If we have two hard drives, load second hard drive as secondary master, so CD-ROM becomes secondary slave,
	   and the Award SiS 496/497 BIOS we use marks secondary IDE as present. */
	if (((romset == ROM_SIS496) || (romset == ROM_430FX)) && (last_drive == 1) && (hds_count == 2) && (maxide == 4))  last_drive = 2;
        loadhd(&ide_drives[last_drive], 1, ide_fn[1]);
	if (ide_drives[last_drive].type == IDE_HDD)
	{
		ide_map[last_drive] = 1;
		hds_loaded++;
		last_drive++;
	}
	/* This is so if at least 1 slot is assigned for primary IDE, we go straight from here to secondary IDE. */
	if (maxide == 4)  if (last_drive == 1)  last_drive = 2;
	// pclog("IDE Loader: Last drive is %i\n", last_drive);
        if (cdrom_enabled && (hds_loaded == limit))  goto load_cdrom;
        if (!cdrom_enabled && (hds_loaded == limit + 1))  goto done_loading;
	if (last_drive > limit2)  goto load_cdrom;

        loadhd(&ide_drives[last_drive], 2, ide_fn[2]);
	if (ide_drives[last_drive].type == IDE_HDD)
	{
		ide_map[last_drive] = 2;
		hds_loaded++;
		last_drive++;
	}
	// pclog("IDE Loader: Last drive is %i\n", last_drive);
        if (cdrom_enabled && (hds_loaded == limit))  goto load_cdrom;
        if (!cdrom_enabled && (hds_loaded == limit + 1))  goto done_loading;
   	if (last_drive > limit2)  goto load_cdrom;

        loadhd(&ide_drives[last_drive], 3, ide_fn[3]);
	if (ide_drives[last_drive].type == IDE_HDD)
	{
		ide_map[last_drive] = 3;
		hds_loaded++;
		last_drive++;
	}
	// pclog("IDE Loader: Last drive is %i\n", last_drive);
        if (cdrom_enabled && (hds_loaded == limit))  goto load_cdrom;
        if (!cdrom_enabled && (hds_loaded == limit + 1))  goto done_loading;
   	if (last_drive > limit2)  goto load_cdrom;

load_cdrom:
        if (cdrom_enabled)
	{
		/* If we're loading CD-ROM to drive ID 2, but drive ID 1 is type none,
		   and the chipset is an Award SiS 496/497, load the CD-ROM to drive ID 1 instead. */
		if (((romset == ROM_SIS496) || (romset == ROM_430FX)) && (last_drive == 2) && (ide_drives[1].type == IDE_NONE))  last_drive = 1;

		ide_drives[last_drive].type = IDE_CDROM;

		if (!first_time)  atapi->stop();
	}

done_loading:
#endif

        cur_ide[0] = 0;
        cur_ide[1] = 2;
        
//        ide_drives[1].type = IDE_CDROM;
//        ide_drives[2].type = IDE_CDROM;
//	ide_drives[3].type = IDE_NONE;

	if (first_time)  first_time = 0;

	// pclog("Setting signature for all drives\n");
	ide_set_signature(&ide_drives[0]);
	ide_set_signature(&ide_drives[1]);
	ide_set_signature(&ide_drives[2]);
	ide_set_signature(&ide_drives[3]);
}

int idetimes=0;
void writeidew(int ide_board, uint16_t val)
{
        IDE *ide = &ide_drives[cur_ide[ide_board]];
#ifndef RPCEMU_IDE
/*        if (ide_board && (cr0&1) && !(eflags&VM_FLAG))
        {
//                pclog("Failed write IDE %04X:%08X\n",CS,pc);
                return;
        }*/
#endif
#ifdef _RPCEMU_BIG_ENDIAN
		val=(val>>8)|(val<<8);
#endif
        // pclog("Write IDEw %04X\n",val);
        ide->buffer[ide->pos >> 1] = val;
        ide->pos+=2;

        if (ide->packetstatus==4)
        {
                if (ide->pos>=(ide->packlen+2))
                {
                        ide->packetstatus=5;
                        timer_process();
                        idecallback[ide_board]=6*IDE_TIME;
                        timer_update_outstanding();
//                        pclog("Packet over!\n");
                        ide_irq_lower(ide);
                }
                return;
        }
        else if (ide->packetstatus==5) return;
        else if (ide->command == WIN_PACKETCMD && ide->pos>=0xC)
        {
                ide->pos=0;
                ide->atastat = BUSY_STAT;
                ide->packetstatus=1;
/*                idecallback[ide_board]=6*IDE_TIME;*/
		timer_process();
                callbackide(ide_board);
                timer_update_outstanding();
//                idecallback[ide_board]=60*IDE_TIME;
//                if ((ide->buffer[0]&0xFF)==0x43) idecallback[ide_board]=1*IDE_TIME;
//                pclog("Packet now waiting!\n");
/*                if (ide->buffer[0]==0x243)
                {
                        idetimes++;
                        output=3;
                }*/
        }
        else if (ide->pos>=512)
        {
                ide->pos=0;
                ide->atastat = BUSY_STAT;
                timer_process();
                if (ide->command == WIN_WRITE_MULTIPLE)
                   callbackide(ide_board);
                else
              	   idecallback[ide_board]=6*IDE_TIME;
                timer_update_outstanding();
        }
}

void writeidel(int ide_board, uint32_t val)
{
        // pclog("WriteIDEl %08X\n", val);
        writeidew(ide_board, val);
        writeidew(ide_board, val >> 16);
}

void writeide(int ide_board, uint16_t addr, uint8_t val)
{
        IDE *ide = &ide_drives[cur_ide[ide_board]];
        IDE *ide_other = &ide_drives[cur_ide[ide_board] ^ 1];
#ifndef RPCEMU_IDE
/*        if (ide_board && (cr0&1) && !(eflags&VM_FLAG))
        {
//                pclog("Failed write IDE %04X:%08X\n",CS,pc);
                return;
        }*/
#endif
//        if ((cr0&1) && !(eflags&VM_FLAG))
        // pclog("WriteIDE %04X %02X from %04X(%08X):%08X %i\n", addr, val, CS, cs, pc, ins);
//        return;
        addr|=0x80;
//        if (ide_board) pclog("Write IDEb %04X %02X %04X(%08X):%04X %i  %02X %02X\n",addr,val,CS,cs,pc,ins,ide->atastat,ide_drives[0].atastat);
        /*if (idedebug) */
        // pclog("Write IDE %08X %02X %04X:%08X\n",addr,val,CS,pc);
//        int c;
//      rpclog("Write IDE %08X %02X %08X %08X\n",addr,val,PC,armregs[12]);

        if (ide->type == IDE_NONE && addr != 0x1f6 && addr != 0x3f6) return;
        
        switch (addr)
        {
        case 0x1F0: /* Data */
                writeidew(ide_board, val | (val << 8));
                return;

        case 0x1F1: /* Features */
                ide->cylprecomp = val;
                ide_other->cylprecomp = val;
                return;

        case 0x1F2: /* Sector count */
                ide->secount = val;
                ide_other->secount = val;
                return;

        case 0x1F3: /* Sector */
                ide->sector = val;
                ide->lba_addr = (ide->lba_addr & 0xFFFFF00) | val;
                ide_other->sector = val;
                ide_other->lba_addr = (ide_other->lba_addr & 0xFFFFF00) | val;
                return;

        case 0x1F4: /* Cylinder low */
                ide->cylinder = (ide->cylinder & 0xFF00) | val;
                ide->lba_addr = (ide->lba_addr & 0xFFF00FF) | (val << 8);
                ide_other->cylinder = (ide_other->cylinder&0xFF00) | val;
                ide_other->lba_addr = (ide_other->lba_addr&0xFFF00FF) | (val << 8);
//                pclog("Write cylinder low %02X\n",val);
                return;

        case 0x1F5: /* Cylinder high */
                ide->cylinder = (ide->cylinder & 0xFF) | (val << 8);
                ide->lba_addr = (ide->lba_addr & 0xF00FFFF) | (val << 16);
                ide_other->cylinder = (ide_other->cylinder & 0xFF) | (val << 8);
                ide_other->lba_addr = (ide_other->lba_addr & 0xF00FFFF) | (val << 16);
                return;

        case 0x1F6: /* Drive/Head */
/*        if (val==0xB0)
        {
                dumpregs();
                exit(-1);
        }*/
        
                if (cur_ide[ide_board] != ((val>>4)&1)+(ide_board<<1))
                {
                        cur_ide[ide_board]=((val>>4)&1)+(ide_board<<1);

                        if (ide->reset || ide_other->reset)
                        {
                                ide->atastat = ide_other->atastat = READY_STAT | DSC_STAT;

                                ide->error = ide_other->error = 1;

                                ide->secount = ide_other->secount = 1;

                                ide->sector = ide_other->sector = 1;

                                ide->head = ide_other->head = 0;

                                ide->cylinder = ide_other->cylinder = 0;

                                ide->reset = ide_other->reset = 0;

                                ide->blocksize = ide_other->blocksize = 0;

                                if (IDE_DRIVE_IS_CDROM(ide))

                                        ide->cylinder=0xEB14;

                                if (IDE_DRIVE_IS_CDROM(ide_other))

                                        ide_other->cylinder=0xEB14;

				ide_reset(ide);
				ide_reset(ide_other);				

                                idecallback[ide_board] = 0;

                                timer_update_outstanding();

                                return;

                        }

                        ide = &ide_drives[cur_ide[ide_board]];
                        ide_other = &ide_drives[cur_ide[ide_board] ^ 1];
                }
                                
                ide->head = val & 0xF;
                ide->lba = val & 0x40;
                ide_other->head = val & 0xF;
                ide_other->lba = val & 0x40;
                
                ide->lba_addr = (ide->lba_addr & 0x0FFFFFF) | ((val & 0xF) << 24);
                ide_other->lba_addr = (ide_other->lba_addr & 0x0FFFFFF)|((val & 0xF) << 24);
                                
                ide_irq_update(ide);
                return;

        case 0x1F7: /* Command register */
        if (ide->type == IDE_NONE) { /* pclog("IDE type is none, returning...\n"); */ return; }
                // pclog("IDE command %02X drive %i\n",val,ide.drive);
                // pclog("IDE command %02X\n",val);
		// if (ide->type == IDE_CDROM)  pclog("IDE command %02X\n",val);
	        ide_irq_lower(ide);
                ide->command=val;
                
                // pclog("New IDE command - %02X %i %i\n",ide->command,cur_ide[ide_board],ide_board);
                ide->error=0;
                switch (val)
                {
                case WIN_SRST: /* ATAPI Device Reset */
                        if (IDE_DRIVE_IS_CDROM(ide)) ide->atastat = BUSY_STAT;
                        else                         ide->atastat = READY_STAT;
                        timer_process();
                        idecallback[ide_board]=100*IDE_TIME;
                        timer_update_outstanding();
                        return;

                case WIN_RESTORE:
                case WIN_SEEK:
                        // pclog("WIN_RESTORE start\n");
                        ide->atastat = READY_STAT;
                        timer_process();
                        idecallback[ide_board]=100*IDE_TIME;
                        timer_update_outstanding();
                        return;

                case WIN_READ_MULTIPLE:
                        if (!ide->blocksize)
                           fatal("READ_MULTIPLE - blocksize = 0\n");
#if 0
                        if (ide->lba) pclog("Read Multiple %i sectors from LBA addr %07X\n",ide->secount,ide->lba_addr);
                        else          pclog("Read Multiple %i sectors from sector %i cylinder %i head %i  %i\n",ide->secount,ide->sector,ide->cylinder,ide->head,ins);
#endif
                        ide->blockcount = 0;
                        
                case WIN_READ:
                case WIN_READ_NORETRY:
                case WIN_READ_DMA:
/*                        if (ide.secount>1)
                        {
                                fatal("Read %i sectors from sector %i cylinder %i head %i\n",ide.secount,ide.sector,ide.cylinder,ide.head);
                        }*/
#if 0
                        if (ide->lba) pclog("Read %i sectors from LBA addr %07X\n",ide->secount,ide->lba_addr);
                        else          pclog("Read %i sectors from sector %i cylinder %i head %i  %i\n",ide->secount,ide->sector,ide->cylinder,ide->head,ins);
#endif
                        ide->atastat = BUSY_STAT;
                        timer_process();
                        idecallback[ide_board]=200*IDE_TIME;
                        timer_update_outstanding();
                        return;
                        
                case WIN_WRITE_MULTIPLE:
                        if (!ide->blocksize)
                           fatal("Write_MULTIPLE - blocksize = 0\n");
#if 0
                        if (ide->lba) pclog("Write Multiple %i sectors from LBA addr %07X\n",ide->secount,ide->lba_addr);
                        else          pclog("Write Multiple %i sectors to sector %i cylinder %i head %i\n",ide->secount,ide->sector,ide->cylinder,ide->head);
#endif
                        ide->blockcount = 0;
                        
                case WIN_WRITE:
                case WIN_WRITE_NORETRY:
                /*                        if (ide.secount>1)
                        {
                                fatal("Write %i sectors to sector %i cylinder %i head %i\n",ide.secount,ide.sector,ide.cylinder,ide.head);
                        }*/
#if 0
                        if (ide->lba) pclog("Write %i sectors from LBA addr %07X\n",ide->secount,ide->lba_addr);
                        else          pclog("Write %i sectors to sector %i cylinder %i head %i\n",ide->secount,ide->sector,ide->cylinder,ide->head);
#endif
                        ide->atastat = DRQ_STAT | DSC_STAT | READY_STAT;
                        ide->pos=0;
                        return;

                case WIN_WRITE_DMA:
#if 0
                        if (ide->lba) pclog("Write %i sectors from LBA addr %07X\n",ide->secount,ide->lba_addr);
                        else          pclog("Write %i sectors to sector %i cylinder %i head %i\n",ide->secount,ide->sector,ide->cylinder,ide->head);
#endif
                        ide->atastat = BUSY_STAT;
                        timer_process();
                        idecallback[ide_board]=200*IDE_TIME;
                        timer_update_outstanding();
                        return;

                case WIN_VERIFY:
#if 0
                        if (ide->lba) pclog("Read verify %i sectors from LBA addr %07X\n",ide->secount,ide->lba_addr);
                        else          pclog("Read verify %i sectors from sector %i cylinder %i head %i\n",ide->secount,ide->sector,ide->cylinder,ide->head);
#endif
                        ide->atastat = BUSY_STAT;
                        timer_process();
                        idecallback[ide_board]=200*IDE_TIME;
                        timer_update_outstanding();
                        return;

                case WIN_FORMAT:
//                        pclog("Format track %i head %i\n",ide.cylinder,ide.head);
                        ide->atastat = DRQ_STAT;
//                        idecallback[ide_board]=200;
                        ide->pos=0;
                        return;

                case WIN_SPECIFY: /* Initialize Drive Parameters */
                        ide->atastat = BUSY_STAT;
                        timer_process();
                        idecallback[ide_board]=200*IDE_TIME;
                        timer_update_outstanding();
//                        pclog("SPECIFY\n");
//                        output=1;
                        return;

                case WIN_DRIVE_DIAGNOSTICS: /* Execute Drive Diagnostics */
                case WIN_PIDENTIFY: /* Identify Packet Device */
                case WIN_SET_MULTIPLE_MODE: /*Set Multiple Mode*/
//                output=1;
                case WIN_SETIDLE1: /* Idle */
                        ide->atastat = BUSY_STAT;
                        timer_process();
                        callbackide(ide_board);
//                        idecallback[ide_board]=200*IDE_TIME;
                        timer_update_outstanding();
                        return;

                case WIN_IDENTIFY: /* Identify Device */
                case 0xEF:
//                        output=3;
//                        timetolive=500;
                        ide->atastat = BUSY_STAT;
                        timer_process();
                        idecallback[ide_board]=200*IDE_TIME;
                        timer_update_outstanding();
                        return;

                case WIN_PACKETCMD: /* ATAPI Packet */
                        ide->packetstatus=0;
                        ide->atastat = BUSY_STAT;
                        timer_process();
                        idecallback[ide_board]=1;//30*IDE_TIME;
                        timer_update_outstanding();
                        ide->pos=0;
                        return;
                        
                case 0xF0:
                        default:
                	ide->atastat = READY_STAT | ERR_STAT | DSC_STAT;
                	ide->error = ABRT_ERR;
                        ide_irq_raise(ide);
/*                        fatal("Bad IDE command %02X\n", val);*/
                        return;
                }
                
                return;

        case 0x3F6: /* Device control */
                if ((ide->fdisk&4) && !(val&4) && (ide->type != IDE_NONE || ide_other->type != IDE_NONE))

                {

			timer_process();

                        idecallback[ide_board]=500*IDE_TIME;

                        timer_update_outstanding();

                        ide->reset = ide_other->reset = 1;

                        ide->atastat = ide_other->atastat = BUSY_STAT;

//                        pclog("IDE Reset %i\n", ide_board);

                }

                ide->fdisk=val;

                ide_irq_update(ide);

                return;

        }
//        fatal("Bad IDE write %04X %02X\n", addr, val);
}

uint8_t readide(int ide_board, uint16_t addr)
{
        IDE *ide = &ide_drives[cur_ide[ide_board]];
        uint8_t temp;
        uint16_t tempw;

        addr|=0x80;
#ifndef RPCEMU_IDE
/*        if (ide_board && (cr0&1) && !(eflags&VM_FLAG))
        {
//                pclog("Failed read IDE %04X:%08X\n",CS,pc);
                return 0xFF;
        }*/
#endif
//        if ((cr0&1) && !(eflags&VM_FLAG))
        // pclog("ReadIDE %04X  from %04X(%08X):%08X\n", addr, CS, cs, pc);
//        return 0xFF;

	/* What the fuck?! */
        // if (ide->type == IDE_NONE && addr != 0x1f6) return 0;
//        /*if (addr!=0x1F7 && addr!=0x3F6) */pclog("Read IDEb %04X %02X %02X %i %04X:%04X %i  %04X\n",addr,ide->atastat,(ide->atastat & ~DSC_STAT) | (ide->service ? SERVICE_STAT : 0),cur_ide[ide_board],CS,pc,ide_board, BX);
//rpclog("Read IDE %08X %08X %02X\n",addr,PC,iomd.irqb.mask);
        switch (addr)
        {
        case 0x1F0: /* Data */
                tempw = readidew(ide_board);
//                pclog("Read IDEW %04X\n", tempw);                
                temp = tempw & 0xff;
                break;
                
        case 0x1F1: /* Error */
//		pclog("Read error %02X\n",ide->error);
                temp = ide->error;
		if (ide->type == IDE_NONE)  temp = 0;
                break;

        case 0x1F2: /* Sector count */
//        pclog("Read sector count %02X\n",ide->secount);
                temp = (uint8_t)ide->secount;
                break;

        case 0x1F3: /* Sector */
                temp = (uint8_t)ide->sector;
                break;

        case 0x1F4: /* Cylinder low */
		// pclog("Read cyl low %02X\n",ide->cylinder&0xFF);
                temp = (uint8_t)(ide->cylinder&0xFF);
#if 0
		pclog("1F4: Previous command was: %02X\n", ide->command);
		if ((CS == 0xE000) && (pc == 0x31DF) && (ide->type == IDE_CDROM) && (romset == ROM_430HX) && soft_reset)
		{
			temp = 0x14;
			soft_reset = 0;
		}
		if ((CS == 0xE000) && (pc == 0x32A6) && (ide->type == IDE_CDROM) && (romset == ROM_430TX) && soft_reset)
		{
			temp = 0x14;
			soft_reset = 0;
		}
#endif
		// pclog("Read cyl low %02X\n",temp);
                break;

        case 0x1F5: /* Cylinder high */
		// pclog("Read cyl high %02X\n",ide->cylinder>>8);
                temp = (uint8_t)(ide->cylinder>>8);
#if 0
		pclog("1F4: Previous command was: %02X\n", ide->command);
		if ((CS == 0xE000) && (pc == 0x31D9) && (ide->type == IDE_CDROM) && (romset == ROM_430HX) && soft_reset)
		{
			temp = 0xEB;
			soft_reset = 0;
		}
		if ((CS == 0xE000) && (pc == 0x32A0) && (ide->type == IDE_CDROM) && (romset == ROM_430TX) && soft_reset)
		{
			temp = 0xEB;
			soft_reset = 0;
		}
#endif
		// pclog("Read cyl high %02X\n",temp);
                break;

        case 0x1F6: /* Drive/Head */
                temp = (uint8_t)(ide->head | ((cur_ide[ide_board] & 1) ? 0x10 : 0) | (ide->lba ? 0x40 : 0) | 0xa0);
                break;

        case 0x1F7: /* Status */
                if (ide->type == IDE_NONE)
                {
                        // pclog("Return status 00\n");
                        temp = 0;
                        break;
                }
                ide_irq_lower(ide);
                if (ide->fdisk & 4)
			temp = 0x80;
                if (ide->type == IDE_CDROM)
                {
                        // pclog("Read CDROM status %02X\n",(ide->atastat & ~DSC_STAT) | (ide->service ? SERVICE_STAT : 0));
                        temp = (ide->atastat & ~DSC_STAT) | (ide->service ? SERVICE_STAT : 0);
                }
                else
                {
//                 && ide->service) return ide.atastat[ide.board]|SERVICE_STAT;
                // pclog("Return status %02X %04X:%04X %02X %02X\n",ide->atastat, CS ,pc, AH, BH);
                        temp = ide->atastat;
                }
                break;

        case 0x3F6: /* Alternate Status */
//        pclog("3F6 read %02X\n",ide.atastat[ide.board]);
//        if (output) output=0;
                if (ide->type == IDE_NONE)
                {
//                        pclog("Return status 00\n");
                        temp = 0;
                        break;
                }
                if (ide->type == IDE_CDROM)
                {
//                        pclog("Read CDROM status %02X\n",(ide->atastat & ~DSC_STAT) | (ide->service ? SERVICE_STAT : 0));
                        temp = (ide->atastat & ~DSC_STAT) | (ide->service ? SERVICE_STAT : 0);
                }
                else
                {
//                 && ide->service) return ide.atastat[ide.board]|SERVICE_STAT;
//                pclog("Return status %02X\n",ide->atastat);
                        temp = ide->atastat;
                }
                break;
        }
//        if (ide_board) pclog("Read IDEb %04X %02X   %02X %02X %i %04X:%04X %i\n", addr, temp, ide->atastat,(ide->atastat & ~DSC_STAT) | (ide->service ? SERVICE_STAT : 0),cur_ide[ide_board],CS,pc,ide_board);
        return temp;
//        fatal("Bad IDE read %04X\n", addr);
}

uint16_t readidew(int ide_board)
{
        IDE *ide = &ide_drives[cur_ide[ide_board]];
        uint16_t temp;
#ifndef RPCEMU_IDE
/*        if (ide_board && (cr0&1) && !(eflags&VM_FLAG))
        {
//                pclog("Failed read IDEw %04X:%08X\n",CS,pc);
                return 0xFFFF;
        }*/
#endif
//        return 0xFFFF;
//        pclog("Read IDEw %04X %04X:%04X %02X %i %i\n",ide->buffer[ide->pos >> 1],CS,pc,opcode,ins, ide->pos);
        
	// pclog("Read IDEW %08X\n", pc);

        temp = ide->buffer[ide->pos >> 1];
	#ifdef _RPCEMU_BIG_ENDIAN
		temp=(temp>>8)|(temp<<8);
	#endif
        ide->pos+=2;
        if ((ide->pos>=512 && ide->command != WIN_PACKETCMD) || (ide->command == WIN_PACKETCMD && ide->pos>=ide->packlen))
        {
//                pclog("Over! packlen %i %i\n",ide->packlen,ide->pos);
                ide->pos=0;
                if (ide->command == WIN_PACKETCMD)// && ide.packetstatus==6)
                {
                        // pclog("Call readCD\n");
                        callreadcd(ide);
                }
                else
                {
                        ide->atastat = READY_STAT | DSC_STAT;
                        ide->packetstatus=0;
                        if (ide->command == WIN_READ || ide->command == WIN_READ_NORETRY || ide->command == WIN_READ_MULTIPLE)
                        {
                                ide->secount = (ide->secount - 1) & 0xff;
                                if (ide->secount)
                                {
                                        ide_next_sector(ide);
                                        ide->atastat = BUSY_STAT;
                                        timer_process();
                                        if (ide->command == WIN_READ_MULTIPLE)
                                           callbackide(ide_board);
                                        else
                                           idecallback[ide_board]=6*IDE_TIME;
                                        timer_update_outstanding();
//                                        pclog("set idecallback\n");
//                                        callbackide(ide_board);
                                }
//                                else
//                                   pclog("readidew done %02X\n", ide->atastat);
                        }
                }
        }
        // pclog("Read IDEw %04X\n",temp);
        return temp;
}

uint32_t readidel(int ide_board)
{
        uint16_t temp;
        // pclog("Read IDEl %i\n", ide_board);
        temp = readidew(ide_board);
        return temp | (readidew(ide_board) << 16);
}

int times30=0;
int readcdmode=0;
void callbackide(int ide_board)
{
        IDE *ide = &ide_drives[cur_ide[ide_board]];
        IDE *ide_other = &ide_drives[cur_ide[ide_board] ^ 1];
	cur_board = ide_board;
        off64_t addr;
        int c;
        ext_ide = ide;
//        return;
        if (ide->command==0x30) times30++;
//        if (times30==2240) output=1;
        //if (times30==2471 && ide->command==0xA0) output=1;
///*if (ide_board) */pclog("CALLBACK %02X %i %i  %i\n",ide->command,times30,ide->reset,cur_ide[ide_board]);
//        if (times30==1294)
//                output=1;
        if (ide->reset)
        {
                ide->atastat = ide_other->atastat = READY_STAT | DSC_STAT;

                ide->error = ide_other->error = 1;

                ide->secount = ide_other->secount = 1;

                ide->sector = ide_other->sector = 1;

                ide->head = ide_other->head = 0;

                ide->cylinder = ide_other->cylinder = 0;

                ide->reset = ide_other->reset = 0;

                if (IDE_DRIVE_IS_CDROM(ide))
                {
                        ide->cylinder=0xEB14;
                        atapi->stop();
                }
                if (IDE_DRIVE_IS_CDROM(ide_other))
                {
                        ide_other->cylinder=0xEB14;
                        atapi->stop();
                }
		ide_reset(ide);
		ide_reset(ide_other);
                // pclog("Reset callback\n");
                return;
        }
        switch (ide->command)
        {
                //Initialize the Task File Registers as follows: Status = 00h, Error = 01h, Sector Count = 01h, Sector Number = 01h, Cylinder Low = 14h, Cylinder High =EBh and Drive/Head = 00h.
        case WIN_SRST: /*ATAPI Device Reset */
                ide->atastat = READY_STAT | DSC_STAT;
                ide->error=1; /*Device passed*/
                ide->secount = ide->sector = 1;
                if (IDE_DRIVE_IS_CDROM(ide)) {
                        ide->cylinder = 0xeb14;
                        ide->atastat = 0;
			atapi->stop();
                } else {
                        ide->cylinder = 0;
                }
                ide_irq_raise(ide);
                if (IDE_DRIVE_IS_CDROM(ide))
                   ide->service = 0;
                return;

        case WIN_RESTORE:
        case WIN_SEEK:
		if (ide->type == IDE_NONE)
		{
			// pclog("Attempt to reclibrate or seek where no device exists\n");
			goto abort_cmd;
		}

                if (IDE_DRIVE_IS_CDROM(ide)) {
                        // pclog("WIN_RESTORE callback on CD-ROM\n");
        	        ide->atastat = 0;
	                ide_irq_raise(ide);
			// pclog("CD: IRQ raised\n");
			return;
                        // goto abort_cmd;
                }
                // pclog("WIN_RESTORE callback on hard disk\n");
                ide->atastat = READY_STAT | DSC_STAT;
                ide_irq_raise(ide);
                return;

        case WIN_READ:
        case WIN_READ_NORETRY:
                if (IDE_DRIVE_IS_CDROM(ide)) {
			/* According to QEMU, ATA4 8.27.5.2 requires it. */
			// ide_set_signature(ide);
                        goto abort_cmd;
                }
		if (ide->type == IDE_NONE)  goto abort_cmd;
                addr = ide_get_sector(ide) * 512;
//                pclog("Read %i %i %i %08X\n",ide.cylinder,ide.head,ide.sector,addr);
                /*                if (ide.cylinder || ide.head)
                {
                        fatal("Read from other cylinder/head");
                }*/
                fseeko64(ide->hdfile, addr, SEEK_SET);
                fread(ide->buffer, 512, 1, ide->hdfile);
                ide->pos=0;
                ide->atastat = DRQ_STAT | READY_STAT | DSC_STAT;
//                pclog("Read sector callback %i %i %i offset %08X %i left %i %02X\n",ide.sector,ide.cylinder,ide.head,addr,ide.secount,ide.spt,ide.atastat[ide.board]);
//                if (addr) output=3;
                ide_irq_raise(ide);
#ifndef RPCEMU_IDE
                readflash=1;
#endif
                return;

        case WIN_READ_DMA:
                if (IDE_DRIVE_IS_CDROM(ide)) {
                        goto abort_cmd;
                }
                addr = ide_get_sector(ide) * 512;
                fseeko64(ide->hdfile, addr, SEEK_SET);
                fread(ide->buffer, 512, 1, ide->hdfile);
                ide->pos=0;
                
                if (ide_bus_master_read_sector)
                {
                        if (ide_bus_master_read_sector(ide_board, (uint8_t *)ide->buffer))
                           idecallback[ide_board]=6*IDE_TIME;           /*DMA not performed, try again later*/
                        else
                        {
                                /*DMA successful*/
                                ide->atastat = DRQ_STAT | READY_STAT | DSC_STAT;

                                ide->secount = (ide->secount - 1) & 0xff;
                                if (ide->secount)
                                {
                                        ide_next_sector(ide);
                                        ide->atastat = BUSY_STAT;
                                        idecallback[ide_board]=6*IDE_TIME;
                                }
                                else
                                {
                                        ide_irq_raise(ide);
                                }
                        }
                }
#ifndef RPCEMU_IDE
                readflash=1;
#endif
                return;

        case WIN_READ_MULTIPLE:
                if (IDE_DRIVE_IS_CDROM(ide)) {
                        goto abort_cmd;
                }
                addr = ide_get_sector(ide) * 512;
//                pclog("Read multiple from %08X %i (%i) %i\n", addr, ide->blockcount, ide->blocksize, ide->secount);
                fseeko64(ide->hdfile, addr, SEEK_SET);
                fread(ide->buffer, 512, 1, ide->hdfile);
                ide->pos=0;
                ide->atastat = DRQ_STAT | READY_STAT | DSC_STAT;
                if (!ide->blockcount)// || ide->secount == 1)
                {
//                        pclog("Read multiple int\n");
                        ide_irq_raise(ide);
                }                        
                ide->blockcount++;
                if (ide->blockcount >= ide->blocksize)
                   ide->blockcount = 0;
#ifndef RPCEMU_IDE
                readflash=1;
#endif
                return;

        case WIN_WRITE:
        case WIN_WRITE_NORETRY:
                if (IDE_DRIVE_IS_CDROM(ide)) {
                        goto abort_cmd;
                }
                addr = ide_get_sector(ide) * 512;
//                pclog("Write sector callback %i %i %i offset %08X %i left %i\n",ide.sector,ide.cylinder,ide.head,addr,ide.secount,ide.spt);
                fseeko64(ide->hdfile, addr, SEEK_SET);
                fwrite(ide->buffer, 512, 1, ide->hdfile);
                ide_irq_raise(ide);
                ide->secount = (ide->secount - 1) & 0xff;
                if (ide->secount)
                {
                        ide->atastat = DRQ_STAT | READY_STAT | DSC_STAT;
                        ide->pos=0;
                        ide_next_sector(ide);
                }
                else
                   ide->atastat = READY_STAT | DSC_STAT;
#ifndef RPCEMU_IDE
                readflash=1;
#endif
                return;
                
        case WIN_WRITE_DMA:
                if (IDE_DRIVE_IS_CDROM(ide)) {
                        goto abort_cmd;
                }

                if (ide_bus_master_write_sector)
                {
                        if (ide_bus_master_write_sector(ide_board, (uint8_t *)ide->buffer))
                           idecallback[ide_board]=6*IDE_TIME;           /*DMA not performed, try again later*/
                        else
                        {
                                /*DMA successful*/
                                addr = ide_get_sector(ide) * 512;
                                fseeko64(ide->hdfile, addr, SEEK_SET);
                                fwrite(ide->buffer, 512, 1, ide->hdfile);
                                
                                ide->atastat = DRQ_STAT | READY_STAT | DSC_STAT;

                                ide->secount = (ide->secount - 1) & 0xff;
                                if (ide->secount)
                                {
                                        ide_next_sector(ide);
                                        ide->atastat = BUSY_STAT;
                                        idecallback[ide_board]=6*IDE_TIME;
                                }
                                else
                                {
                                        ide_irq_raise(ide);
                                }
                        }
                }
#ifndef RPCEMU_IDE
                readflash=1;
#endif
                return;

        case WIN_WRITE_MULTIPLE:
                if (IDE_DRIVE_IS_CDROM(ide)) {
                        goto abort_cmd;
                }
                addr = ide_get_sector(ide) * 512;
//                pclog("Write sector callback %i %i %i offset %08X %i left %i\n",ide.sector,ide.cylinder,ide.head,addr,ide.secount,ide.spt);
                fseeko64(ide->hdfile, addr, SEEK_SET);
                fwrite(ide->buffer, 512, 1, ide->hdfile);
                ide->blockcount++;
                if (ide->blockcount >= ide->blocksize || ide->secount == 1)
                {
                        ide->blockcount = 0;
                        ide_irq_raise(ide);
                }
                ide->secount = (ide->secount - 1) & 0xff;
                if (ide->secount)
                {
                        ide->atastat = DRQ_STAT | READY_STAT | DSC_STAT;
                        ide->pos=0;
                        ide_next_sector(ide);
                }
                else
                   ide->atastat = READY_STAT | DSC_STAT;
#ifndef RPCEMU_IDE
                readflash=1;
#endif
                return;

        case WIN_VERIFY:
                if (IDE_DRIVE_IS_CDROM(ide)) {
                        goto abort_cmd;
                }
                ide->pos=0;
                ide->atastat = READY_STAT | DSC_STAT;
//                pclog("Read verify callback %i %i %i offset %08X %i left\n",ide.sector,ide.cylinder,ide.head,addr,ide.secount);
                ide_irq_raise(ide);
#ifndef RPCEMU_IDE
                readflash=1;
#endif
                return;

        case WIN_FORMAT:
                if (IDE_DRIVE_IS_CDROM(ide)) {
                        goto abort_cmd;
                }
                addr = ide_get_sector(ide) * 512;
//                pclog("Format cyl %i head %i offset %08X %08X %08X secount %i\n",ide.cylinder,ide.head,addr,addr>>32,addr,ide.secount);
                fseeko64(ide->hdfile, addr, SEEK_SET);
                memset(ide->buffer, 0, 512);
                for (c=0;c<ide->secount;c++)
                {
                        fwrite(ide->buffer, 512, 1, ide->hdfile);
                }
                ide->atastat = READY_STAT | DSC_STAT;
                ide_irq_raise(ide);
#ifndef RPCEMU_IDE
                readflash=1;
#endif
                return;

        case WIN_DRIVE_DIAGNOSTICS:
                ide->error=1; /*No error detected*/
                ide->atastat = READY_STAT | DSC_STAT;
		ide_irq_raise(ide);
                return;

        case WIN_SPECIFY: /* Initialize Drive Parameters */
		if (ide->type != IDE_HDD)
		{
			// pclog("ATA: Specify issued to non-disk\n");
			goto abort_cmd;
		}

                if (IDE_DRIVE_IS_CDROM(ide)) {
#ifndef RPCEMU_IDE
                        // pclog("IS CDROM - ABORT\n");
#endif
			/* Command is a NOP for CD. */
			// pclog("SPECIFY on CD-ROM is a NOP\n");
			ide->atastat = READY_STAT;
			ide_irq_raise(ide);
			return;
                }
                ide->spt=ide->secount;
                ide->hpc=ide->head+1;
                ide->atastat = READY_STAT | DSC_STAT;
#ifndef RPCEMU_IDE
//                pclog("SPECIFY - %i sectors per track, %i heads per cylinder  %i %i\n",ide->spt,ide->hpc,cur_ide[ide_board],ide_board);
#endif
                ide_irq_raise(ide);
                return;

        case WIN_PIDENTIFY: /* Identify Packet Device */
                if (IDE_DRIVE_IS_CDROM(ide)) {
//                        pclog("ATAPI identify\n");
                        ide_atapi_identify(ide);
                        ide->pos=0;
                        ide->error=0;
                        ide->atastat = DRQ_STAT | READY_STAT | DSC_STAT;
                        ide_irq_raise(ide);
                        return;
                }
//                pclog("Not ATAPI\n");
                goto abort_cmd;

        case WIN_SET_MULTIPLE_MODE:
                if (IDE_DRIVE_IS_CDROM(ide)) {
#ifndef RPCEMU_IDE
                        // pclog("IS CDROM - ABORT\n");
#endif
                        // goto abort_cmd;
			// pclog("SET_MULTIPLE_MODE on CD-ROM is a NOP\n");
			ide->atastat = READY_STAT | DSC_STAT;
			ide_irq_raise(ide);
			return;
                }
                ide->blocksize = ide->secount;
                ide->atastat = READY_STAT | DSC_STAT;
#ifndef RPCEMU_IDE
                // pclog("Set multiple mode - %i\n", ide->blocksize);
#endif
                ide_irq_raise(ide);
                return;
                
        case WIN_SETIDLE1: /* Idle */
        case 0xEF:
                goto abort_cmd;

        case WIN_IDENTIFY: /* Identify Device */
                if (IDE_DRIVE_IS_CDROM(ide)) {
			ide_set_signature(ide);
			ide->drive = 0;
                        goto abort_cmd;
                }
                if (ide->type != IDE_NONE)
                {
                        ide_identify(ide);
                        ide->pos=0;
                        ide->atastat = DRQ_STAT | READY_STAT | DSC_STAT;
			// pclog("ID callback on hard disk\n");
                        ide_irq_raise(ide);
                }
                return;

        case WIN_PACKETCMD: /* ATAPI Packet */
                if (!IDE_DRIVE_IS_CDROM(ide)) goto abort_cmd;
                // pclog("Packet callback! %i %08X\n",ide->packetstatus,ide);
                if (!ide->packetstatus)
                {
			readcdmode=0;
                        ide->pos=0;
                        ide->secount = (uint8_t)((ide->secount&0xF8)|1);
                        ide->atastat = DRQ_STAT |(ide->atastat&ERR_STAT);
                        // ide_irq_raise(ide);
                        // pclog("1 Preparing to recieve packet max DRQ count %04X\n",ide->cylinder);
                }
                else if (ide->packetstatus==1)
                {
                        ide->atastat = BUSY_STAT|(ide->atastat&ERR_STAT);
                        // pclog("Running ATAPI command 2\n");
                        atapicommand(ide_board);
//                        exit(-1);
                }
                else if (ide->packetstatus==2)
                {
                        // pclog("packetstatus==2\n");
                        ide->atastat = READY_STAT;
                        ide->secount=3;
                        ide_irq_raise(ide);
//                        if (iomd.irqb.mask&2) output=1;
                }
                else if (ide->packetstatus==3)
                {
                        ide->atastat = DRQ_STAT|(ide->atastat&ERR_STAT);
                        // pclog("Recieve data packet 3! %02X\n",ide->atastat);
                        ide_irq_raise(ide);
                        ide->packetstatus=0xFF;
                }
                else if (ide->packetstatus==4)
                {
                        ide->atastat = DRQ_STAT|(ide->atastat&ERR_STAT);
                        // pclog("Send data packet 4!\n");
                        ide_irq_raise(ide);
//                        ide.packetstatus=5;
                        ide->pos=2;
                }
                else if (ide->packetstatus==5)
                {
                        // pclog("Packetstatus 5 !\n");
                        atapicommand(ide_board);
                }
                else if (ide->packetstatus==6) /*READ CD callback*/
                {
                        ide->atastat = DRQ_STAT|(ide->atastat&ERR_STAT);
                        // pclog("Recieve data packet 6!\n");
                        ide_irq_raise(ide);
//                        ide.packetstatus=0xFF;
                }
                else if (ide->packetstatus==0x80) /*Error callback*/
                {
                        // pclog("Packet error\n");
                        ide->atastat = READY_STAT | ERR_STAT;
                        ide_irq_raise(ide);
                }
                else if (ide->packetstatus==0x81) /*Error callback with atastat already set*/
                {
                        // pclog("Packet check status\n");
                        ide->atastat = ERR_STAT;
                        ide_irq_raise(ide);
                }
                return;
        }

abort_cmd:
	ide->atastat = READY_STAT | ERR_STAT | DSC_STAT;
	ide->error = ABRT_ERR;
	ide_irq_raise(ide);
	// pclog("ABRT: IRQ raised\n");
}

void ide_callback_pri()
{
	idecallback[0] = 0;
	callbackide(0);
}

void ide_callback_sec()
{
	idecallback[1] = 0;
	callbackide(1);
}

static uint32_t atapi_event_status(IDE *ide, uint8_t *buffer)
{
	uint8_t event_code, media_status;
	media_status = 0;
	if (buffer[5])
	{
		media_status = MS_TRAY_OPEN;
		atapi->stop();
	} else
	{
		media_status = MS_MEDIA_PRESENT;
	}
	
	event_code = MEC_NO_CHANGE;
	if (media_status != MS_TRAY_OPEN)
	{
		if (!buffer[4])
		{
			event_code = MEC_NEW_MEDIA;
			atapi->load();
		}
		else if (buffer[4]==2)
		{
			event_code = MEC_EJECT_REQUESTED;
			atapi->eject();
		}
	}
	
	buffer[4] = event_code;
	buffer[5] = media_status;
	buffer[6] = 0;
	buffer[7] = 0;
	
	return 8;
}

/*ATAPI CD-ROM emulation
  This mostly seems to work. It is implemented only on Windows at the moment as
  I haven't had time to implement any interfaces for it in the generic gui.
  It mostly depends on driver files - cdrom-iso.c for ISO image files (in theory
  on any platform) and cdrom-ioctl.c for Win32 IOCTL access. There's an ATAPI
  interface defined in ide.h.
  There are a couple of bugs in the CD audio handling.
  */

static void atapi_notready(IDE *ide)
{
        /*Medium not present is 02/3A/--*/
        /*cylprecomp is error number*/
        /*SENSE/ASC/ASCQ*/
        ide->atastat = READY_STAT | ERR_STAT;    /*CHECK CONDITION*/
        ide->error = (SENSE_NOT_READY << 4) | ABRT_ERR;
        if (ide->discchanged) {
                ide->error |= MCR_ERR;
                atapi_sense.sensekey=SENSE_UNIT_ATTENTION;
                atapi_sense.asc=ASC_MEDIUM_MAY_HAVE_CHANGED;
        } else {
                atapi_sense.sensekey=SENSE_NOT_READY;
                atapi_sense.asc=ASC_MEDIUM_NOT_PRESENT;
        }
        ide->discchanged=0;
	// pclog("Not ready, packet status is now 0x80\n");
        ide->packetstatus=0x80;
        idecallback[ide->board]=50*IDE_TIME;
}

void atapi_discchanged()
{
        ext_ide->discchanged=1;
        atapi_sense.sensekey=6;
        atapi_sense.asc=0x28;
}

uint8_t changed_status = 0;

void atapi_cmd_check_status(IDE *ide)
{
	// pclog("atapi_cmd_check_status\n");
	ide->error = MCR_ERR | (6 << 4);
	ide->atastat = ERR_STAT;
	ide->secount = 0;
	ide->packetstatus = 0x81;
        idecallback[ide->board]=50*IDE_TIME;
}

void atapi_cmd_error(IDE *ide, uint8_t sensekey, uint8_t asc)
{
	ide->error = (sensekey << 4);
	ide->atastat = READY_STAT | ERR_STAT;
	ide->secount = (ide->secount & ~7) | 3;
	atapi_sense.sensekey = sensekey;
	atapi_sense.asc = asc;
        ide->discchanged=0;
	ide->packetstatus = 0x80;
        idecallback[ide->board]=50*IDE_TIME;
}

uint8_t atapi_ready_handler(IDE *ide, uint8_t command)
{
	uint8_t temp = 0;
	temp = atapi->ready();

	/* If the comamnd does not check for ready status, return 1. */
	if (!(atapi_cmd_table[command] & CHECK_READY))  return 1;

	if (!temp)
	{
		changed_status = 0;
		atapi_cmd_error(ide, SENSE_NOT_READY, ASC_MEDIUM_NOT_PRESENT);
		return 0;	/* Not ready. */
	}
	else
	{
		/* If command has ALLOW_UA flag, then don't do the disk changed check. */
		if (atapi_cmd_table[command] & ALLOW_UA)  return 1;

		if (temp == 2)
		{
			changed_status = 1;
			atapi_cmd_error(ide, SENSE_NOT_READY, ASC_MEDIUM_NOT_PRESENT);
			return 0;	/* Disc changed, we returned not ready. */
		}
		if (changed_status == 1)
		{
			changed_status = 2;
			atapi_cmd_error(ide, SENSE_UNIT_ATTENTION, ASC_MEDIUM_MAY_HAVE_CHANGED);
			return 0;	/* Disc changed, we returned medium may have changed. */
		}

		changed_status = 0;
		return 1;	/* Ready. */
	}
}

uint8_t atapi_prev;
int toctimes=0;
static uint8_t atapi_check_ready(int ide_board)
{
	IDE *ide = &ide_drives[cur_ide[ide_board]];
        uint8_t *idebufferb = (uint8_t *) ide->buffer;

	// if (changed_status == 2)
	/* Clear UNIT_ATTENTION on TEST UNIT READY, REQUEST SENSE, and READ TOC commands.
	   The clear on READ TOC is an undocumented behavior VIDE-CDD.SYS relies on. */
	if ((idebufferb[0] == 0x00) || (idebufferb[0] == 0x43))
	{
		changed_status = 0;
                atapi_prev=idebufferb[0];
       	        atapi_sense.sensekey=0;
               	atapi_sense.asc=0;
	}

	if ((atapi_sense.sensekey == 6) && (!(atapi_cmd_table[idebufferb[0]] & ALLOW_UA)))
	{
		// pclog("Unit attention, sending CHECK STATUS\n");
		atapi_cmd_check_status(ide);
		return 0;
	}

	if (!(atapi_ready_handler(ide, idebufferb[0])))
	{
		// pclog("ATAPI Command %02X: Ready handler returned NOT READY\n", idebufferb[0]);
		return 0;
	}

	// pclog("Returning ready\n");
	return 1;
}

static void atapicommand(int ide_board)
{
	IDE *ide = &ide_drives[cur_ide[ide_board]];
        uint8_t *idebufferb = (uint8_t *) ide->buffer;
	uint8_t rcdmode = 0;
        int c;
        int len;
        int msf;
        int pos;
        unsigned char temp;
        uint32_t size;
#ifndef RPCEMU_IDE
        // pclog("ATAPI command %02X %i\n",idebufferb[0],ins);
#endif
#ifdef ENABLE_FLASH
	readflash=1;
#endif
        msf=idebufferb[1]&2;
        ide->cdlen=0;

	atapi_command = idebufferb[0];

	if (!(atapi_check_ready(ide_board)))
	{
		// pclog("ATAPI: Not ready, returning\n");
		return;
	}

	/* if ((atapi_sense.sensekey == 6) && (!(atapi_cmd_table[idebufferb[0]] & ALLOW_UA)))
	{
		atapi_cmd_check_status(ide);
		return;
	}

	if (!(atapi_ready_handler(ide, idebufferb[0])))
	{
		// pclog("ATAPI Command %02X: Ready handler returned NOT READY\n", idebufferb[0]);
		return;
	} */

        if (idebufferb[0]!=REQUEST_SENSE)
        {
                atapi_prev=idebufferb[0];
                atapi_sense.sensekey=SENSE_NONE;
                atapi_sense.asc=0;
        }

        switch (idebufferb[0])
        {
	        case TEST_UNIT_READY: /*0x00*/
			ide->discchanged=1;
               	        ide->packetstatus=2;
                       	idecallback[ide_board]=50*IDE_TIME;
	                break;

        	case REQUEST_SENSE: /* Used by ROS 4+ */ /*0x03*/
			/*Will return 18 bytes of 0*/
			memset(idebufferb,0,512);
			idebufferb[0]=0x80|0x70;
			idebufferb[2]=atapi_sense.sensekey;
			idebufferb[12]=atapi_sense.asc;
                       	idebufferb[13]=atapi_sense.ascq;
        	        ide->packetstatus=3;
	                ide->cylinder=18;
        	        ide->secount=2;
                	ide->pos=0;
	                idecallback[ide_board]=60*IDE_TIME;
	                ide->packlen=18;

			/* Clear the sense stuff as per the spec. */
			changed_status = 0;
	                atapi_prev=idebufferb[0];
	       	        atapi_sense.sensekey=0;
	               	atapi_sense.asc=0;

       		        break;

		case READ_6: /*0x08*/
			readcdmode = 0;

	                ide->cdlen=idebufferb[4];
        	        ide->cdpos=((((uint32_t) idebufferb[1]) & 0x1f)<<16)|(((uint32_t) idebufferb[2])<<8)|((uint32_t) idebufferb[3]);
	                if (!ide->cdlen)
	                {
				// pclog("All done - callback set\n");
				ide->packetstatus=2;
				idecallback[ide_board]=20*IDE_TIME;
				break;
			}

	                atapi->readsector(idebufferb,ide->cdpos);
#ifdef ENABLE_FLASH
	                readflash=1;
#endif
	                ide->cdpos++;
	                ide->cdlen--;
	                if (ide->cdlen>=0) ide->packetstatus=6;
	                else               ide->packetstatus=3;
	                ide->cylinder=2048;
	                ide->secount=2;
	                ide->pos=0;
	                idecallback[ide_board]=60*IDE_TIME;
	                ide->packlen=2048;
	                return;

		case INQUIRY: /*0x12*/
	                idebufferb[0] = 0x05; /*CD-ROM*/
        	        idebufferb[1] = 0x80; /*Removable*/
	                idebufferb[2] = 0x05;
	                idebufferb[3] = 0x02;
        	        idebufferb[4] = 31;
                	idebufferb[5] = 0;
	                idebufferb[6] = 0;
	                idebufferb[7] = 0;

	                ide_padstr8(idebufferb + 8, 8, "MITSUMI "); /* Vendor */
	                ide_padstr8(idebufferb + 16, 16, "CD-ROM SR244W   "); /* Product */
	                ide_padstr8(idebufferb + 32, 4, "T01A"); /* Revision */

	                len=36;
        	        ide->packetstatus=3;
	                ide->cylinder=len;
        	        ide->secount=2;
	                ide->pos=0;
        	        idecallback[ide_board]=60*IDE_TIME;
	                ide->packlen=len;
	                break;

		case MODE_SELECT: /*0x15*/
	                if (ide->packetstatus==5)
        	        {
				ide->atastat = 0;
				// pclog("Recieve data packet!\n");
				ide_irq_raise(ide);
				ide->packetstatus=0xFF;
				ide->pos=0;
				// pclog("Length - %02X%02X\n",idebufferb[0],idebufferb[1]);
				// pclog("Page %02X length %02X\n",idebufferb[8],idebufferb[9]);
	                }
	                else
	                {
				len=idebufferb[4];
				ide->packetstatus=4;
				ide->cylinder=len;
				ide->secount=2;
				ide->pos=0;
				idecallback[ide_board]=6*IDE_TIME;
				ide->packlen=len;
	                }			
        	        return;
					
		case MODE_SENSE: /*0x1a*/
        	        len=idebufferb[4];

			// pclog("Mode sense %02X %i\n",idebufferb[2],len);
	                temp=idebufferb[2];
	                for (c=0;c<len;c++) idebufferb[c]=0;
	                len = ide_atapi_mode_sense(ide,8,temp);

			/*Set mode parameter header - bytes 0 & 1 are data length (filled out later),
				byte 2 is media type*/
	                idebufferb[0]=len>>8;
        	        idebufferb[1]=len&255;
                	idebufferb[2]=3; /*120mm data CD-ROM*/
			idebufferb[3]=0;
			// pclog("ATAPI buffer len %i\n",len);
			// pclog("Mode sense 6: %016X%016X\n", *(uint64_t *) &idebufferb[16], *(uint64_t *) &idebufferb[0]);
			ide->packetstatus=3;
			ide->cylinder=len;
			ide->secount=2;
			ide->pos=0;
	                idecallback[ide_board]=1000*IDE_TIME;
        	        ide->packlen=len;
			// pclog("Sending packet\n");
			return;

		case START_STOP: /*LOAD_UNLOAD*/ /*0x1b*/
			if (idebufferb[4]!=2 && idebufferb[4]!=3 && idebufferb[4])
	                {
				ide->atastat = READY_STAT | ERR_STAT;    /*CHECK CONDITION*/
				ide->error = (SENSE_ILLEGAL_REQUEST << 4) | ABRT_ERR;
				if (atapi_sense.sensekey == SENSE_UNIT_ATTENTION) {
					ide->error |= MCR_ERR;
				}
				ide->discchanged=0;
				atapi_sense.asc = ASC_ILLEGAL_OPCODE;
				// pclog("Packet status is now 0x80 during start/stop unit (%i)\n", idebufferb[4]);
				ide->packetstatus=0x80;
				idecallback[ide_board]=50*IDE_TIME;
				break;
				
	                }
               		if (!idebufferb[4])        atapi->stop();
	                else if (idebufferb[4]==2) atapi->eject();
        	        else                       atapi->load();
                	ide->packetstatus=2;
	                idecallback[ide_board]=50*IDE_TIME;
        	        break;

		case ALLOW_MEDIUM_REMOVAL: /*0x1e*/
	                ide->packetstatus=2;
        	        idecallback[ide_board]=50*IDE_TIME;
	                break;			

		case READ_CAPACITY_10: /*0x25*/
        	        size = atapi->size();
	                idebufferb[0] = (size >> 24) & 0xff;
        	        idebufferb[1] = (size >> 16) & 0xff;
                	idebufferb[2] = (size >> 8) & 0xff;
	                idebufferb[3] = size & 0xff;
	                idebufferb[4] = (2048 >> 24) & 0xff;
	                idebufferb[5] = (2048 >> 16) & 0xff;
	                idebufferb[6] = (2048 >> 8) & 0xff;
	                idebufferb[7] = 2048 & 0xff;
	                len=8;
	                ide->packetstatus=3;
	                ide->cylinder=len;
	                ide->secount=2;
	                ide->pos=0;
	                idecallback[ide_board]=60*IDE_TIME;
	                ide->packlen=len;
	                break;				

		case READ_10: /*0x28*/
			// pclog("Read 10 : start LBA %02X%02X%02X%02X Length %02X%02X%02X Flags %02X\n",idebufferb[2],idebufferb[3],idebufferb[4],idebufferb[5],idebufferb[6],idebufferb[7],idebufferb[8],idebufferb[9]);

			readcdmode = 0;

	                ide->cdlen=(((uint32_t) idebufferb[7])<<8)|((uint32_t) idebufferb[8]);
	                ide->cdpos=(idebufferb[2]<<24)|(idebufferb[3]<<16)|(idebufferb[4]<<8)|idebufferb[5];
	                if (!ide->cdlen)
	                {
				// pclog("All done - callback set\n");
				ide->packetstatus=2;
				idecallback[ide_board]=20*IDE_TIME;
				break;
	                }

	                atapi->readsector(idebufferb,ide->cdpos);
#ifdef ENABLE_FLASH
	                readflash=1;
#endif
	                ide->cdpos++;
	                ide->cdlen--;
	                if (ide->cdlen>=0) ide->packetstatus=6;
        	        else               ide->packetstatus=3;
	                ide->cylinder=2048;
        	        ide->secount=2;
                	ide->pos=0;
	                idecallback[ide_board]=60*IDE_TIME;
        	        ide->packlen=2048;
	                return;

		case SEEK_10: /*0x2b*/
	                pos=(idebufferb[3]<<16)|(idebufferb[4]<<8)|idebufferb[5];
        	        atapi->seek(pos);
	                ide->packetstatus=2;
        	        idecallback[ide_board]=50*IDE_TIME;
                	break;

		case READ_SUBCHANNEL: /*0x42*/
	                temp=idebufferb[2]&0x40;
	                if (idebufferb[3]!=1)
	                {
				// pclog("Read subchannel check condition %02X\n",idebufferb[3]);
				ide->atastat = READY_STAT | ERR_STAT;    /*CHECK CONDITION*/
				ide->error = (SENSE_ILLEGAL_REQUEST << 4) | ABRT_ERR;
				if (atapi_sense.sensekey == SENSE_UNIT_ATTENTION) {
					ide->error |= MCR_ERR;
                    		}
				// ext_ide->discchanged=1; /*ATAPI drivers for NT 3.1 rely on this behavoir for disc ejects/loads.*/
				ide->discchanged=1;
				atapi_sense.asc = ASC_ILLEGAL_OPCODE;
				// pclog("Packet status is now 0x80 during read subchannel (%i)\n", idebufferb[3]);
				ide->packetstatus=0x80;
                    		idecallback[ide_board]=50*IDE_TIME;
				break;
	                }
	                pos=0;
	                idebufferb[pos++]=0;
	                idebufferb[pos++]=0; /*Audio status*/
	                idebufferb[pos++]=0; idebufferb[pos++]=0; /*Subchannel length*/
	                idebufferb[pos++]=1; /*Format code*/
	                idebufferb[1]=atapi->getcurrentsubchannel(&idebufferb[5],msf);
			// pclog("Read subchannel buffer: %016X%016X, msf: %i\n", *(uint64_t *) &idebufferb[16], *(uint64_t *) &idebufferb[0], msf);
			// pclog("Read subchannel complete - audio status %02X\n",idebufferb[1]);
			// atapi->stop();
	                len=11+5;
        	        if (!temp) len=4;
	                ide->packetstatus=3;
	                ide->cylinder=len;
	                ide->secount=2;
	                ide->pos=0;
	                idecallback[ide_board]=1000*IDE_TIME;
        	        ide->packlen=len;
                	break;				

		case READ_TOC: /*0x43*/
			// pclog("Read TOC ready? %08X\n",ide);
	                toctimes++;
	                switch (idebufferb[9]>>6)
	                {
				case 0: /*Normal*/
					len=idebufferb[8]+(idebufferb[7]<<8);
					len=atapi->readtoc(idebufferb,idebufferb[6],msf,len,0);
					break;

				case 1: /*Multi session*/
					len=idebufferb[8]+(idebufferb[7]<<8);
					atapi->readtoc_session(idebufferb,msf,len);
					idebufferb[0]=0; idebufferb[1]=0xA;
					break;

				case 2: /*Raw*/
					len=idebufferb[8]+(idebufferb[7]<<8);
					len=atapi->readtoc_raw(idebufferb,len);
					break;

				default:
					ide->atastat = READY_STAT | ERR_STAT;    /*CHECK CONDITION*/
					ide->error = (SENSE_ILLEGAL_REQUEST << 4) | ABRT_ERR;
					if (atapi_sense.sensekey == SENSE_UNIT_ATTENTION) {
						ide->error |= MCR_ERR;
					}
					// pclog("Packet status is now 0x80 during read TOC ((idebufferb[9] >> 6) = %i)\n", idebufferb[9] >> 6);
					ide->discchanged=1;
					ide->packetstatus=0x80;
					idecallback[ide_board]=50*IDE_TIME;
					break;
			}
			// pclog("ATAPI buffer len %i\n",len);
	                ide->packetstatus=3;
	                ide->cylinder=len;
	                ide->secount=2;
	                ide->pos=0;
	                idecallback[ide_board]=60*IDE_TIME;
        	        ide->packlen=len;
                	return;

		case READ_HEADER: /*0x44*/
			if (msf)
			{
				ide->atastat = READY_STAT | ERR_STAT;    /*CHECK CONDITION*/
				ide->error = (SENSE_ILLEGAL_REQUEST << 4) | ABRT_ERR;
				if (ide->discchanged) {
					ide->error |= MCR_ERR;
				}
				ide->discchanged=0;
				atapi_sense.asc = ASC_ILLEGAL_OPCODE;
				// pclog("Packet status is now 0x80 during read header\n");
				ide->packetstatus=0x80;
				idecallback[ide_board]=50*IDE_TIME;
				break;
				// pclog("Read Header MSF!\n");
				// exit(-1);
			}
			for (c=0;c<4;c++) idebufferb[c+4]=idebufferb[c+2];
			idebufferb[0]=1; /*2048 bytes user data*/
			idebufferb[1]=idebufferb[2]=idebufferb[3]=0;

			ide->packetstatus=3;
			ide->cylinder=8;
			ide->secount=2;
			ide->pos=0;
			idecallback[ide_board]=60*IDE_TIME;
			ide->packlen=8;
			return;

		case PLAY_AUDIO_10: /*0x45*/
			pos=(idebufferb[2]<<24)|(idebufferb[3]<<16)|(idebufferb[4]<<8)|idebufferb[5];
			len=(idebufferb[7]<<8)|idebufferb[8];
			atapi->playaudio(pos,len,0);
			ide->packetstatus=2;
			idecallback[ide_board]=50*IDE_TIME;
			break;

		case GET_CONFIGURATION: /*0x46*/
			if (idebufferb[2] != 0 || idebufferb[3] != 0)
			{
				ide->atastat = READY_STAT | ERR_STAT;
				ide->error = (SENSE_ILLEGAL_REQUEST << 4) | ABRT_ERR;
				if (atapi_sense.sensekey == SENSE_UNIT_ATTENTION) {
					ide->error |= MCR_ERR;
				}
				ide->discchanged=0;
				atapi_sense.asc = ASC_INV_FIELD_IN_CMD_PACKET;
				ide->packetstatus=0x80;
				idecallback[ide_board]=50*IDE_TIME;
				break;
			}
			/*
			* the number of sectors from the media tells us which profile
			* to use as current.  0 means there is no media.
			*/
			pos = (idebufferb[2]<<24)|(idebufferb[3]<<16)|(idebufferb[4]<<8)|idebufferb[5];
			len = (idebufferb[7]<<8)|idebufferb[8];
			idebufferb[10] = 0x02 | 0x01;
			len = 8 + 4;
			ide->packetstatus=3;
			ide->cylinder=len;
			ide->secount=2;
			ide->pos=0;
			idecallback[ide_board]=60*IDE_TIME;
			ide->packlen=len;
			break;
			
		case PLAY_AUDIO_MSF: /*0x47*/
	                pos=(idebufferb[3]<<16)|(idebufferb[4]<<8)|idebufferb[5];
	                len=(idebufferb[6]<<16)|(idebufferb[7]<<8)|idebufferb[8];
	                atapi->playaudio(pos,len,1);
	                ide->packetstatus=2;
	                idecallback[ide_board]=50*IDE_TIME;
	                break;

		case GET_EVENT_NOTIFICATION: /*0x4a*/
			if (idebufferb[1] != 0)
			{
				ide->atastat = READY_STAT | ERR_STAT;
				ide->error = (SENSE_ILLEGAL_REQUEST << 4) | ABRT_ERR;
				if (atapi_sense.sensekey = SENSE_UNIT_ATTENTION)
				{
					ide->error |= MCR_ERR;
				}
				ide->discchanged=0;
				atapi_sense.asc=ASC_INV_FIELD_IN_CMD_PACKET;
				ide->packetstatus=0x80;
				idecallback[ide_board]=50*IDE_TIME;
				break;
			}
			
			uint8_t events, class;
			
			events = 1 << GESN_MEDIA;
			class = 0;
			
			if (idebufferb[4] & (1 << GESN_MEDIA))
			{
				class |= GESN_MEDIA;
				len = atapi_event_status(ide, idebufferb);
			}
			else
			{
				class = 0x80;
				len = idebufferb[4];
			}
			len=(idebufferb[7]<<8)|idebufferb[8]; 
			
			ide->cylinder=len;
			ide->packetstatus=3;
			ide->secount=2;
			ide->pos=0;
			idecallback[ide_board]=50*IDE_TIME;
			ide->packlen=len;
			break;
					
		case PAUSE_RESUME: /*0x4b*/
	                if (idebufferb[8]&1) atapi->resume();
        	        else                 atapi->pause();
	                ide->packetstatus=2;
        	        idecallback[ide_board]=50*IDE_TIME;
	                break;

	        case STOP_PLAY_SCAN:
        	        if (!atapi->ready()) { atapi_notready(ide); return; }
	                atapi->stop();
			ide->packetstatus=2;
        	        idecallback[ide_board]=50*IDE_TIME;
	                break;

		case READ_DISC_INFORMATION:
			idebufferb[1] = 32;
			idebufferb[2] = 0xe; /* last session complete, disc finalized */
			idebufferb[3] = 1; /* first track on disc */
			idebufferb[4] = 1; /* # of sessions */
			idebufferb[5] = 1; /* first track of last session */
			idebufferb[6] = 1; /* last track of last session */
			idebufferb[7] = 0x20; /* unrestricted use */
			idebufferb[8] = 0x00; /* CD-ROM */
			
			len=34;
			ide->packetstatus=3;
			ide->cylinder=len;
			ide->secount=2;
			ide->pos=0;
			idecallback[ide_board]=60*IDE_TIME;
			ide->packlen=len;			
			break;
					
		case MODE_SELECT_10: /*0x55*/
	                if (ide->packetstatus==5)
        	        {
				ide->atastat = 0;
				// pclog("Recieve data packet!\n");
				ide_irq_raise(ide);
				ide->packetstatus=0xFF;
				ide->pos=0;
				// pclog("Length - %02X%02X\n",idebufferb[0],idebufferb[1]);
				// pclog("Page %02X length %02X\n",idebufferb[8],idebufferb[9]);
	                }
	                else
	                {
				len=(idebufferb[7]<<8)|idebufferb[8];
				ide->packetstatus=4;
				ide->cylinder=len;
				ide->secount=0;
				ide->pos=0;
				idecallback[ide_board]=6*IDE_TIME;
				ide->packlen=len;
	                }
        	        return;

		case MODE_SENSE_10: /*0x5a*/
	                // pclog("Mode sense - ready?\n");
	                len=(idebufferb[8]|(idebufferb[7]<<8));

			// pclog("Mode sense 10 %02X %i\n",idebufferb[2],len);
	                temp=idebufferb[2];
	                for (c=0;c<len;c++) idebufferb[c]=0;
	                len = ide_atapi_mode_sense(ide,8,temp);

	                /*Set mode parameter header - bytes 0 & 1 are data length (filled out later),
				byte 2 is media type*/
	                idebufferb[0]=len>>8;
	                idebufferb[1]=len&255;
	                idebufferb[2]=3; /*120mm data CD-ROM*/
			// idebufferb[3]=0;
			// pclog("ATAPI buffer len %i\n",len);
			// pclog("Mode sense 10: buffer %016X%016X\n", *(uint64_t *) &idebufferb[16], *(uint64_t *) &idebufferb[0]);
			ide->packetstatus=3;
			ide->cylinder=len;
			ide->secount=2;
			// ide.atastat = DRQ_STAT;
			ide->pos=0;
			idecallback[ide_board]=1000*IDE_TIME;
			ide->packlen=len;
			// pclog("Sending packet\n");
			return;

		case PLAY_AUDIO_12: /*0xa5*/
	                /*This is apparently deprecated in the ATAPI spec, and apparently
	                  has been since 1995 (!). Hence I'm having to guess most of it*/
        	        pos=(idebufferb[3]<<16)|(idebufferb[4]<<8)|idebufferb[5];
	                len=(idebufferb[7]<<16)|(idebufferb[8]<<8)|idebufferb[9];
	                atapi->playaudio(pos,len,0);
	                ide->packetstatus=2;
	                idecallback[ide_board]=50*IDE_TIME;
	                break;

		case READ_12: /*0xa8*/
			readcdmode = 0;

			// pclog("Read 10 : start LBA %02X%02X%02X%02X Length %02X%02X%02X Flags %02X\n",idebufferb[2],idebufferb[3],idebufferb[4],idebufferb[5],idebufferb[6],idebufferb[7],idebufferb[8],idebufferb[9]);

	                ide->cdlen=(((uint32_t) idebufferb[6])<<24)|(((uint32_t) idebufferb[7])<<16)|(((uint32_t) idebufferb[8])<<8)|((uint32_t) idebufferb[9]);
	                ide->cdpos=(((uint32_t) idebufferb[2])<<24)|(((uint32_t) idebufferb[3])<<16)|(((uint32_t) idebufferb[4])<<8)|((uint32_t) idebufferb[5]);
	                if (!ide->cdlen)
	                {
				// pclog("All done - callback set\n");
				ide->packetstatus=2;
				idecallback[ide_board]=20*IDE_TIME;
				break;
	                }

	                atapi->readsector(idebufferb,ide->cdpos);
#ifdef ENABLE_FLASH
	                readflash=1;
#endif
	                ide->cdpos++;
	                ide->cdlen--;
	                if (ide->cdlen>=0) ide->packetstatus=6;
        	        else               ide->packetstatus=3;
	                ide->cylinder=2048;
        	        ide->secount=2;
                	ide->pos=0;
	                idecallback[ide_board]=60*IDE_TIME;
        	        ide->packlen=2048;
	                return;	
					
		case SET_CD_SPEED: /*0xbb*/
	                ide->packetstatus=2;
	                idecallback[ide_board]=50*IDE_TIME;
	                break;
		
		case MECHANISM_STATUS: /*0xbd*/
		{
			len=(idebufferb[7]<<16)|(idebufferb[8]<<8)|idebufferb[9];
			
			idebufferb[2] = 0;
			idebufferb[3] = 0;
			idebufferb[4] = 0;
			idebufferb[5] = 1;
			len = 8;
			
			ide->cylinder=len;
			ide->packetstatus=3;
			ide->secount=2;
			ide->pos=0;
			idecallback[ide_board]=60*IDE_TIME;
			ide->packlen=len;	
			break;			
		}
		
		case READ_CD: /*0xbe*/
			// pclog("Read CD : start LBA %02X%02X%02X%02X Length %02X%02X%02X Flags %02X\n",idebufferb[2],idebufferb[3],idebufferb[4],idebufferb[5],idebufferb[6],idebufferb[7],idebufferb[8],idebufferb[9]);
			rcdmode = idebufferb[9] & 0xF8;
	                if ((rcdmode != 0x10) && (rcdmode != 0xF8))
	                {
				ide->atastat = READY_STAT | ERR_STAT;    /*CHECK CONDITION*/
				ide->error = (SENSE_ILLEGAL_REQUEST << 4) | ABRT_ERR;
				if (atapi_sense.sensekey == SENSE_UNIT_ATTENTION) {
					ide->error |= MCR_ERR;
				}
				ide->discchanged=0;
				atapi_sense.asc = ASC_ILLEGAL_OPCODE;
				// pclog("Packet status is now 0x80 during read CD (%i)\n", idebufferb[9]);
				ide->packetstatus=0x80;
				idecallback[ide_board]=50*IDE_TIME;
				break;
	                }
	                ide->cdlen=(idebufferb[6]<<16)|(idebufferb[7]<<8)|idebufferb[8];
	                ide->cdpos=(idebufferb[2]<<24)|(idebufferb[3]<<16)|(idebufferb[4]<<8)|idebufferb[5];
			// pclog("Read at %08X %08X\n",ide.cdpos,ide.cdpos*2048);
			if (rcdmode == 0x10)
		                atapi->readsector(idebufferb,ide->cdpos);
			else
		                atapi->readsector_raw(idebufferb,ide->cdpos);
#ifndef ENABLE_FLASH
	                readflash=1;
#endif
			readcdmode = (rcdmode == 0xF8);
	                ide->cdpos++;
	        	        ide->cdlen--;
        	        if (ide->cdlen>=0) ide->packetstatus=6;
                	else               ide->packetstatus=3;
	                ide->cylinder=(idebufferb[9] == 0x10) ? 2048 : 2352;
				ide->secount=2;
	                ide->pos=0;
        	        idecallback[ide_board]=60*IDE_TIME;
                	ide->packlen=(idebufferb[9] == 0x10) ? 2048 : 2352;
	                return;						
			
		case SEND_DVD_STRUCTURE:
		default:
	                ide->atastat = READY_STAT | ERR_STAT;    /*CHECK CONDITION*/
	                ide->error = (SENSE_ILLEGAL_REQUEST << 4) | ABRT_ERR;
        	        if (atapi_sense.sensekey == SENSE_UNIT_ATTENTION) {
                	        ide->error |= MCR_ERR;
	                }
	                ide->discchanged=0;
	                atapi_sense.asc = ASC_ILLEGAL_OPCODE;
			// pclog("Packet status is now 0x80: Generating DVD structure is not supported\n");
	                ide->packetstatus=0x80;
	                idecallback[ide_board]=50*IDE_TIME;
	                break;                
        }
}

static void callreadcd(IDE *ide)
{
        ide_irq_lower(ide);
        if (ide->cdlen<=0)
        {
		// pclog("Callback: cdlen=%i, pos=%i, cdpos=%i, cylinder=%i, atastat=%i, secount=%i\n", ide->cdlen, ide->pos, ide->cdpos, ide->cylinder, ide->atastat, ide->secount);
//                pclog("All done - callback set\n");
                ide->packetstatus=2;
                idecallback[ide->board]=20*IDE_TIME;
                return;
        }
        // pclog("Continue readcd! %i blocks left\n",ide->cdlen);
        ide->atastat = BUSY_STAT;

	if (readcdmode)
	        atapi->readsector_raw((uint8_t *) ide->buffer, ide->cdpos);
	else
	        atapi->readsector((uint8_t *) ide->buffer, ide->cdpos);
#ifndef RPCEMU_IDE
	readflash=1;
#endif
	ide->cdpos++;
	ide->cdlen--;
	ide->packetstatus=6;
	ide->cylinder=readcdmode ? 2352 : 2048;
	ide->secount=2;
	ide->pos=0;
	idecallback[ide->board]=60*IDE_TIME;
	ide->packlen=readcdmode ? 2352 : 2048;
}

void ide_write_pri(uint16_t addr, uint8_t val, void *priv)
{
        writeide(0, addr, val);
}
void ide_write_pri_w(uint16_t addr, uint16_t val, void *priv)
{
        writeidew(0, val);
}
void ide_write_pri_l(uint16_t addr, uint32_t val, void *priv)
{
        writeidel(0, val);
}
uint8_t ide_read_pri(uint16_t addr, void *priv)
{
        return readide(0, addr);
}
uint16_t ide_read_pri_w(uint16_t addr, void *priv)
{
        return readidew(0);
}
uint32_t ide_read_pri_l(uint16_t addr, void *priv)
{
        return readidel(0);
}

void ide_write_sec(uint16_t addr, uint8_t val, void *priv)
{
        writeide(1, addr, val);
}
void ide_write_sec_w(uint16_t addr, uint16_t val, void *priv)
{
        writeidew(1, val);
}
void ide_write_sec_l(uint16_t addr, uint32_t val, void *priv)
{
        writeidel(1, val);
}
uint8_t ide_read_sec(uint16_t addr, void *priv)
{
        return readide(1, addr);
}
uint16_t ide_read_sec_w(uint16_t addr, void *priv)
{
        return readidew(1);
}
uint32_t ide_read_sec_l(uint16_t addr, void *priv)
{
        return readidel(1);
}

void ide_pri_enable()
{
        io_sethandler(0x01f0, 0x0008, ide_read_pri, ide_read_pri_w, ide_read_pri_l, ide_write_pri, ide_write_pri_w, ide_write_pri_l, NULL);
        io_sethandler(0x03f6, 0x0001, ide_read_pri, NULL,           NULL,           ide_write_pri, NULL,            NULL           , NULL);
}

void ide_pri_enable_custom(uint16_t port1, uint16_t port2)
{
        if (port1)  io_sethandler(port1, 0x0008, ide_read_pri, ide_read_pri_w, ide_read_pri_l, ide_write_pri, ide_write_pri_w, ide_write_pri_l, NULL);
        if (port2)  io_sethandler(port2, 0x0001, ide_read_pri, NULL,           NULL,           ide_write_pri, NULL,            NULL           , NULL);
}

void ide_pri_disable()
{
        io_removehandler(0x01f0, 0x0008, ide_read_pri, ide_read_pri_w, ide_read_pri_l, ide_write_pri, ide_write_pri_w, ide_write_pri_l, NULL);
        io_removehandler(0x03f6, 0x0001, ide_read_pri, NULL,           NULL,           ide_write_pri, NULL,            NULL           , NULL);
}

void ide_sec_enable()
{
        io_sethandler(0x0170, 0x0008, ide_read_sec, ide_read_sec_w, ide_read_sec_l, ide_write_sec, ide_write_sec_w, ide_write_sec_l, NULL);
        io_sethandler(0x0376, 0x0001, ide_read_sec, NULL,           NULL,           ide_write_sec, NULL,            NULL           , NULL);
}

void ide_sec_enable_custom(uint16_t port1, uint16_t port2)
{
        if (port1)  io_sethandler(port1, 0x0008, ide_read_sec, ide_read_sec_w, ide_read_sec_l, ide_write_sec, ide_write_sec_w, ide_write_sec_l, NULL);
        if (port2)  io_sethandler(port2, 0x0001, ide_read_sec, NULL,           NULL,           ide_write_sec, NULL,            NULL           , NULL);
}

void ide_sec_disable()
{
        io_removehandler(0x0170, 0x0008, ide_read_sec, ide_read_sec_w, ide_read_sec_l, ide_write_sec, ide_write_sec_w, ide_write_sec_l, NULL);
        io_removehandler(0x0376, 0x0001, ide_read_sec, NULL,           NULL,           ide_write_sec, NULL,            NULL           , NULL);
}

void ide_init()
{
        ide_pri_enable();
#ifndef RELEASE_BUILD
	pclog("maxide is %i\n", maxide);
#endif
        if (maxide == 4)
	{
		ide_sec_enable();
	}
	else
	{
		ide_sec_disable();
	}
        ide_bus_master_read_sector = ide_bus_master_write_sector = NULL;
        
        timer_add(ide_callback_pri, &idecallback[0], &idecallback[0],  NULL);
        if (maxide == 4)  timer_add(ide_callback_sec, &idecallback[1], &idecallback[1],  NULL);
}

void ide_set_bus_master(int (*read_sector)(int channel, uint8_t *data), int (*write_sector)(int channel, uint8_t *data), void (*set_irq)(int channel))
{
        ide_bus_master_read_sector = read_sector;
        ide_bus_master_write_sector = write_sector;
        ide_bus_master_set_irq = set_irq;
}
