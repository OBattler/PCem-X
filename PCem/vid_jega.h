typedef struct jega_t
{
        mem_mapping_t mapping;
        mem_mapping_t necmapping;
        mem_mapping_t bufmapping;
        
        rom_t bios_rom;
        
        uint8_t crtcreg;
        uint8_t crtc[256];
        uint8_t gdcreg[16];
        int gdcaddr;
        uint8_t attrregs[32];
        int attraddr, attrff;
        uint8_t seqregs[64];
        int seqaddr;
        
        uint8_t miscout;
        int vidclock;

        uint8_t la, lb, lc, ld, le, lf, lg, lh;
        
        uint8_t stat;
        
        int fast;
        uint8_t colourcompare, colournocare;
        int readmode, writemode, readplane;
        int chain4, chain2;
        uint8_t writemask;
        uint32_t charseta, charsetb;
        
        uint8_t jegapal[16];
        uint32_t *pallook;

        int vtotal, dispend, vsyncstart, split, vblankstart;
        int hdisp,  htotal,  hdisp_time, rowoffset;
        int lowres, interlace;
        int linedbl, rowcount;
        double clock;
        uint32_t ma_latch;
        
        int vres;
        
        int dispontime, dispofftime;
        int vidtime;
        
        uint8_t scrblank;
        
        int dispon;
        int hdisp_on;

        uint32_t ma, vtma, maback, ca;
        int vc;
        int sc;
        int linepos, vslines, linecountff, oddeven;
        int con, cursoron, blink;
        int scrollcache;
        
        int firstline, lastline;
        int firstline_draw, lastline_draw;
        int displine;
        
        uint8_t *vram;
	uint8_t extram[32768];
        int vrammask;

        int video_res_x, video_res_y, video_bpp;
	int rconfig;

	int ccr;
	uint8_t cr[0x6C];
	uint16_t buffers[3];
	uint8_t storage[128];
	int multiread;
	int multiwrite;
	int mrcount;
	int mwcount;

	int country;
	/* uint8_t sbcs_chars16[256][16];
	uint8_t sbcs_chars19[256][19];
	uint16_t dbcs_chars16[256][256][2][19];
	uint8_t dbcs_chstat[256][256]; */
	uint8_t dbcs_userchars16[4][256][2][19];
	uint8_t dbcs_userchstat[4][256];
	int mode;
	int dispmode;
	int lastdbcs[2];
	int lastsbcs[2];
	int egamparams;
	int jegamparams;
	int colorbufferseg;
	int monobufferseg;
	void *mwtarget;
	void *mrsource;
	int lastegacharheight;
	int lastjegacharheight;
	int mwcurpos;
	int mrcurpos;
	int blockwrite;
	int forceattr;
} jega_t;

void   *jega_standalone_init();
void    jega_out(uint16_t addr, uint8_t val, void *p);
uint8_t jega_in(uint16_t addr, void *p);
void    jega_poll(void *p);
void    jega_recalctimings(struct jega_t *jega);
void    jega_write(uint32_t addr, uint8_t val, void *p);
uint8_t jega_read(uint32_t addr, void *p);

extern device_t jega_device;
