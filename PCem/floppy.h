#define SECTOR_OK 0xEF

#define CLASS_400 0	/* 250 kbps at 360 rpm */
#define CLASS_500 1	/* 250 kbps at 300 rpm or 300 kbps at 360 rpm */
#define CLASS_600 2	/* 300 kbps at 300 rpm */
#define CLASS_800 3	/* 250 kbps at 360 rpm */
#define CLASS_1000 4	/* 250 kbps at 300 rpm */
#define CLASS_1200 5	/* 300 kbps at 300 rpm */
#define CLASS_1600 6	/* 500 kbps at 360 rpm */
#define CLASS_2000 7	/* 500 kbps at 300 rpm */
#define CLASS_3200 8	/* 1000 kbps at 360 rpm */
#define CLASS_4000 9	/* 1000 kbps at 300 rpm */
/* There *ARE* some real floppies with 2 megabyte data rate and relevant capacity, but I am not sure if they actually work
   with the SMSC FDC's 2 megabyte data rate mode; they are also very rare, usually one-off models. */
#define CLASS_6400 10	/* 2000 kbps at 360 rpm */
#define CLASS_8000 11	/* 2000 kbps at 300 rpm */
#define CLASS_INV -1

// 3.5" 1.44M, 2.88M
#define M_35_2H2E 0xf0
// 3.5"/5.25" 360k 1 Side
#define M_35_1D9 0xf8
// 3.5"/5.25" 720k, 5.25" 1.2M, 2.4M
#define M_35_2D9_525_2H2E 0xf9
// 3.5"/5.25" 320k 1 Side
#define M_35_1D8 0xfa
// 3.5"/5.25" 640k
#define M_35_2D8 0xfb
// 5.25" 180k
#define M_525_1D9 0xfc
// 5.25" 360k 2 Sides
#define M_525_2D9 0xfd
// 5.25" 160k, 3.5"/5.25" 1.25M
#define M_35_3M_525_1D8 0xfe
// 5.25" 320k 2 Sides
#define M_525_2D8 0xff

#define RATE_500K 0
#define RATE_300K 1
#define RATE_250K 2
#define RATE_1M 3

#define DRT_NORMAL 0
#define DRT_3MODE 1
#define DRT_2MEG 2
#define DRT_2MEGEX 3

#define DEN_DD 0
#define DEN_HD 1
#define DEN_ED 2
// #define DEN_NONE 3
#define DEN_2MEG 3

#define DT_12M 0
#define DT_360K 1
#define DT_144M 2
#define DT_720K 3

#define IMGT_NONE 0
#define IMGT_RAW 1
#define IMGT_PEF 2
#define IMGT_FDI 3

/*FDC*/
typedef struct FDC
{
        uint8_t dor,stat,command,dat,st0,st1,st2;
        uint8_t head[2],track[2],pcn[2],sector[2],drive,lastdrive;
        int pos[2];
        uint8_t params[64];
        uint8_t res[64];
        int pnum,ptot;
        uint8_t rate;
        uint8_t specify[64];
        uint8_t eot[2];
        uint8_t lock;
        int perp;
        uint8_t config, pretrk;
        uint8_t abort[2];
        uint8_t dsr;
        int deldata;
        uint8_t tdr;
        uint8_t format_started[2];
        uint8_t relative;
        uint8_t direction;
        uint8_t dma;
        uint8_t fillbyte[2];
        int fdmaread[2];
        uint8_t fifo;
        uint8_t tfifo;
        uint8_t fifobuf[16];
        uint8_t fifobufpos;
	uint8_t eis;
	uint8_t scan_wildcard;
        int gotdata[2];
        
        uint8_t pcjr;
        
        int watchdog_timer;
        int watchdog_count;

	int dskchg_activelow;
} FDC;

FDC fdc;

void fdc_poll();
extern int timetolive;

extern int output;
extern int lastbyte;

/*      Preliminary map of the header of PCem image format:

        0x00000000      [dword]         Magic ("PCem")
        0x00000004      [dword]         2nd part of Magic
                                            First 3 byuts are always 77 0F D8
                                            Last byte is 0xCC if the image has regular extra sector data and raw dump,
                                            and 0x2E if it has an actual FM/MFM/Perpendicular stream dump.
                                            In the latter case, byte E is unused, and byte D has the following meaning:
                                                Bit 0: This byte is used? (0 = no, 1 = yes)
                                                Bit 1: PEF contains the DATA part of sectors? (0 = no, 1 = yes)
                                                In case both of these bits are set, 0x00000010 to 0x00000100F contain the file name
                                                of the raw image.
        0x00000008      [byte]          Sides and Bytes per Sector (0 = 128)
                                        Same multiplexing as for multiplexed sector ID's
        0x00000009      [byte]          Tracks
        0x0000000A      [byte]          Sectors per Track
        0x0000000B      [byte]          Media Type ID
        0x0000000C      [byte]          Write protection byte:
                                        Bit 0: This byte is used? (0 = no, ignore it, 1 = yes)
                                        Bit 1: Write protected? (0 = no, 1 = yes)
        0x0000000D      [byte]          PEF format specification byte:
                                        Bit 0: This byte is used? (0 = no, ignore it, 1 = yes)
                                        Bit 1: PEF contains sector states? (0 = no (and read bit 2), 1 = yes (and ignore bit 2))
                                        Bit 2: PEF contains extra sector data? (0 = no, 1 = yes)
                                        Effectively, this means it contains sector ID, CRC of ID, and CRC of data for each sector.
                                        Bit 3 (only if 1,2 is 10, otherwise ignore):
                                                0       Head number and sector size code are stored separately (ie. we store C,H,R,N,CRC-APPR)
                                                1       Head number and sector size code are multiplexed into a single byte (ie. we store C,HN,R,CRC-APPR)
                                                        Head is stored as bits 6 and 7, of which 6 is effective head number, 7 is bit 8 (for 2M purposes)
                                        Bit 4: PEF contains raw data (0 = no, 1 = yes)
                                                If bit 4 is clear, 0x00000010 to 0x0000010F contain the file name of the raw image.
        0x0000000E      [byte]          PEF raw image type specification byte:
                                        Bit 0: This byte is used? (0 = no, ignore it, 1 = yes)
                                        Bit 1-4: Image type:
                                                000     Normal
                                                001-011 XDF
                                                100-101 XXDF
                                                110     2M
                                                111     2MGUI
                                                1000-1111Reserved
                                                These 4 bits are ignored if bits 1 and 2 at 0x00000009 are set to 10.
                                        Bits 5-7: Media class:
                                                (essentially, we only need 250/500 (1DD, 2DD 5.25"), 500/1000 (1QD/2QD 5.25", 1DD/2DD 3.5"),
                                                800/1600 (1HD/2HD 5.25", 3.5" 3Mode), 1000/2000 (1HD/2HD 3.5"), 1600/3200 (1ED/2ED 5.25", 3.5" 3Mode),
                                                2000/4000 (1ED/2ED 3.5")
                                                000      208/ 416       Possible 5.25" 1DD/2DD media for 360 rpm/300 kbps rate*
                                                001      250/ 500       1DD/2DD 5.25"
                                                010      416/ 833       Possible 5.25" 1QD/2QD media for 360 rpm/300 kbps rate*
                                                011      500/1000       1QD/2QD 5.25", 1DD/2DD 3.5"
                                                100      833/1666       1HD/2HD 5.25", 3.5" 3-Mode
                                                101     1000/2000       1HD/2HD 3.5"
                                                110     1666/3333       1ED/2ED 2.5", 3.5" 3-Mode
                                                111     2000/4000       1ED/2ED 3.5"
                                                This allows the PEF image to distinguish between a 1600-class 1.44 MB floppy and a 2000-class one, for example.
                                                * Not sure if this kind of media exist in existence (but TESTFDC testing a single-sided format at 300 kbps points at yes).
        0x0000000F      [1 byte]        Sectors per non-zero track (only if bits 1-4 of 0x0000000A are set to anything other than 0000 and bit 0 of 0x0000000A is set)
        0x00000010-10F  [255 bytes]     Null-terminated file name of the raw image (if bit 1 of 0x00000009 is set and bit 4 is clear)
        0x00000?10      Sector extra data if bit 0 of 0x00000009 is clear or bit 1,2 of 0x00000009 is set to 01 or 10:
                        [H*T*SPT]       Sector states if bit 0 of 0x00000009 is clear or bit 1,2 of 0x00000009 is set to 01
                                        42848 bytes maximum (that's to keep it aligned at 16 bytes).
                                        2*85*255 maximum + 1 terminator with bit 7 clear.
                        [H*T*SPT*5]     Sector states if bit 1,2 of 0x00000009 is set to 10 and bit 3 of 0x00000009 is set to 0
                                        171376 bytes maximum (that's to keep it aligned at 16 bytes).
                                        2*85*255*5 maximum + 1 terminator with all the bytes set to 255.
                        [H*T*SPT*4]     Sector states if bit 1,2 of 0x00000009 is set to 10 and bit 3 of 0x00000009 is set to 1
                                        214208 bytes maximum (that's to keep it aligned at 16 bytes).
                                        2*85*255*4 maximum + 1 terminator with all the bytes set to 255.
        0x????????      Raw image data
                        [255 bytes]     Raw image file name if bit 4 of 0x00000009 is clear and bit 1 of the same byte is set to 1
                        [H*T*SPT*BPS]   Raw image data if bit 4 of 0x00000009 is set to 1 or bit 1 of the same byte is clear

        Sector status flag:
        Bit 7 - IDAM (ID address mark): 1 - Present, 0 - Absent;
        Bit 6 - IAM (index address mark - track start only): 1 - Present, 0 - Absent;
        Bit 4,5 - DAM (Data address mark): 11 - Present, 10 - Reserved, 01 - Deleted, 00 - Absent;
        Bit 3 - Data CRC presence: 1- Present, 0 - Absent;
        Bit 2 - ID CRC presence: 1- Present, 0 - Absent;
        Bit 1 - Data CRC stats: 1 = OK, 0 = Data Error;
        Bit 0 - ID CRC status: 1 = OK, 0 = Data Error.

        Possibble 
        A good sector has all these bits set, and therefore yields 0xFF, or 0xBF for any non-first sector of the track.
        A non-set sector ID has bits 2, 3 clear and bits 0, 1 set.

        Padded so header is exactly 32768 bytes.


        Supported floppy drive configurations:

        5.25"?          Density         3-Mode          Description                                             Supported PEF media classes
        -----------------------------------------------------------------------------------------------------------------------------------
        1               0               0               5.25" 360 kB DD drive                                   0
        1               0               1               5.25" 720 kB QD drive                                   0, 1
        1               1               0               5.25" 1.2 MB HD drive, only 360 rpm                     0, 1, 2
        1               1               1               5.25" 1.2 MB HD drive, 300 and 360 rpm                  0, 1, 2
        1               2               0               5.25" 2.4 MB HD drive, only 360 rpm*                    0, 1, 2, 4
        1               2               1               5.25" 2.4 MB HD drive, 300 and 360 rpm*                 0, 1, 2, 4
        1               3               Any             Invalid
        0               0               Any             3.5" 720 kB DD drive                                    1
        0               1               0               3.5" 720 kB/1.44 MB HD drive                            1, 3
        0               1               1               3.5" 720 kB/1.25 MB/1.44 MB 3-mode HD drive             1, 2, 3
        0               2               0               3.5" 720 kB/1.44 MB/2.88 MB ED drive                    1, 3, 5
        0               2               1               3.5" 720 kB/1.25 MB/1.44 MB/2.5 MB/2.88 MB 3-mode ED d.*1, 2, 3, 4, 5
        0               3               0               Invalid
        0               3               1               None

                                                        * Not supported by BIOS, but can be supported by appropriate OS driver.

        Bit field:
        7       6       5       4       3       2       1       0
                                        5.25    Dens.0  Dens.1  3-Mode

        The combo box will contain: 0, 2, 3, 4, 8, 9, 10, 11


        Note that the 1.2M and 1.25M (ie. 3-mode) 5.25" floppies are not supported on 720k or 2.88 MB drives:
        If the BIOS sees the drive is not 1.44 MB, it won't bother doing the Mode 3 check at all.

        That is, however, a driver fallacy. The Win9x driver, for example, could be modified to support 3-mode
        on, say, 2.88 MB drives as well.

        Also 5.25" 1.25M (77-track) floppies are not supported by the FDC driver, however they exist in practice,
        and the format actually originated from 8" floppies.    */

uint8_t disc_3f7;

uint8_t current_scid[5];

extern uint8_t OLD_BPS;
extern uint8_t OLD_SPC;
extern uint8_t OLD_C;
extern uint8_t OLD_H;
extern uint8_t OLD_R;
extern uint8_t OLD_N;

extern uint8_t flag;
extern int curoffset;

extern int tempdiv;

typedef struct
{
        uint8_t         magic0;
        uint8_t         magic1;
        uint8_t         magic2;
        uint8_t         magic3;
        uint8_t         sides_bps;
        uint8_t         tracks;
        uint8_t         spt;
        uint8_t         mid;
        uint8_t         params0;
        uint8_t         params1;
        uint8_t         params2;
        uint8_t         res0;
        uint8_t         res1;
        uint8_t         res2;
        uint8_t         res3;
        uint8_t         res4;
}
pef_header;

typedef struct
{
        uint32_t        fdi_data_0;
        uint32_t        fdi_data_1;
        uint32_t        rawoffs;
        uint32_t        rawsize;
        uint32_t        bps;
        uint32_t        spt;
        uint32_t        sides;
        uint32_t        tracks;
}
fdi_header;

uint8_t xdf_track0[11][3];
uint8_t xdf_spt[11];
uint8_t xdf_map[11][24][3];

uint8_t get_h_from_hn(uint8_t hn);
uint8_t get_n_from_hn(uint8_t hn);

uint8_t current_state();
int ss_good_sector1(uint8_t state);
int ss_good_other(uint8_t state);
int ss_good(uint8_t state);
int ss_idam_present(uint8_t state);
int ss_iam_present(uint8_t state);
int ss_dam_nondel(uint8_t state);
int ss_dam_present(uint8_t state);
int ss_data_crc_present(uint8_t state);
int ss_id_crc_present(uint8_t state);
int ss_data_crc_correct(uint8_t state);
int ss_id_crc_correct(uint8_t state);

int getshift(int val);
int get_class(int total, int sides, uint8_t mid);
int getmaxbytes(int d);
int getminbytes(int d);
int samediskclass(int d, int c, int s, int n);

void initsectors(int d);
void defaultsstates(int d);
void process_byte_A(int d, uint8_t byteA);
void create_byte_A(int d, uint8_t *byteA);
void ejectdisc(int d);
void set_sector_id(int d, int t, int h, int s, int sid, int nb);
void set_sector_id_nh(int d, int t, int h, int s, int sid, int nb);
void set_sector_id_2m(int d, int t, int h, int s, int sid, int nb);
void read_raw_sectors(FILE *f, int d, uint8_t st, uint8_t nt, uint8_t sh, uint8_t nh, uint8_t ss2, uint8_t ns, int nb, uint8_t si);

void read_normal_floppy(FILE *f, int d);
void pef_set_spt(int d);
void pef_read_advanced_floppy(FILE *f, int d);
void initialize_xdf_maps();
void read_xdf_track0(FILE *f, int d, int sfat, int se, int sg);
void read_xdf(FILE *f, int d, int xdft);
void read_2m_track0(FILE *f, int d, int sfat, int se, int sg);
void read_2m(FILE *f, int d, int xdft);
int guess_geometry(FILE *f, int d);
void set_xdf(int d);
void set_class_and_xdf(int d);
void read_fdi_header(FILE *f, int d);
void read_sector_state(FILE *f, int d, int h, int t, int s);
void read_normal_scid(FILE *f);
void read_multiplexed_scid(FILE *f);
void read_sstates(FILE *f, int d);
void read_scids(FILE *f, int d);
void read_sector_info(FILE *f, int d);

void pef_read_header(FILE *f, int d);
void pef_set_info(int d);
int pef_open_ext_raw_image(FILE **f, int d);

void fdi_read_header(FILE *f, int d);
void fdi_set_info(FILE *f, int d);

int raw_set_info(FILE *f, int d);

void init_raw_image_fn_buffers();
int is_2m(FILE *f, int d);
int is_nonzero_sector_size(uint8_t size);
void find_next_big_scid(int st, int *c, int *h, int *r, int *n, int *sn);
void clear_sectors();
void consolidate_special_sectors();
int img_type(FILE *f, int d);
void read_raw_or_fdi(FILE *f, int d);
void clear_sector_states(int d);
void floppy_load_image(int d, char *fn);

void freetracksectors(int d, int h, int t);

typedef struct FDD
{
	uint64_t FDIDATA;
	uint8_t MID;
	uint8_t TRACKS;
	uint8_t SECTORS;
	uint8_t SIDES;
	uint16_t BPS;
	uint8_t BPSCODE;
	char MAGIC;
	int WP;
	int HSIZE;
	int MFM;
	uint8_t THREEMODE;
	uint8_t BIGFLOPPY;                // 0 = 3.5", 1 = 5.25"
	// 0 = low, 1 = high, 2 = extended
	uint8_t DENSITY;
	uint32_t TOTAL;
	// So far: 0 = none, 1 = raw, 2 = PCem image, 3 = FDI
	uint8_t IMGTYPE;
	uint32_t RAWOFFS;
	int sstates;
	uint8_t XDF;
	int8_t CLASS;
	uint8_t LITE;
	uint8_t IDTYPE;
	uint8_t rws;
	uint8_t temp_bps;
	uint8_t temp_spt;
	uint8_t *disc[2][86][255];
	/* Actual track buffers, the "sectors" will be mere pointers to that */
	uint8_t trackbufs[2][86][25500];
	/* Sectors per track, data rate (0 = 500, 1 = 300, 2 = 250, 3 = 1000), RPM (0 = 300, 1 = 360), encoding (0 = FM, 1 = MFM) */
	uint8_t trackparams[2][86][4];
	// Sector ID fields
	uint8_t scid[2][86][255][5];
	uint8_t spt[86];
	// Sector states
	uint8_t sstat[2][86][255];
	uint8_t track;
	uint8_t trk0;
	int discchanged;
	int discmodified;

	uint8_t sectors_formatted;
	char *image_file;

	pef_header ph;
	char *ph_raw_image_temp;
	char *ph_raw_image_file;

	fdi_header fh;
	uint8_t floppy_drive_enabled;

	// This is needed for PEF images, since stuff like XDF like to mix sectors from the two heads in the order of reading
	// This is set while preparing sector ID's
	uint8_t sequential_sectors_index[21930][4];

	uint8_t driveempty;
	uint16_t ltpos;
} FDD;

extern FDD fdd[2];

/* For drive swap functionality. */
uint8_t vfdd[2];

extern int allocated;

uint8_t is_48tpi(int d);

void reconfigure_from_int(int d, int val);
void fdd_seek(int d, uint8_t n, uint8_t dir);

void fdd_init();

#define ISSPECIAL !(is_48tpi(d)) && (fdd[d].CLASS < CLASS_800) && (fdd[d].CLASS != -1)
#define FDDSPECIAL !(is_48tpi(vfdd[fdc.drive])) && (fdd[vfdd[fdc.drive]].CLASS < CLASS_800) && (fdd[vfdd[fdc.drive]].CLASS != -1)