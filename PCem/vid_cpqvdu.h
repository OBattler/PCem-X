typedef struct cpqvdu_t
{
        mem_mapping_t mapping;
        
        int crtcreg;
        uint8_t crtc[32];
        
        uint8_t cpqvdustat;
        
        uint8_t cpqvdumode, cpqvducol;

        int linepos, displine;
        int sc, vc;
        int cpqvdudispon;
        int con, coff, cursoron, cpqvdublink;
        int vsynctime, vadj;
        uint16_t ma, maback;
        int oddeven;

        int dispontime, dispofftime;
        int vidtime;
        
        int firstline, lastline;
        
        int drawcursor;
        
        uint8_t *vram;
        
        uint8_t charbuffer[256];
} cpqvdu_t;

void    cpqvdu_init(cpqvdu_t *cpqvdu);
void    cpqvdu_out(uint16_t addr, uint8_t val, void *p);
uint8_t cpqvdu_in(uint16_t addr, void *p);
void    cpqvdu_write(uint32_t addr, uint8_t val, void *p);
uint8_t cpqvdu_read(uint32_t addr, void *p);
void    cpqvdu_recalctimings(cpqvdu_t *cpqvdu);
void    cpqvdu_poll(void *p);

extern device_t cpqvdu_device;
